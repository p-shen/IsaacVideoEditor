#include "preview_cache.h"

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

PreviewCache::PreviewCache(const QString &cacheDirectory, QObject *parent)
    : QObject(parent), directory_(cacheDirectory.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("previews")
        : cacheDirectory), process_(new QProcess(this)), probeTimer_(new QTimer(this)) {
    probeTimer_->setSingleShot(true);
    connect(probeTimer_, &QTimer::timeout, this, [this] { if (probing_) process_->kill(); });
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        errors_.append(process_->readAllStandardError());
        if (errors_.size() > 4096) errors_ = errors_.right(4096);
    });
    connect(process_, &QProcess::readyReadStandardOutput, this, &PreviewCache::readProgress);
    connect(process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!cancelledPartial_.isEmpty()) {
            QFile::remove(cancelledPartial_);
            cancelledPartial_.clear();
        }
        if (current_.key.isEmpty()) {
            QTimer::singleShot(0, this, &PreviewCache::startNext);
            return;
        }
        if (probing_) finishProbe(code == 0 && status == QProcess::NormalExit);
        else complete(code == 0 && status == QProcess::NormalExit, QString::fromUtf8(errors_).trimmed());
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            if (probing_) finishProbe(false);
            else complete(false, process_->errorString());
        }
    });
}

PreviewCache::~PreviewCache() {
    process_->disconnect(this);
    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(1000);
    }
    if (!current_.partialPath.isEmpty()) QFile::remove(current_.partialPath);
    if (!cancelledPartial_.isEmpty()) QFile::remove(cancelledPartial_);
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
        + '\0' + QByteArrayLiteral("h264-540p-native-pcm-v2");
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
    return !current_.key.isEmpty() || !queue_.isEmpty() || process_->state() != QProcess::NotRunning;
}

void PreviewCache::request(const QString &source, int sourceFrames) {
    Job job = makeJob({source, sourceFrames, 0, sourceFrames});
    if (job.key.isEmpty()) {
        job.key = sourceKey(source) + ':' + QString::number(sourceFrames) + ":invalid";
        reportFailure(job, "The source video is missing or has no usable duration.");
        return;
    }
    sources_.insert(job.key, job);
    if (failed_.contains(job.key)) return;
    if (usableFile(job.outputPath)) { notifyReady(job); return; }
    notified_.remove(job.key);
    if (pending_.contains(job.key) || failed_.contains(job.key)) return;
    pending_.insert(job.key);
    queue_.enqueue(job);
    if (current_.key.isEmpty()) startNext();
}

void PreviewCache::discard(const QString &source) {
    for (const auto &job : sources_) {
        if (sourceKey(job.source) != sourceKey(source)) continue;
        failed_.insert(job.key);
        notified_.remove(job.key);
        QFile::remove(job.outputPath);
    }
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
        sources_.insert(job.key, job);
        desired.append(job);
    }
    if (!current_.key.isEmpty() && !keys.contains(current_.key)) cancelCurrent(false);
    queue_.clear();
    pending_.clear();
    if (!current_.key.isEmpty()) pending_.insert(current_.key);
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
    if (!current_.key.isEmpty() && sourceKey(current_.source) == sourceKey(source) &&
        current_.start <= inFrame && current_.end >= outFrame) return;
    for (int index = 0; index < queue_.size(); ++index) {
        const auto &job = queue_[index];
        if (sourceKey(job.source) == sourceKey(source) && job.start <= inFrame && job.end >= outFrame) {
            const Job urgent = queue_.takeAt(index);
            cancelCurrent(true);
            queue_.prepend(urgent);
            QTimer::singleShot(0, this, &PreviewCache::startNext);
            return;
        }
    }
}

void PreviewCache::cancelCurrent(bool requeue) {
    if (current_.key.isEmpty()) return;
    if (requeue) queue_.prepend(current_);
    else pending_.remove(current_.key);
    cancelledPartial_ = current_.partialPath;
    probing_ = false;
    probeTimer_->stop();
    current_ = Job{};
    if (process_->state() != QProcess::NotRunning) process_->kill();
    else {
        QFile::remove(cancelledPartial_);
        cancelledPartial_.clear();
    }
}

void PreviewCache::finishProbe(bool success) {
    probeTimer_->stop();
    probing_ = false;
    probeBuffer_.append(process_->readAllStandardOutput());
    if (jobKey(current_) != current_.key) {
        complete(false, "The source video changed while its timing was checked.");
        return;
    }
    cadencePeriods_.insert(fingerprint(current_.source, current_.frames), success ? verifiedCadencePeriod(probeBuffer_) : 0);
    pending_.remove(current_.key);
    current_ = Job{};
    probeBuffer_.clear();
    setRequests(requests_);
}

void PreviewCache::setPaused(bool paused) {
    if (paused_ == paused) return;
    paused_ = paused;
    if (paused) {
        cancelCurrent(true);
        if (process_->state() != QProcess::NotRunning) process_->waitForFinished(1000);
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
    if (!current_.key.isEmpty() && !keep.contains(sourceKey(current_.source))) {
        cancelCurrent(false);
        QTimer::singleShot(0, this, &PreviewCache::startNext);
        emit statusChanged(QString());
    }
}

void PreviewCache::notifyReady(const Job &job) {
    if (notified_.contains(job.key)) return;
    notified_.insert(job.key);
    QTimer::singleShot(0, this, [this, job] {
        if (usable(job)) emit ready(job.source, job.outputPath);
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
    if (paused_ || !current_.key.isEmpty() || queue_.isEmpty() || process_->state() != QProcess::NotRunning) return;
    current_ = queue_.dequeue();
    if (jobKey(current_) != current_.key) {
        complete(false, "The source video changed while its preview was queued.");
        return;
    }
    if (usableFile(current_.outputPath)) { complete(true); return; }
    if (current_.needsProbe) {
        const QString probe = executablePath("ffprobe");
        if (probe.isEmpty()) { finishProbe(false); return; }
        probing_ = true;
        probeBuffer_.clear();
        errors_.clear();
        emit statusChanged("Checking source timing…");
        probeTimer_->start(10000);
        process_->start(probe, {"-v", "error", "-select_streams", "v:0", "-show_entries",
            "stream=r_frame_rate,avg_frame_rate,time_base:packet=pts", "-of", "json", current_.source});
        return;
    }
    const QString ffmpeg = executablePath("ffmpeg");
    if (ffmpeg.isEmpty()) {
        complete(false, "FFmpeg is unavailable. Install FFmpeg to prepare lighter preview copies.");
        return;
    }
    if (!QDir().mkpath(directory_)) {
        complete(false, "Could not create the preview cache folder.");
        return;
    }
    QTemporaryFile temporary(QDir(directory_).filePath(current_.key + "-XXXXXX.part.mov"));
    if (!temporary.open()) { complete(false, "Could not write the preview cache folder."); return; }
    temporary.setAutoRemove(false);
    current_.partialPath = temporary.fileName();
    temporary.close();
    errors_.clear();
    progressBuffer_.clear();
    lastPercent_ = -1;
    const QString duration = QString::number((current_.end - current_.start) / 60.0, 'f', 9);
    // Bake camera orientation once; the editor still applies the user's separate rotation.
    const QString filter = "scale=w='max(2,trunc(min(960,min(iw*sar,540*dar))/2)*2)':"
        "h='max(2,trunc(ow/dar/2)*2)',setsar=1,"
        "tpad=stop_mode=clone:stop=-1";
    QStringList arguments{"-nostdin", "-hide_banner", "-loglevel", "error", "-y"};
    if (current_.start > 0) arguments << "-ss" << QString::number(current_.start / 60.0, 'f', 9);
    arguments << QStringList{
        "-i", current_.source,
        "-map", "0:v:0", "-map", "0:a:0?", "-vf", filter,
        "-fps_mode", "passthrough", "-t", duration,
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "23", "-g", "15", "-bf", "0",
        "-pix_fmt", "yuv420p", "-c:a", "pcm_s16le", "-ar", "48000", "-ac", "2", "-af", "apad",
        "-map_metadata", "-1", "-metadata:s:v:0", "rotate=0", "-movflags", "+faststart",
        "-progress", "pipe:1", "-nostats", current_.partialPath
    };
    emit statusChanged("Preparing lighter preview…");
    process_->start(ffmpeg, arguments);
}

void PreviewCache::complete(bool success, const QString &error) {
    if (current_.key.isEmpty()) return;
    const Job job = current_;
    current_ = Job{};
    pending_.remove(job.key);
    success = success && jobKey(job) == job.key;
    if (success && !usableFile(job.outputPath)) {
        success = usableFile(job.partialPath) && QFile::rename(job.partialPath, job.outputPath);
    }
    if (!job.partialPath.isEmpty()) QFile::remove(job.partialPath);
    if (success) {
        notifyReady(job);
        emit statusChanged(queue_.isEmpty() ? "Lighter preview ready" : "Preparing next preview…");
    } else reportFailure(job, error);
    QTimer::singleShot(0, this, &PreviewCache::startNext);
}

void PreviewCache::readProgress() {
    if (probing_) {
        probeBuffer_.append(process_->readAllStandardOutput());
        if (probeBuffer_.size() > 16 * 1024 * 1024) process_->kill();
        return;
    }
    progressBuffer_.append(process_->readAllStandardOutput());
    int end;
    while ((end = progressBuffer_.indexOf('\n')) >= 0) {
        const QByteArray line = progressBuffer_.left(end).trimmed();
        progressBuffer_.remove(0, end + 1);
        if (!line.startsWith("out_time_us=") || current_.end <= current_.start) continue;
        bool valid = false;
        const qint64 microseconds = line.mid(12).toLongLong(&valid);
        if (!valid) continue;
        const int percent = std::clamp(int(microseconds * 60.0 / ((current_.end - current_.start) * 10000.0)), 0, 99);
        if (percent == lastPercent_) continue;
        lastPercent_ = percent;
        emit statusChanged(QString("Preparing lighter preview… %1%").arg(percent));
    }
    if (progressBuffer_.size() > 4096) progressBuffer_ = progressBuffer_.right(4096);
}
