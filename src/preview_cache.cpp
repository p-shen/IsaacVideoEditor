#include "preview_cache.h"
#include "encoding.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
constexpr int MaximumQueuedJobs = 128;
QString executablePath(const QString &name) {
    const QString path = QStandardPaths::findExecutable(name);
    return path.isEmpty()
        ? QStandardPaths::findExecutable(name, {"/opt/homebrew/bin", "/usr/local/bin"}) : path;
}
bool usableFile(const QString &path) {
    const QFileInfo info(path);
    return info.isFile() && info.size() > 0;
}

int verifiedCadencePeriod(const QByteArray &json) {
    const auto root = QJsonDocument::fromJson(json).object();
    const auto streams = root.value("streams").toArray();
    const auto packets = root.value("packets").toArray();
    if (streams.size() != 1 || packets.size() < 2) return 0;
    const auto rational = [](const QString &value) {
        const auto parts = value.split('/');
        return parts.size() == 2 ? qMakePair(qint64(parts[0].toInt()), qint64(parts[1].toInt()))
                                 : qMakePair(qint64(0), qint64(0));
    };
    const auto stream = streams.first().toObject();
    const auto rate = rational(stream.value("r_frame_rate").toString());
    const auto average = rational(stream.value("avg_frame_rate").toString());
    const auto clock = rational(stream.value("time_base").toString());
    if (rate.first <= 0 || rate.second <= 0 || average.first <= 0 || average.second <= 0 ||
        clock.first <= 0 || clock.second <= 0 || rate.first * average.second != average.first * rate.second) return 0;
    QVector<qint64> timestamps;
    timestamps.reserve(packets.size());
    for (const auto &value : packets) {
        const auto packet = value.toObject();
        if (!packet.value("pts").isDouble()) return 0;
        timestamps.append(packet.value("pts").toInteger());
    }
    std::sort(timestamps.begin(), timestamps.end());
    if (timestamps.first() != 0) return 0;
    const long double unitsPerTick = static_cast<long double>(rate.first) * clock.first;
    for (int frame = 0; frame < timestamps.size(); ++frame) {
        const long double actual = static_cast<long double>(timestamps[frame]) * unitsPerTick;
        const long double expected = static_cast<long double>(frame) * rate.second * clock.second;
        if (std::abs(actual - expected) > unitsPerTick * 0.51L) return 0;
    }
    const qint64 cadence = 60 * rate.second / std::gcd(rate.first, 60 * rate.second);
    const qint64 tick = 60 * clock.first / std::gcd(clock.second, 60 * clock.first);
    if (cadence > 3600 || tick > 3600) return 0;
    const qint64 aligned = std::lcm(cadence, tick);
    return aligned <= 3600 ? int(aligned) : 0;
}
}

PreviewCache::PreviewCache(const QString &cacheDirectory, QObject *parent, const Performance::Budget &budget)
    : QObject(parent), directory_(cacheDirectory.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("previews")
        : cacheDirectory), budget_(budget) {
    for (int index = 0; index < std::clamp(budget_.proxyJobs, 1, 2); ++index) {
        auto *worker = new Worker;
        worker->process = new QProcess(this);
        worker->probeTimer = new QTimer(this);
        worker->probeTimer->setSingleShot(true);
        workers_.append(worker);
        connect(worker->probeTimer, &QTimer::timeout, this, [worker] { if (worker->probing) worker->process->kill(); });
        connect(worker->process, &QProcess::readyReadStandardError, this, [worker] {
            worker->errors.append(worker->process->readAllStandardError());
            if (worker->errors.size() > 4096) worker->errors = worker->errors.right(4096);
        });
        connect(worker->process, &QProcess::readyReadStandardOutput, this, [this, worker] { readProgress(worker); });
        connect(worker->process, &QProcess::finished, this, [this, worker](int code, QProcess::ExitStatus status) {
            readProgress(worker);
            worker->errors.append(worker->process->readAllStandardError());
            if (worker->probing) finishProbe(worker, code == 0 && status == QProcess::NormalExit);
            else complete(worker, code == 0 && status == QProcess::NormalExit, QString::fromUtf8(worker->errors).trimmed());
        });
        connect(worker->process, &QProcess::errorOccurred, this, [this, worker](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                if (worker->probing) finishProbe(worker, false);
                else complete(worker, false, worker->process->errorString());
            }
        });
    }
}

PreviewCache::~PreviewCache() {
    for (auto *worker : workers_) {
        worker->process->disconnect(this);
        worker->probeTimer->stop();
        if (worker->process->state() != QProcess::NotRunning) {
            worker->process->kill();
            worker->process->waitForFinished(1000);
        }
        if (!worker->job.partialPath.isEmpty()) QFile::remove(worker->job.partialPath);
        delete worker;
    }
}

QString PreviewCache::sourceKey(const QString &source) const {
    const QFileInfo info(source);
    return info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
}

QString PreviewCache::fingerprint(const QString &source, int frames) const {
    const QFileInfo info(source);
    if (!info.isFile() || frames < 1) return {};
    const QByteArray identity = sourceKey(source).toUtf8() + '\0' + QByteArray::number(info.size()) + '\0'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + '\0' + QByteArray::number(frames)
        + '\0' + QByteArray::number(std::clamp(budget_.previewWidth, 2, 1920)) + 'x'
        + QByteArray::number(std::clamp(budget_.previewHeight, 2, 1080))
        + '\0' + QByteArrayLiteral("h264-native-pcm-v3");
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

QString PreviewCache::readyPath(const QString &source) const {
    for (const auto &job : sources_)
        if (sourceKey(job.source) == sourceKey(source) && job.start == 0 && job.end == job.frames && usable(job))
            return job.outputPath;
    return {};
}

QString PreviewCache::jobKey(const Job &job) const {
    const QString base = fingerprint(job.source, job.frames);
    if (base.isEmpty() || (job.start == 0 && job.end == job.frames)) return base;
    return QString::fromLatin1(QCryptographicHash::hash(base.toLatin1() + ":range-v2:"
        + QByteArray::number(job.start) + ':' + QByteArray::number(job.end), QCryptographicHash::Sha256).toHex());
}

bool PreviewCache::usable(const Job &job) const {
    return !failed_.contains(job.key) && !job.key.isEmpty() && jobKey(job) == job.key && usableFile(job.outputPath);
}

PreviewCache::Copy PreviewCache::readyCopy(const QString &source, int inFrame, int outFrame) const {
    if (inFrame < 0 || outFrame <= inFrame) return {};
    const QString identity = sourceKey(source);
    for (const auto &job : sources_)
        if (sourceKey(job.source) == identity && job.start <= inFrame && job.end >= outFrame && usable(job))
            return {job.outputPath, job.start};
    return {};
}

PreviewCache::Job PreviewCache::makeJob(const Request &request) {
    Job job;
    job.source = request.source;
    job.frames = request.sourceFrames;
    if (request.inFrame < 0 || request.outFrame <= request.inFrame || request.outFrame > job.frames) return job;
    const QString base = fingerprint(job.source, job.frames);
    if (base.isEmpty()) return job;
    for (const auto &existing : sources_)
        if (sourceKey(existing.source) == sourceKey(job.source) && existing.frames == job.frames &&
            existing.start <= request.inFrame && existing.end >= request.outFrame && usable(existing)) return existing;
    // Existing full-source caches remain usable without any metadata migration.
    job.end = job.frames;
    job.key = base;
    job.outputPath = QDir(directory_).absoluteFilePath(base + ".mov");
    if (usable(job) || (request.inFrame == 0 && request.outFrame == job.frames)) return job;

    job.needsProbe = !cadencePeriods_.contains(base);
    const int period = cadencePeriods_.value(base);
    // Preserve resampling phase; unknown/variable cadence retains its original time origin.
    job.start = period > 0 ? std::max(0, request.inFrame - 60) / period * period : 0;
    job.end = int(std::min(qint64(job.frames), qint64(request.outFrame) + 60));
    job.key = jobKey(job);
    job.outputPath = QDir(directory_).absoluteFilePath(job.key + ".mov");
    return job;
}

bool PreviewCache::isBusy() const {
    return !queue_.isEmpty() || std::any_of(workers_.cbegin(), workers_.cend(), [](const Worker *worker) {
        return !worker->job.key.isEmpty() || worker->process->state() != QProcess::NotRunning;
    });
}

void PreviewCache::registerJob(Job &job) {
    const auto found = sources_.constFind(job.key);
    job.generation = found != sources_.cend() && (pending_.contains(job.key) || usable(*found))
        ? found->generation : ++nextGeneration_;
    sources_.insert(job.key, job);
}

void PreviewCache::request(const QString &source, int sourceFrames) {
    Job job = makeJob({source, sourceFrames, 0, sourceFrames});
    if (job.key.isEmpty()) {
        job.key = sourceKey(source) + ':' + QString::number(sourceFrames) + ":invalid";
        reportFailure(job, "The source video is missing or has no usable duration.");
        return;
    }
    registerJob(job);
    wanted_.insert(job.key);
    if (failed_.contains(job.key)) return;
    if (usableFile(job.outputPath)) { notifyReady(job); return; }
    notified_.remove(job.key);
    if (pending_.contains(job.key) || failed_.contains(job.key)) return;
    pending_.insert(job.key);
    if (queue_.size() >= MaximumQueuedJobs) {
        pending_.remove(queue_.last().key);
        queue_.removeLast();
    }
    queue_.enqueue(job);
    startNext();
}

void PreviewCache::discard(const QString &source) {
    const QString identity = sourceKey(source);
    for (const auto &job : sources_) {
        if (sourceKey(job.source) != identity) continue;
        failed_.insert(job.key);
        notified_.remove(job.key);
        QFile::remove(job.outputPath);
    }
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (sourceKey(it->source) == identity) { pending_.remove(it->key); it = queue_.erase(it); }
        else ++it;
    }
    for (auto *worker : workers_)
        if (!worker->job.key.isEmpty() && sourceKey(worker->job.source) == identity) cancel(worker, false);
}

void PreviewCache::setRequests(const QVector<Request> &requests) {
    requests_ = requests;
    QVector<Job> desired;
    QSet<QString> keys;
    for (const auto &request : requests) {
        Job job = makeJob(request);
        if (job.key.isEmpty()) {
            job.key = sourceKey(request.source) + ":invalid";
            reportFailure(job, "The source video is missing or its preview range is invalid.");
            continue;
        }
        if (keys.contains(job.key)) continue;
        keys.insert(job.key);
        registerJob(job);
        desired.append(job);
        if (desired.size() >= MaximumQueuedJobs) break;
    }
    wanted_ = keys;
    for (auto *worker : workers_)
        if (!worker->job.key.isEmpty() && !keys.contains(worker->job.key)) cancel(worker, false);
    queue_.clear();
    pending_.clear();
    for (const auto *worker : workers_)
        if (!worker->cancelled && !worker->job.key.isEmpty()) pending_.insert(worker->job.key);
    for (const auto &job : desired) {
        if (failed_.contains(job.key)) continue;
        if (usable(job)) { notifyReady(job); continue; }
        if (pending_.contains(job.key)) continue;
        notified_.remove(job.key);
        pending_.insert(job.key);
        queue_.enqueue(job);
    }
    startNext();
}

void PreviewCache::prioritize(const QString &source, int inFrame, int outFrame) {
    for (int row = 0; row < requests_.size(); ++row) {
        const auto &request = requests_[row];
        if (sourceKey(request.source) == sourceKey(source) && request.inFrame <= inFrame && request.outFrame >= outFrame) {
            if (row > 0) requests_.prepend(requests_.takeAt(row));
            break;
        }
    }
    if (!readyCopy(source, inFrame, outFrame).path.isEmpty()) return;
    for (const auto *worker : workers_)
        if (!worker->cancelled && !worker->job.key.isEmpty() && sourceKey(worker->job.source) == sourceKey(source) &&
            worker->job.start <= inFrame && worker->job.end >= outFrame) return;
    for (int index = 0; index < queue_.size(); ++index) {
        const auto &job = queue_[index];
        if (sourceKey(job.source) == sourceKey(source) && job.start <= inFrame && job.end >= outFrame) {
            const Job urgent = queue_.takeAt(index);
            const bool full = std::all_of(workers_.cbegin(), workers_.cend(), [](const Worker *worker) { return !worker->job.key.isEmpty(); });
            if (full) {
                auto victim = std::find_if(workers_.begin(), workers_.end(), [](const Worker *worker) { return !worker->cancelled; });
                if (victim != workers_.end()) cancel(*victim, true);
            }
            queue_.prepend(urgent);
            QTimer::singleShot(0, this, &PreviewCache::startNext);
            return;
        }
    }
}

void PreviewCache::cancel(Worker *worker, bool requeue) {
    if (worker->job.key.isEmpty() || worker->cancelled) return;
    worker->cancelled = true;
    worker->probeTimer->stop();
    if (requeue) {
        if (queue_.size() >= MaximumQueuedJobs) {
            pending_.remove(queue_.last().key);
            queue_.removeLast();
        }
        Job job = worker->job;
        job.partialPath.clear();
        queue_.prepend(job);
    } else pending_.remove(worker->job.key);
    if (worker->process->state() != QProcess::NotRunning) worker->process->kill();
    else complete(worker, false);
}

bool PreviewCache::isFresh(const Worker *worker) const {
    const auto found = sources_.constFind(worker->job.key);
    return !worker->cancelled && found != sources_.cend() && found->generation == worker->job.generation &&
        jobKey(worker->job) == worker->job.key && !failed_.contains(worker->job.key);
}

void PreviewCache::finishProbe(Worker *worker, bool success) {
    worker->probeTimer->stop();
    worker->probing = false;
    worker->probeBuffer.append(worker->process->readAllStandardOutput());
    if (!isFresh(worker)) { complete(worker, false); return; }
    cadencePeriods_.insert(fingerprint(worker->job.source, worker->job.frames), success ? verifiedCadencePeriod(worker->probeBuffer) : 0);
    pending_.remove(worker->job.key);
    worker->job = {};
    worker->probeBuffer.clear();
    setRequests(requests_);
}

void PreviewCache::setPaused(bool paused) {
    if (paused_ == paused) return;
    paused_ = paused;
    if (paused) {
        for (auto *worker : workers_) cancel(worker, true);
        if (!queue_.isEmpty()) emit statusChanged("Preview preparation paused during export");
    } else startNext();
}

void PreviewCache::retainSources(const QStringList &sources) {
    QSet<QString> keep;
    for (const auto &source : sources) keep.insert(sourceKey(source));
    requests_.erase(std::remove_if(requests_.begin(), requests_.end(), [&](const Request &request) {
        return !keep.contains(sourceKey(request.source));
    }), requests_.end());
    QQueue<Job> retained;
    while (!queue_.isEmpty()) {
        const Job job = queue_.dequeue();
        if (keep.contains(sourceKey(job.source))) retained.enqueue(job);
        else pending_.remove(job.key);
    }
    queue_.swap(retained);
    for (const auto &job : sources_)
        if (!keep.contains(sourceKey(job.source))) wanted_.remove(job.key);
    for (auto *worker : workers_)
        if (!worker->job.key.isEmpty() && !keep.contains(sourceKey(worker->job.source))) cancel(worker, false);
    QTimer::singleShot(0, this, &PreviewCache::startNext);
}

void PreviewCache::notifyReady(const Job &job) {
    if (notified_.contains(job.key)) return;
    notified_.insert(job.key);
    QTimer::singleShot(0, this, [this, job] {
        const auto found = sources_.constFind(job.key);
        if (wanted_.contains(job.key) && found != sources_.cend() && found->generation == job.generation && usable(job))
            emit ready(job.source, job.outputPath);
        else notified_.remove(job.key);
    });
}

void PreviewCache::reportFailure(const Job &job, const QString &error) {
    if (failed_.contains(job.key)) return;
    failed_.insert(job.key);
    emit failed(job.source, error.isEmpty() ? "Could not prepare a lighter preview copy." : error);
    emit statusChanged("Preview copy unavailable; using original video");
}

void PreviewCache::startNext() {
    if (paused_) return;
    for (auto *worker : workers_) {
        if (!worker->job.key.isEmpty() || worker->process->state() != QProcess::NotRunning || queue_.isEmpty()) continue;
        worker->job = queue_.dequeue();
        worker->cancelled = worker->probing = worker->hardware = false;
        worker->errors.clear();
        if (!isFresh(worker)) { complete(worker, false); continue; }
        if (usableFile(worker->job.outputPath)) { complete(worker, true); continue; }
        if (worker->job.needsProbe) {
            const QString probe = executablePath("ffprobe");
            if (probe.isEmpty()) { finishProbe(worker, false); continue; }
            worker->probing = true;
            worker->probeBuffer.clear();
            emit statusChanged("Checking source timing…");
            worker->probeTimer->start(10000);
            worker->process->start(probe, {"-v", "error", "-select_streams", "v:0", "-show_entries",
                "stream=r_frame_rate,avg_frame_rate,time_base:packet=pts", "-of", "json", worker->job.source});
        } else startEncoding(worker);
    }
}

void PreviewCache::startEncoding(Worker *worker, bool software) {
    const QString ffmpeg = executablePath("ffmpeg");
    if (ffmpeg.isEmpty()) {
        complete(worker, false, "FFmpeg is unavailable. Install FFmpeg to prepare lighter preview copies.");
        return;
    }
    if (!QDir().mkpath(directory_)) { complete(worker, false, "Could not create the preview cache folder."); return; }
    QTemporaryFile temporary(QDir(directory_).filePath(worker->job.key + "-XXXXXX.part.mov"));
    if (!temporary.open()) { complete(worker, false, "Could not write the preview cache folder."); return; }
    temporary.setAutoRemove(false);
    worker->job.partialPath = temporary.fileName();
    temporary.close();
    worker->errors.clear();
    worker->progressBuffer.clear();
    worker->lastPercent = -1;
    QString encoder = software || hardwareFailed_ ? QString("libx264") : Media::hardwareH264Encoder();
    if (encoder.isEmpty()) encoder = "libx264";
    worker->hardware = Media::isHardwareH264Encoder(encoder);
    const auto &job = worker->job;
    const QString duration = QString::number((job.end - job.start) / 60.0, 'f', 9);
    const QString threads = QString::number(std::clamp(budget_.backgroundThreads, 1, 8));
    // Bake camera orientation once; preserve native cadence and the verified range phase.
    const QString filter = QString("scale=w='max(2,trunc(min(%1,min(iw*sar,%2*dar))/2)*2)':"
        "h='max(2,trunc(ow/dar/2)*2)',setsar=1,tpad=stop_mode=clone:stop=-1")
        .arg(std::clamp(budget_.previewWidth, 2, 1920)).arg(std::clamp(budget_.previewHeight, 2, 1080));
    QStringList arguments{"-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-threads", threads,
        "-filter_threads", threads};
    if (job.start > 0) arguments << "-ss" << QString::number(job.start / 60.0, 'f', 9);
    arguments << "-i" << job.source << "-map" << "0:v:0" << "-map" << "0:a:0?" << "-vf" << filter
        << "-fps_mode" << "passthrough" << "-t" << duration << "-c:v" << encoder;
    if (encoder == "libx264") arguments << "-preset" << "veryfast" << "-crf" << "23";
    else if (encoder == "h264_nvenc") arguments << "-preset" << "p4" << "-rc" << "vbr" << "-cq" << "23" << "-b:v" << "0";
    else arguments << "-b:v" << "8M" << "-allow_sw" << "0";
    arguments << "-threads" << threads << "-g" << "15" << "-bf" << "0" << "-pix_fmt" << "yuv420p"
        << "-c:a" << "pcm_s16le" << "-ar" << "48000" << "-ac" << "2" << "-af" << "apad"
        << "-map_metadata" << "-1" << "-metadata:s:v:0" << "rotate=0" << "-movflags" << "+faststart"
        << "-progress" << "pipe:1" << "-nostats" << job.partialPath;
    emit statusChanged("Preparing lighter preview…");
    worker->process->start(ffmpeg, arguments);
}

void PreviewCache::complete(Worker *worker, bool success, const QString &error) {
    if (worker->job.key.isEmpty()) return;
    const bool fresh = isFresh(worker);
    if (!success && fresh && worker->hardware) {
        // A runtime failure disables this cache instance's hardware choice; retry each failed job once in software.
        hardwareFailed_ = true;
        worker->hardware = false;
        QFile::remove(worker->job.partialPath);
        worker->job.partialPath.clear();
        startEncoding(worker, true);
        return;
    }
    const Job job = worker->job;
    success = success && fresh;
    if (success && !usableFile(job.outputPath))
        success = usableFile(job.partialPath) && QFile::rename(job.partialPath, job.outputPath);
    if (!job.partialPath.isEmpty()) QFile::remove(job.partialPath);
    worker->job = {};
    worker->probing = worker->hardware = false;
    worker->probeTimer->stop();
    const bool replacementPending = std::any_of(queue_.cbegin(), queue_.cend(), [&job](const Job &queued) { return queued.key == job.key; }) ||
        std::any_of(workers_.cbegin(), workers_.cend(), [&job](const Worker *other) { return !other->cancelled && other->job.key == job.key; });
    if (!replacementPending) pending_.remove(job.key);
    if (success) {
        notifyReady(job);
        emit statusChanged(queue_.isEmpty() ? "Lighter preview ready" : "Preparing next preview…");
    } else if (fresh) reportFailure(job, error);
    QTimer::singleShot(0, this, &PreviewCache::startNext);
}

void PreviewCache::readProgress(Worker *worker) {
    if (worker->probing) {
        worker->probeBuffer.append(worker->process->readAllStandardOutput());
        if (worker->probeBuffer.size() > 16 * 1024 * 1024) worker->process->kill();
        return;
    }
    worker->progressBuffer.append(worker->process->readAllStandardOutput());
    if (worker->cancelled) { worker->progressBuffer.clear(); return; }
    int end;
    while ((end = worker->progressBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = worker->progressBuffer.left(end).trimmed();
        worker->progressBuffer.remove(0, end + 1);
        if (!line.startsWith("out_time_us=") || worker->job.end <= worker->job.start) continue;
        bool valid = false;
        const qint64 microseconds = line.mid(12).toLongLong(&valid);
        if (!valid) continue;
        const int percent = std::clamp(int(microseconds * 60.0 / ((worker->job.end - worker->job.start) * 10000.0)), 0, 99);
        if (percent == worker->lastPercent) continue;
        worker->lastPercent = percent;
        emit statusChanged(QString("Preparing lighter preview… %1%").arg(percent));
    }
    if (worker->progressBuffer.size() > 4096) worker->progressBuffer = worker->progressBuffer.right(4096);
}
