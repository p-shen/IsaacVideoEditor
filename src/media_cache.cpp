#include "media_cache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr int MaximumQueuedJobs = 128;
constexpr int MaximumQueuedPriorityJobs = 2;
constexpr int MaximumImageBytes = 1024 * 1024;
constexpr int MaximumBatchFrames = 8;
constexpr int MaximumBatchSpan = 120; // Two seconds bounds foreground preemption and decode work.
QImage nextPng(const QByteArray &bytes, qsizetype *offset) {
    const qsizetype start = *offset;
    if (bytes.mid(start, 8) != QByteArray::fromHex("89504e470d0a1a0a")) return {};
    qsizetype cursor = start + 8;
    while (cursor + 12 <= bytes.size()) {
        const quint32 length = qFromBigEndian<quint32>(bytes.constData() + cursor);
        if (length > quint32(MaximumImageBytes) || cursor + 12 + length > bytes.size()) return {};
        const bool last = bytes.mid(cursor + 4, 4) == "IEND";
        cursor += 12 + length;
        if (last) { *offset = cursor; return QImage::fromData(bytes.mid(start, cursor - start), "PNG"); }
    }
    return {};
}
QString ffmpegPath() {
    const QString path = QStandardPaths::findExecutable("ffmpeg");
    return path.isEmpty()
        ? QStandardPaths::findExecutable("ffmpeg", {"/opt/homebrew/bin", "/usr/local/bin"}) : path;
}
}

MediaCache::MediaCache(QObject *parent, const Performance::Budget &budget) : QObject(parent), budget_(budget) {
    images_.setMaxCost(std::max(1, budget_.thumbnailBytes));
    waveforms_.setMaxCost(std::max(int(sizeof(float)), budget_.waveformBytes));
    for (int index = 0; index < std::clamp(budget_.mediaJobs, 1, 2); ++index) {
        auto *worker = new Worker;
        worker->process = new QProcess(this);
        workers_.append(worker);
        connect(worker->process, &QProcess::readyReadStandardOutput, this, [this, worker] { readOutput(worker); });
        connect(worker->process, &QProcess::readyReadStandardError, this, [worker] { worker->process->readAllStandardError(); });
        connect(worker->process, &QProcess::started, this, [this, worker] { worker->elapsed.start(); ++processCount_; });
        connect(worker->process, &QProcess::finished, this, [this, worker](int code, QProcess::ExitStatus status) {
            if (worker->elapsed.isValid()) { preparationMs_ += worker->elapsed.elapsed(); worker->elapsed.invalidate(); }
            readOutput(worker);
            complete(worker, code == 0 && status == QProcess::NormalExit);
        });
        connect(worker->process, &QProcess::errorOccurred, this, [this, worker](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                ffmpegUnavailable_ = true;
                complete(worker, false);
            }
        });
    }
}

MediaCache::~MediaCache() {
    for (auto *worker : workers_) {
        auto *process = worker->process;
        process->disconnect(this);
        // Let the event loop reap a killed process without blocking the GUI on destruction.
        if (process->state() != QProcess::NotRunning) {
            process->setParent(nullptr);
            connect(process, &QProcess::finished, process, &QObject::deleteLater);
            process->kill();
        }
        delete worker;
    }
}

QString MediaCache::sourceKey(const QString &path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QString MediaCache::fingerprint(const QString &path) {
    const QFileInfo info(path);
    const QByteArray identity = sourceKey(path).toUtf8() + '\0' + QByteArray::number(info.size()) + '\0'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + '\0'
        + QByteArrayLiteral("thumbnail-60fps-160x90-autorotate-nearest-v2;pcm-mono-2000hz-100samples-fixed-v1");
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

MediaCache::Source &MediaCache::source(const QString &path) {
    const QString key = sourceKey(path), identity = fingerprint(path);
    auto found = sources_.find(key);
    if (found != sources_.end() && found->identity != identity) {
        discardSource(key);
        found = sources_.end();
    }
    if (found == sources_.end())
        found = sources_.insert(key, Source{path, identity, {}, ++nextGeneration_});
    return found.value();
}

QImage MediaCache::thumbnail(const QString &path, double sourceSeconds, int rotationDegrees, bool priority, int toleranceFrames) {
    if (path.isEmpty() || !std::isfinite(sourceSeconds) || sourceSeconds < 0 ||
        sourceSeconds > double(std::numeric_limits<qint64>::max() / 60 - 1)) return {};
    const auto &entry = source(path);
    qint64 frame = qRound64(sourceSeconds * 60);
    const int rotation = (rotationDegrees % 360 + 360) % 360;
    if (toleranceFrames > 0) {
        const qint64 requested = frame;
        qint64 nearest = qint64(toleranceFrames) + 1;
        const auto consider = [&](qint64 candidate) {
            const qint64 distance = std::abs(candidate - requested);
            if (distance < nearest) { nearest = distance; frame = candidate; }
        };
        const QString prefix = entry.identity + ':', suffix = ':' + QString::number(rotation);
        for (const auto &key : images_.keys()) {
            if (!key.startsWith(prefix) || !key.endsWith(suffix)) continue;
            consider(key.mid(prefix.size(), key.size() - prefix.size() - suffix.size()).toLongLong());
        }
        if (nearest > toleranceFrames) {
            for (const auto *worker : workers_)
                if (!worker->job.cancelled)
                    for (const auto &job : worker->batch)
                        if (job.kind == Thumbnail && job.identity == entry.identity && job.rotation == rotation) consider(job.frame);
            for (const auto &job : queue_)
                if (job.kind == Thumbnail && job.identity == entry.identity && job.rotation == rotation) consider(job.frame);
        }
    }
    const QString key = entry.identity + ':' + QString::number(frame) + ':' + QString::number(rotation);
    if (viewport_) viewportRequests_.insert(key);
    if (const auto image = images_.object(key)) { ++thumbnailHits_; return *image; }
    ++thumbnailMisses_;
    if (priority && pending_.contains(key)) {
        for (auto *worker : workers_)
            if (!worker->job.cancelled && worker->batch.size() > 1 &&
                std::any_of(worker->batch.cbegin(), worker->batch.cend(), [&key](const Job &job) { return job.key == key; }))
                cancel(worker, true);
        auto queued = std::find_if(queue_.begin(), queue_.end(), [&key](const Job &job) { return job.key == key; });
        if (queued != queue_.end()) {
            pending_.remove(key);
            queue_.erase(queued);
        }
    }
    if (!pending_.contains(key) && !failures_.contains(key)) {
        Job job{sourceKey(path), entry.path, entry.identity, key, entry.generation, frame, rotation, Thumbnail, priority};
        job.batchable = toleranceFrames > 0 && !priority;
        enqueue(job);
    }
    return {};
}

QVector<float> MediaCache::waveform(const QString &path) {
    if (path.isEmpty()) return {};
    auto &entry = source(path);
    const QString key = entry.identity + ":waveform";
    if (const auto peaks = waveforms_.object(key)) return *peaks;
    if (entry.waveformState == "unavailable") return {};
    if (!pending_.contains(key) && enqueue(Job{sourceKey(path), entry.path, entry.identity, key, entry.generation, 0, 0, Waveform}))
        entry.waveformState = "loading";
    return {};
}

QString MediaCache::waveformState(const QString &path) const {
    if (path.isEmpty()) return {};
    const auto found = sources_.constFind(sourceKey(path));
    if (found == sources_.cend() || found->identity != fingerprint(path)) return {};
    return found->waveformState;
}

void MediaCache::setInteractive(bool interactive) {
    interactive_ = interactive;
    if (!interactive_) schedule();
}

void MediaCache::retainSources(const QStringList &sources) {
    QSet<QString> keep;
    for (const QString &path : sources) keep.insert(sourceKey(path));
    const auto keys = sources_.keys();
    for (const QString &key : keys) if (!keep.contains(key)) discardSource(key);
}

void MediaCache::beginViewport() {
    viewport_ = true;
    viewportRequests_.clear();
}

void MediaCache::endViewport() {
    if (!viewport_) return;
    viewport_ = false;
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->kind == Thumbnail && !it->priority && !viewportRequests_.contains(it->key)) {
            pending_.remove(it->key);
            it = queue_.erase(it);
        } else ++it;
    }
    viewportRequests_.clear();
}

void MediaCache::retry(const QString &path) {
    ffmpegUnavailable_ = false;
    discardSource(sourceKey(path));
    emit changed();
}

void MediaCache::discardSource(const QString &key) {
    const auto found = sources_.constFind(key);
    if (found == sources_.cend()) return;
    const QString prefix = found->identity + ':';
    for (const QString &cacheKey : images_.keys()) if (cacheKey.startsWith(prefix)) images_.remove(cacheKey);
    for (const QString &cacheKey : waveforms_.keys()) if (cacheKey.startsWith(prefix)) waveforms_.remove(cacheKey);
    for (const QString &cacheKey : failures_.keys()) if (cacheKey.startsWith(prefix)) failures_.remove(cacheKey);
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->source == key) {
            pending_.remove(it->key);
            it = queue_.erase(it);
        } else ++it;
    }
    for (auto *worker : workers_)
        if (!worker->job.key.isEmpty() && worker->job.source == key) cancel(worker, false);
    sources_.remove(key);
}

bool MediaCache::enqueue(const Job &job) {
    if (job.priority) {
        while (std::count_if(queue_.cbegin(), queue_.cend(), [](const Job &queued) { return queued.priority; }) >= MaximumQueuedPriorityJobs) {
            auto oldest = std::find_if(queue_.begin(), queue_.end(), [](const Job &queued) { return queued.priority; });
            pending_.remove(oldest->key);
            queue_.erase(oldest);
        }
    }
    if (queue_.size() >= MaximumQueuedJobs) {
        auto oldest = std::find_if(queue_.begin(), queue_.end(), [](const Job &queued) { return queued.kind == Thumbnail && !queued.priority; });
        if (oldest == queue_.end()) {
            if (!job.priority) return false;
            oldest = std::find_if(queue_.begin(), queue_.end(), [](const Job &queued) { return !queued.priority; });
            if (oldest == queue_.end()) return false;
            auto source = sources_.find(oldest->source);
            if (source != sources_.end() && source->generation == oldest->generation) source->waveformState.clear();
        }
        pending_.remove(oldest->key);
        queue_.erase(oldest);
    }
    queue_.enqueue(job);
    pending_.insert(job.key);
    schedule();
    return true;
}

void MediaCache::schedule() {
    if (scheduled_) return;
    scheduled_ = true;
    QTimer::singleShot(0, this, [this] {
        scheduled_ = false;
        startNext();
    });
}

bool MediaCache::isFresh(const Job &job) const {
    const auto found = sources_.constFind(job.source);
    return !job.cancelled && found != sources_.cend() && found->generation == job.generation &&
        found->identity == job.identity && fingerprint(job.path) == job.identity;
}

void MediaCache::cancel(Worker *worker, bool requeue) {
    if (worker->job.key.isEmpty() || worker->job.cancelled) return;
    worker->job.cancelled = true;
    for (const auto &job : worker->batch) {
        pending_.remove(job.key);
        if (requeue && !enqueue(job)) {
            auto source = sources_.find(job.source);
            if (source != sources_.end() && source->generation == job.generation) source->waveformState.clear();
        }
    }
    if (worker->process->state() != QProcess::NotRunning) worker->process->kill();
    else complete(worker, false);
}

void MediaCache::startNext() {
    // A waveform can run for hours; release a slot for comparison frames even on a one-worker machine.
    if (std::any_of(queue_.cbegin(), queue_.cend(), [](const Job &job) { return job.priority; }) &&
        std::none_of(workers_.cbegin(), workers_.cend(), [](const Worker *worker) { return worker->job.key.isEmpty(); })) {
        const auto waveform = std::find_if(workers_.begin(), workers_.end(), [](const Worker *worker) {
            return !worker->job.cancelled && worker->job.kind == Waveform;
        });
        if (waveform != workers_.end()) cancel(*waveform, true);
        else {
            const auto batch = std::find_if(workers_.begin(), workers_.end(), [](const Worker *worker) {
                return !worker->job.cancelled && !worker->job.priority && worker->batch.size() > 1;
            });
            if (batch != workers_.end()) cancel(*batch, true);
        }
    }
    for (auto *worker : workers_) {
        if (!worker->job.key.isEmpty() || worker->process->state() != QProcess::NotRunning || queue_.isEmpty()) continue;
        auto next = std::find_if(queue_.begin(), queue_.end(), [](const Job &job) { return job.priority; });
        if (next == queue_.end()) {
            if (interactive_ && !budget_.prepareDuringExport) return;
            next = queue_.begin();
        }
        worker->job = *next;
        queue_.erase(next);
        worker->batch = {worker->job};
        if (worker->job.batchable) {
            const auto first = worker->job;
            for (auto it = queue_.begin(); it != queue_.end() && worker->batch.size() < MaximumBatchFrames;) {
                if (it->batchable && it->source == first.source && it->generation == first.generation &&
                    it->identity == first.identity && it->rotation == first.rotation &&
                    std::abs(it->frame - first.frame) <= MaximumBatchSpan / 2) {
                    worker->batch.append(*it); it = queue_.erase(it);
                } else ++it;
            }
            std::sort(worker->batch.begin(), worker->batch.end(), [](const Job &a, const Job &b) { return a.frame < b.frame; });
            worker->job = worker->batch.first();
        }
        worker->imageBytes.clear();
        worker->peaks.clear();
        worker->binSamples = 0;
        worker->binPeak = 0;
        worker->pcmLowByte = -1;
        worker->outputOverflow = false;
        const auto &job = worker->job;
        const QFileInfo info(job.path);
        const QString ffmpeg = ffmpegUnavailable_ ? QString() : ffmpegPath();
        if (ffmpeg.isEmpty()) ffmpegUnavailable_ = true;
        if (!isFresh(job) || !info.isFile() || info.size() == 0 || ffmpeg.isEmpty()) {
            complete(worker, false);
            continue;
        }
        const QString threads = QString::number(std::clamp(budget_.backgroundThreads, 1, 8));
        QStringList arguments{"-nostdin", "-hide_banner", "-loglevel", "error", "-threads", threads, "-filter_threads", threads};
        if (worker->job.kind == Thumbnail) {
            arguments << "-ss" << QString::number(worker->job.frame / 60.0, 'f', 9) << "-noaccurate_seek"
                      << "-i" << worker->job.path << "-ss" << "0" << "-map" << "0:v:0";
            QString filter;
            if (worker->job.rotation == 90) filter = "transpose=clock,";
            else if (worker->job.rotation == 180) filter = "hflip,vflip,";
            else if (worker->job.rotation == 270) filter = "transpose=cclock,";
            else if (worker->job.rotation != 0) {
                const QString angle = QString::number(worker->job.rotation) + "*PI/180";
                filter = "rotate=" + angle + ":ow=rotw(" + angle + "):oh=roth(" + angle + "),";
            }
            // Match MLT's nearest native frame, including half-frame ties on 30 fps footage.
            // FFmpeg applies camera orientation before this explicit user rotation.
            filter = "tpad=stop=1:stop_mode=clone,setpts=ceil(PTS-0.5/(FRAME_RATE*TB)),fps=60:round=up," + filter;
            filter += "scale=w='max(1,trunc(min(160,min(iw*sar,90*dar))))':h='max(1,trunc(ow/dar))',setsar=1";
            if (worker->batch.size() > 1) {
                QStringList positions;
                for (const auto &frame : worker->batch) positions.append(QString("eq(pts,%1)").arg(frame.frame - worker->job.frame));
                filter += ",select='" + positions.join('+') + "'";
                arguments << "-fps_mode" << "passthrough";
                batchedFrames_ += worker->batch.size();
            }
            ++thumbnailProcesses_;
            arguments << "-vf" << filter << "-frames:v" << QString::number(worker->batch.size()) << "-an" << "-c:v" << "png" << "-threads" << threads << "-f" << "image2pipe" << "pipe:1";
        } else {
            arguments << "-i" << worker->job.path << "-map" << "0:a:0" << "-vn" << "-ac" << "1" << "-ar" << "2000"
                      << "-c:a" << "pcm_s16le" << "-threads" << threads << "-f" << "s16le" << "pipe:1";
        }
        worker->process->start(ffmpeg, arguments);
    }
}

void MediaCache::readOutput(Worker *worker) {
    const int maximumBins = waveforms_.maxCost() / int(sizeof(float));
    const QByteArray bytes = worker->process->readAllStandardOutput();
    if (worker->job.key.isEmpty() || worker->job.cancelled || worker->outputOverflow) return;
    if (worker->job.kind == Thumbnail) {
        if (worker->imageBytes.size() + bytes.size() <= MaximumImageBytes * worker->batch.size()) worker->imageBytes.append(bytes);
        else { worker->outputOverflow = true; worker->process->kill(); }
        return;
    }
    for (const unsigned char byte : bytes) {
        if (worker->pcmLowByte < 0) { worker->pcmLowByte = byte; continue; }
        const qint16 sample = qint16(worker->pcmLowByte | (int(byte) << 8));
        worker->pcmLowByte = -1;
        worker->binPeak = std::max(worker->binPeak, std::abs(int(sample)) / 32768.0f);
        if (++worker->binSamples != 100) continue;
        if (worker->peaks.size() >= maximumBins) { worker->outputOverflow = true; worker->process->kill(); break; }
        worker->peaks.append(std::clamp(worker->binPeak, 0.0f, 1.0f));
        worker->binSamples = 0;
        worker->binPeak = 0;
    }
}

void MediaCache::complete(Worker *worker, bool success) {
    const int maximumBins = waveforms_.maxCost() / int(sizeof(float));
    if (worker->job.key.isEmpty()) return;
    bool changed = false;
    qsizetype pngOffset = 0;
    for (const auto &job : worker->batch) {
        const bool fresh = !worker->job.cancelled && isFresh(job);
        bool valid = success && !worker->outputOverflow;
        QImage image;
        if (job.kind == Thumbnail && valid) image = nextPng(worker->imageBytes, &pngOffset);
        if (fresh) {
            if (job.kind == Thumbnail) {
                valid = !image.isNull() && image.width() <= 160 && image.height() <= 90;
                if (valid) images_.insert(job.key, new QImage(image), int(image.sizeInBytes()));
                else failures_.insert(job.key, new bool(true));
            } else {
                if (valid && worker->binSamples > 0) {
                    if (worker->peaks.size() < maximumBins) worker->peaks.append(std::clamp(worker->binPeak, 0.0f, 1.0f));
                    else valid = false;
                }
                valid = valid && !worker->peaks.isEmpty();
                if (valid) waveforms_.insert(job.key, new QVector<float>(worker->peaks), int(worker->peaks.size() * sizeof(float)));
                sources_[job.source].waveformState = valid ? "ready" : "unavailable";
            }
            changed = true;
        }
        // A replacement may already have queued the same key while a cancelled process exits.
        const bool replacementPending = std::any_of(queue_.cbegin(), queue_.cend(), [&job](const Job &queued) { return queued.key == job.key; }) ||
            std::any_of(workers_.cbegin(), workers_.cend(), [worker, &job](const Worker *other) {
                return other != worker && !other->job.cancelled && std::any_of(other->batch.cbegin(), other->batch.cend(),
                    [&job](const Job &active) { return active.key == job.key; });
            });
        if (!replacementPending) pending_.remove(job.key);
    }
    worker->job = {};
    worker->batch.clear();
    worker->imageBytes = {};
    worker->peaks = {};
    if (changed) emit this->changed();
    schedule();
}

QVariantMap MediaCache::diagnostics() const {
    return {{"preparationMs", preparationMs_}, {"processes", QVariant::fromValue(processCount_)}, {"thumbnailHits", QVariant::fromValue(thumbnailHits_)}, {"thumbnailMisses", QVariant::fromValue(thumbnailMisses_)},
            {"thumbnailProcesses", QVariant::fromValue(thumbnailProcesses_)}, {"batchedFrames", QVariant::fromValue(batchedFrames_)},
            {"queuedJobs", queue_.size()}, {"thumbnailBytes", images_.totalCost()}, {"waveformBytes", waveforms_.totalCost()}};
}
