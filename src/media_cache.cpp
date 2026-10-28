#include "media_cache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr int MaximumQueuedJobs = 128;
constexpr int MaximumQueuedPriorityJobs = 2;
constexpr int MaximumImageBytes = 1024 * 1024;
constexpr int MaximumWaveformBins = 8 * 1024 * 1024 / sizeof(float);
QString ffmpegPath() {
    const QString path = QStandardPaths::findExecutable("ffmpeg");
    return path.isEmpty()
        ? QStandardPaths::findExecutable("ffmpeg", {"/opt/homebrew/bin", "/usr/local/bin"}) : path;
}
}

MediaCache::MediaCache(QObject *parent) : QObject(parent), process_(new QProcess(this)) {
    connect(process_, &QProcess::readyReadStandardOutput, this, &MediaCache::readOutput);
    connect(process_, &QProcess::readyReadStandardError, this, [this] { process_->readAllStandardError(); });
    connect(process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readOutput();
        complete(code == 0 && status == QProcess::NormalExit);
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            ffmpegUnavailable_ = true;
            complete(false);
        }
    });
}

MediaCache::~MediaCache() {
    // Let the event loop reap a killed process without blocking the GUI on destruction.
    process_->disconnect(this);
    if (process_->state() != QProcess::NotRunning) {
        process_->setParent(nullptr);
        connect(process_, &QProcess::finished, process_, &QObject::deleteLater);
        process_->kill();
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
            if (!current_.cancelled && current_.kind == Thumbnail && current_.identity == entry.identity && current_.rotation == rotation)
                consider(current_.frame);
            for (const auto &job : queue_)
                if (job.kind == Thumbnail && job.identity == entry.identity && job.rotation == rotation) consider(job.frame);
        }
    }
    const QString key = entry.identity + ':' + QString::number(frame) + ':' + QString::number(rotation);
    if (viewport_) viewportRequests_.insert(key);
    if (const auto image = images_.object(key)) return *image;
    if (priority && pending_.contains(key)) {
        auto queued = std::find_if(queue_.begin(), queue_.end(), [&key](const Job &job) { return job.key == key; });
        if (queued != queue_.end()) {
            pending_.remove(key);
            queue_.erase(queued);
        }
    }
    if (!pending_.contains(key) && !failures_.contains(key))
        enqueue(Job{sourceKey(path), entry.path, entry.identity, key, entry.generation, frame, rotation, Thumbnail, priority});
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
    if (!current_.key.isEmpty() && current_.source == key) {
        current_.cancelled = true;
        pending_.remove(current_.key);
        process_->kill();
    }
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

bool MediaCache::currentIsFresh() const {
    const auto found = sources_.constFind(current_.source);
    return !current_.cancelled && found != sources_.cend() && found->generation == current_.generation &&
        found->identity == current_.identity && fingerprint(current_.path) == current_.identity;
}

void MediaCache::startNext() {
    if (!current_.key.isEmpty() || process_->state() != QProcess::NotRunning || queue_.isEmpty()) return;
    auto next = std::find_if(queue_.begin(), queue_.end(), [](const Job &job) { return job.priority; });
    if (next == queue_.end()) {
        if (interactive_) return;
        next = queue_.begin();
    }
    current_ = *next;
    queue_.erase(next);
    imageBytes_.clear();
    peaks_.clear();
    binSamples_ = 0;
    binPeak_ = 0;
    pcmLowByte_ = -1;
    outputOverflow_ = false;
    const QFileInfo info(current_.path);
    const QString ffmpeg = ffmpegUnavailable_ ? QString() : ffmpegPath();
    if (ffmpeg.isEmpty()) ffmpegUnavailable_ = true;
    if (!currentIsFresh() || !info.isFile() || info.size() == 0 || ffmpeg.isEmpty()) {
        complete(false);
        return;
    }
    QStringList arguments{"-nostdin", "-hide_banner", "-loglevel", "error"};
    if (current_.kind == Thumbnail) {
        arguments << "-ss" << QString::number(current_.frame / 60.0, 'f', 9) << "-noaccurate_seek"
                  << "-i" << current_.path << "-ss" << "0" << "-map" << "0:v:0";
        QString filter;
        if (current_.rotation == 90) filter = "transpose=clock,";
        else if (current_.rotation == 180) filter = "hflip,vflip,";
        else if (current_.rotation == 270) filter = "transpose=cclock,";
        else if (current_.rotation != 0) {
            const QString angle = QString::number(current_.rotation) + "*PI/180";
            filter = "rotate=" + angle + ":ow=rotw(" + angle + "):oh=roth(" + angle + "),";
        }
        // Match MLT's nearest native frame, including half-frame ties on 30 fps footage.
        // FFmpeg applies camera orientation before this explicit user rotation.
        filter = "tpad=stop=1:stop_mode=clone,setpts=ceil(PTS-0.5/(FRAME_RATE*TB)),fps=60:round=up," + filter;
        filter += "scale=w='max(1,trunc(min(160,min(iw*sar,90*dar))))':h='max(1,trunc(ow/dar))',setsar=1";
        arguments << "-vf" << filter << "-frames:v" << "1" << "-an" << "-c:v" << "png" << "-f" << "image2pipe" << "pipe:1";
    } else {
        arguments << "-i" << current_.path << "-map" << "0:a:0" << "-vn" << "-ac" << "1" << "-ar" << "2000"
                  << "-c:a" << "pcm_s16le" << "-f" << "s16le" << "pipe:1";
    }
    process_->start(ffmpeg, arguments);
}

void MediaCache::readOutput() {
    const QByteArray bytes = process_->readAllStandardOutput();
    if (current_.key.isEmpty() || current_.cancelled || outputOverflow_) return;
    if (current_.kind == Thumbnail) {
        if (imageBytes_.size() + bytes.size() <= MaximumImageBytes) imageBytes_.append(bytes);
        else { outputOverflow_ = true; process_->kill(); }
        return;
    }
    for (const unsigned char byte : bytes) {
        if (pcmLowByte_ < 0) { pcmLowByte_ = byte; continue; }
        const qint16 sample = qint16(pcmLowByte_ | (int(byte) << 8));
        pcmLowByte_ = -1;
        binPeak_ = std::max(binPeak_, std::abs(int(sample)) / 32768.0f);
        if (++binSamples_ != 100) continue;
        // shortcut: about 29 hours of 20 Hz peaks, raise the budget if longer assets are required.
        if (peaks_.size() >= MaximumWaveformBins) { outputOverflow_ = true; process_->kill(); break; }
        peaks_.append(std::clamp(binPeak_, 0.0f, 1.0f));
        binSamples_ = 0;
        binPeak_ = 0;
    }
}

void MediaCache::complete(bool success) {
    if (current_.key.isEmpty()) return;
    const Job job = current_;
    const bool fresh = currentIsFresh();
    success = success && !outputOverflow_;
    if (fresh) {
        if (job.kind == Thumbnail) {
            const QImage image = success ? QImage::fromData(imageBytes_, "PNG") : QImage();
            success = !image.isNull() && image.width() <= 160 && image.height() <= 90;
            if (success) images_.insert(job.key, new QImage(image), int(image.sizeInBytes()));
            else failures_.insert(job.key, new bool(true));
        } else {
            if (success && binSamples_ > 0) {
                if (peaks_.size() < MaximumWaveformBins) peaks_.append(std::clamp(binPeak_, 0.0f, 1.0f));
                else success = false;
            }
            success = success && !peaks_.isEmpty();
            if (success) waveforms_.insert(job.key, new QVector<float>(peaks_), int(peaks_.size() * sizeof(float)));
            sources_[job.source].waveformState = success ? "ready" : "unavailable";
        }
    }
    // A replacement may already have queued the same key while a cancelled process exits.
    const bool replacementPending = std::any_of(queue_.cbegin(), queue_.cend(), [&job](const Job &queued) { return queued.key == job.key; });
    if (!replacementPending) pending_.remove(job.key);
    current_ = {};
    imageBytes_ = {};
    peaks_ = {};
    if (fresh) emit changed();
    schedule();
}
