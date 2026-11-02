#include "preview_cache.h"
#include "encoding.h"
#include "source_timing.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QUuid>
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


constexpr auto Owner = "IsaacVideoEditor preview cache";
constexpr auto RecordSuffix = ".isaac-cache.json";
QString recordPath(const QString &path) { return path + RecordSuffix; }
std::shared_ptr<QLockFile> lockFile(const QString &path, QString *error = nullptr) {
    auto lock = std::make_shared<QLockFile>(path);
    lock->setStaleLockTime(0); // Running processes retain leases for arbitrarily long edits.
    if (lock->tryLock(0)) return lock;
    if (error && lock->error() != QLockFile::LockFailedError)
        *error = "The preview cache folder is not writable. Choose another folder in Settings.";
    return nullptr;
}
std::shared_ptr<QLockFile> directoryLock(const QString &directory, QString *error = nullptr) {
    if (!QDir().mkpath(directory)) {
        if (error) *error = "Could not create the preview cache folder. Choose another folder in Settings.";
        return nullptr;
    }
    return lockFile(QDir(directory).filePath(".isaac-preview.lock"), error);
}
bool writeJson(const QString &path, const QJsonObject &object) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) > 0 && file.commit();
}
bool recordArtifact(const QString &path, const QString &key, const QString &kind) {
    const QFileInfo existing(recordPath(path));
    if (existing.isSymLink()) return false;
    if (existing.exists()) {
        QFile file(existing.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) return false;
        const auto json = QJsonDocument::fromJson(file.readAll()).object();
        if (json.value("owner").toString() != Owner || json.value("version").toInt() != 1 ||
            json.value("key").toString() != key || json.value("file").toString() != QFileInfo(path).fileName()) return false;
    }
    return writeJson(recordPath(path), {{"owner", Owner}, {"version", 1}, {"file", QFileInfo(path).fileName()},
                                      {"key", key}, {"kind", kind}});
}
bool validKey(const QString &key) {
    return key.size() == 64 && std::all_of(key.cbegin(), key.cend(), [](QChar c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
bool invalidArtifact(const QString &path) {
    QFile file(recordPath(path));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) return false;
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    return json.value("owner").toString() == Owner && json.value("file").toString() == QFileInfo(path).fileName() &&
        json.value("invalid").toBool();
}
bool usablePreview(const QString &path) { return usableFile(path) && !invalidArtifact(path); }
bool protectedArtifact(const QString &directory, const QString &key, const QString &ignore = QString()) {
    const auto entries = QDir(directory).entryList({key + ".use-*.lock", key + ".write.lock"}, QDir::Files);
    for (const auto &entry : entries) {
        if (entry == ignore || (!ignore.isEmpty() && entry == key + ".write.lock")) continue;
        const auto lock = lockFile(QDir(directory).filePath(entry));
        if (!lock) return true;
    }
    return false;
}

}

PreviewCache::PreviewCache(const QString &cacheDirectory, QObject *parent, const Performance::Budget &budget)
    : QObject(parent), directory_(cacheDirectory.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("previews")
        : cacheDirectory), budget_(budget) {
    persistentSettings_ = cacheDirectory.isEmpty();
    instanceId_ = QUuid::createUuid().toString(QUuid::Id128);
    if (persistentSettings_) {
        QSettings settings;
        directory_ = settings.value("preview/cacheDirectory", directory_).toString();
        storageLimit_ = std::max(qint64(0), settings.value("preview/storageLimitBytes", storageLimit_).toLongLong());
        if (settings.contains("preview/width") && settings.contains("preview/height")) {
            budget_.previewWidth = std::clamp(settings.value("preview/width").toInt(), 2, 3840);
            budget_.previewHeight = std::clamp(settings.value("preview/height").toInt(), 2, 2160);
        }
    }
    directory_ = QDir(directory_).absolutePath();
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
        connect(worker->process, &QProcess::started, this, [this, worker] { worker->elapsed.start(); ++processCount_; });
        connect(worker->process, &QProcess::finished, this, [this, worker](int code, QProcess::ExitStatus status) {
            if (worker->elapsed.isValid()) { preparationMs_ += worker->elapsed.elapsed(); worker->elapsed.invalidate(); }
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
    QTimer::singleShot(0, this, &PreviewCache::pruneStorage);
}

PreviewCache::~PreviewCache() {
    for (auto *worker : workers_) {
        worker->process->disconnect(this);
        worker->probeTimer->stop();
        if (worker->process->state() != QProcess::NotRunning) {
            worker->process->kill();
            worker->process->waitForFinished(1000);
        }
        if (!worker->job.partialPath.isEmpty()) {
            QFile::remove(worker->job.partialPath);
            QFile::remove(recordPath(worker->job.partialPath));
        }
        delete worker;
    }
}

void PreviewCache::setStorageLimit(qint64 bytes) {
    storageLimit_ = std::max(qint64(0), bytes);
    if (persistentSettings_) QSettings().setValue("preview/storageLimitBytes", storageLimit_);
    pruneStorage();
}

bool PreviewCache::setCacheDirectory(const QString &directory, QString *error) {
    const QString path = QDir(directory.trimmed()).absolutePath();
    if (directory.trimmed().isEmpty() || !QDir().mkpath(path) || !QFileInfo(path).isDir()) {
        if (error) *error = "Choose a writable local folder for preview copies.";
        return false;
    }
    QTemporaryFile check(QDir(path).filePath(".isaac-write-check-XXXXXX"));
    if (!check.open()) {
        if (error) *error = "The preview cache folder is not writable.";
        return false;
    }
    if (path == directory_) return true;
    const auto requests = requests_;
    for (auto *worker : workers_) cancel(worker, false);
    queue_.clear(); pending_.clear(); sources_.clear(); wanted_.clear(); notified_.clear(); failed_.clear();
    directory_ = path;
    if (persistentSettings_) QSettings().setValue("preview/cacheDirectory", directory_);
    setRequests(requests);
    return true;
}

void PreviewCache::setQualityLimit(const QSize &size) {
    const QSize bounded(std::clamp(size.width(), 2, 3840), std::clamp(size.height(), 2, 2160));
    if (bounded == qualityLimit()) return;
    budget_.previewWidth = bounded.width(); budget_.previewHeight = bounded.height();
    if (persistentSettings_) {
        QSettings settings;
        settings.setValue("preview/width", bounded.width()); settings.setValue("preview/height", bounded.height());
    }
    const auto requests = requests_;
    for (auto *worker : workers_) cancel(worker, false);
    queue_.clear(); pending_.clear(); sources_.clear(); wanted_.clear(); notified_.clear(); failed_.clear();
    setRequests(requests);
}

bool PreviewCache::protect(const Job &job) {
    if (leases_.contains(job.outputPath)) return true;
    if (const auto existing = knownLeases_.value(job.outputPath).lock()) {
        leases_.insert(job.outputPath, existing);
        return true;
    }
    storageError_.clear();
    const auto guard = directoryLock(QFileInfo(job.outputPath).absolutePath(), &storageError_);
    if (!guard) return false;
    const QString lease = QFileInfo(job.outputPath).absolutePath() + '/' + job.key + ".use-" + instanceId_ + ".lock";
    const auto lock = lockFile(lease, &storageError_);
    if (!lock) return false;
    leases_.insert(job.outputPath, lock);
    knownLeases_.insert(job.outputPath, lock);
    return true;
}

void PreviewCache::releaseUnusedLeases() {
    QSet<QString> keep;
    for (const auto &job : sources_) if (wanted_.contains(job.key)) keep.insert(job.outputPath);
    for (const auto *worker : workers_) if (!worker->job.key.isEmpty()) keep.insert(worker->job.outputPath);
    for (auto it = leases_.begin(); it != leases_.end();) {
        if (!keep.contains(it.key())) it = leases_.erase(it);
        else ++it;
    }
    for (auto it = knownLeases_.begin(); it != knownLeases_.end();) {
        if (it.value().expired()) it = knownLeases_.erase(it);
        else ++it;
    }
}

void PreviewCache::touchArtifact(const QString &path) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastTouched_.contains(path) && now - lastTouched_.value(path) < 30000) return;
    if (QFileInfo(recordPath(path)).isSymLink()) return;
    QFile record(recordPath(path));
    if (record.open(QIODevice::ReadOnly) && record.size() <= 4096) {
        const auto json = QJsonDocument::fromJson(record.readAll()).object();
        if (json.value("owner").toString() != Owner || json.value("version").toInt() != 1 ||
            json.value("file").toString() != QFileInfo(path).fileName()) return;
        record.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
        lastTouched_.insert(path, now);
    }
}

bool PreviewCache::loadTiming(const QString &source) {
    const QString key = SourceTiming::fingerprint(source);
    if (key.isEmpty()) return false;
    if (cadencePeriods_.contains(key)) return true;
    QFile file(QDir(directory_).filePath(key + ".timing.json"));
    if (QFileInfo(file).isSymLink()) return false;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) return false;
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    if (json.value("owner").toString() != Owner || json.value("version").toInt() != 1 ||
        json.value("fingerprint").toString() != key || !json.value("period").isDouble()) return false;
    const int period = json.value("period").toInt(-1);
    if (period < 0 || period > 3600) return false;
    cadencePeriods_.insert(key, period);
    ++timingHits_;
    touchArtifact(file.fileName());
    return true;
}

void PreviewCache::saveTiming(const QString &source, int period) {
    const QString key = SourceTiming::fingerprint(source);
    if (key.isEmpty()) return;
    const QString path = QDir(directory_).filePath(key + ".timing.json");
    const auto guard = directoryLock(directory_);
    if (!guard) return;
    if (QFileInfo(path).isSymLink()) return;
    if (QFileInfo::exists(path)) {
        QFile existing(path);
        if (!existing.open(QIODevice::ReadOnly) || existing.size() > 4096) return;
        const auto json = QJsonDocument::fromJson(existing.readAll()).object();
        if (json.value("owner").toString() != Owner || json.value("version").toInt() != 1 || json.value("fingerprint").toString() != key) return;
    }
    if (recordArtifact(path, key, "timing"))
        writeJson(path, {{"owner", Owner}, {"version", 1}, {"fingerprint", key}, {"period", period}});
}

void PreviewCache::pruneStorage() {
    const auto guard = directoryLock(directory_);
    if (!guard) return;
    struct Entry { QString path, record, key; qint64 bytes, used; bool active; };
    QVector<Entry> entries;
    storageBytes_ = protectedBytes_ = 0;
    const QDir directory(directory_);
    for (const auto &name : directory.entryList({QString("*") + RecordSuffix}, QDir::Files | QDir::NoSymLinks)) {
        QFile file(directory.filePath(name));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) continue;
        const auto json = QJsonDocument::fromJson(file.readAll()).object();
        const QString key = json.value("key").toString(), artifact = json.value("file").toString();
        const auto kind = json.value("kind").toString();
        const bool recognized = (kind == "preview" && artifact == key + ".mov") ||
            (kind == "timing" && artifact == key + ".timing.json") ||
            (kind == "partial" && artifact.startsWith(key + '-') && artifact.endsWith(".part.mov"));
        if (json.value("owner").toString() != Owner || json.value("version").toInt() != 1 || !validKey(key) ||
            !recognized || QFileInfo(artifact).fileName() != artifact || name != artifact + RecordSuffix) continue;
        const QFileInfo info(directory.filePath(artifact));
        if (info.isSymLink() || (info.exists() && !info.isFile())) continue;
        const qint64 bytes = info.size() + file.size();
        const bool active = protectedArtifact(directory_, key);
        entries.append({info.absoluteFilePath(), file.fileName(), key, bytes, QFileInfo(file).lastModified().toMSecsSinceEpoch(), active});
        storageBytes_ += bytes;
        if (active) protectedBytes_ += bytes;
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.used < b.used; });
    for (const auto &entry : entries) {
        if (storageBytes_ <= storageLimit_) break;
        if (entry.active) continue;
        if ((!QFile::exists(entry.path) || QFile::remove(entry.path)) && QFile::remove(entry.record)) {
            storageBytes_ -= entry.bytes;
            evictedBytes_ += entry.bytes;
        }
    }
}

QVariantMap PreviewCache::diagnostics() const {
    int active = 0;
    for (const auto &lease : knownLeases_) if (!lease.expired()) ++active;
    return {{"preparationMs", preparationMs_}, {"processes", QVariant::fromValue(processCount_)}, {"cacheHits", QVariant::fromValue(cacheHits_)}, {"cacheMisses", QVariant::fromValue(cacheMisses_)},
            {"timingHits", QVariant::fromValue(timingHits_)}, {"timingScans", QVariant::fromValue(timingScans_)},
            {"storageBytes", storageBytes_}, {"protectedBytes", protectedBytes_}, {"storageLimitBytes", storageLimit_}, {"evictedBytes", evictedBytes_},
            {"activeCopies", active}, {"queuedJobs", queue_.size()}};
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
        + '\0' + QByteArray::number(std::clamp(budget_.previewWidth, 2, 3840)) + 'x'
        + QByteArray::number(std::clamp(budget_.previewHeight, 2, 2160))
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
    return !failed_.contains(job.key) && !job.key.isEmpty() && jobKey(job) == job.key &&
        leases_.contains(job.outputPath) && usablePreview(job.outputPath);
}

PreviewCache::Copy PreviewCache::readyCopy(const QString &source, int inFrame, int outFrame) const {
    if (inFrame < 0 || outFrame <= inFrame) return {};
    const QString identity = sourceKey(source);
    for (const auto &job : sources_)
        if (sourceKey(job.source) == identity && job.start <= inFrame && job.end >= outFrame && usable(job))
            return {job.outputPath, job.start, leases_.value(job.outputPath)};
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
    if ((!failed_.contains(job.key) && usablePreview(job.outputPath)) || (request.inFrame == 0 && request.outFrame == job.frames)) return job;

    loadTiming(job.source);
    const QString timingKey = SourceTiming::fingerprint(job.source);
    job.needsProbe = !cadencePeriods_.contains(timingKey);
    const int period = cadencePeriods_.value(timingKey);
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
    protect(job);
}

void PreviewCache::request(const QString &source, int sourceFrames) {
    if (std::none_of(requests_.cbegin(), requests_.cend(), [&](const Request &request) {
            return request.source == source && request.sourceFrames == sourceFrames && request.inFrame == 0 && request.outFrame == sourceFrames;
        })) requests_.append({source, sourceFrames, 0, sourceFrames});
    Job job = makeJob({source, sourceFrames, 0, sourceFrames});
    if (job.key.isEmpty()) {
        job.key = sourceKey(source) + ':' + QString::number(sourceFrames) + ":invalid";
        reportFailure(job, "The source video is missing or has no usable duration.");
        return;
    }
    registerJob(job);
    wanted_.insert(job.key);
    if (failed_.contains(job.key)) return;
    if (usable(job)) { ++cacheHits_; touchArtifact(job.outputPath); notifyReady(job); return; }
    ++cacheMisses_;
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
        wanted_.remove(job.key);
        leases_.remove(job.outputPath);
        const auto guard = directoryLock(QFileInfo(job.outputPath).absolutePath());
        if (!guard) continue;
        if (QFileInfo(recordPath(job.outputPath)).isSymLink()) continue;
        QFile record(recordPath(job.outputPath));
        if (!record.open(QIODevice::ReadOnly) || record.size() > 4096) continue;
        auto json = QJsonDocument::fromJson(record.readAll()).object();
        record.close();
        if (json.value("owner").toString() != Owner || json.value("file").toString() != QFileInfo(job.outputPath).fileName()) continue;
        // Retire a corrupt key immediately, but keep another process's open working copy until it releases its lease.
        json.insert("invalid", true);
        writeJson(record.fileName(), json);
        if (!protectedArtifact(QFileInfo(job.outputPath).absolutePath(), job.key)) {
            QFile::remove(job.outputPath);
            QFile::remove(record.fileName());
        }
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
        if (usable(job)) { ++cacheHits_; touchArtifact(job.outputPath); notifyReady(job); continue; }
        ++cacheMisses_;
        if (pending_.contains(job.key)) continue;
        notified_.remove(job.key);
        pending_.insert(job.key);
        queue_.enqueue(job);
    }
    releaseUnusedLeases();
    pruneStorage();
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
    const int period = success ? SourceTiming::verifiedCadencePeriod(worker->probeBuffer) : 0;
    cadencePeriods_.insert(SourceTiming::fingerprint(worker->job.source), period);
    saveTiming(worker->job.source, period);
    worker->writeLock.reset();
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
    releaseUnusedLeases();
    pruneStorage();
    QTimer::singleShot(0, this, &PreviewCache::startNext);
}

void PreviewCache::notifyReady(const Job &job) {
    touchArtifact(job.outputPath);
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
    bool retry = false;
    for (auto *worker : workers_) {
        if (!worker->job.key.isEmpty() || worker->process->state() != QProcess::NotRunning || queue_.isEmpty()) continue;
        worker->job = queue_.dequeue();
        worker->cancelled = worker->probing = worker->hardware = false;
        worker->errors.clear();
        if (!isFresh(worker)) { complete(worker, false); continue; }
        if (!protect(worker->job)) {
            if (!storageError_.isEmpty()) { complete(worker, false, storageError_); continue; }
            queue_.enqueue(worker->job); worker->job = {}; retry = true; continue;
        }
        if (usable(worker->job)) { ++cacheHits_; complete(worker, true); continue; }
        if (worker->job.needsProbe && loadTiming(worker->job.source)) {
            worker->job = {}; setRequests(requests_); return;
        }
        const QString lockKey = worker->job.needsProbe ? SourceTiming::fingerprint(worker->job.source) : worker->job.key;
        storageError_.clear();
        worker->writeLock = lockFile(QDir(directory_).filePath(lockKey + ".write.lock"), &storageError_);
        if (!worker->writeLock) {
            if (!storageError_.isEmpty()) { complete(worker, false, storageError_); continue; }
            queue_.enqueue(worker->job); worker->job = {}; retry = true; continue;
        }
        waitingForCache_ = false;
        if (invalidArtifact(worker->job.outputPath)) {
            const auto guard = directoryLock(directory_);
            const auto lease = leases_.constFind(worker->job.outputPath);
            const bool decoderPinned = lease != leases_.cend() && lease.value().use_count() > 1;
            if (!guard || decoderPinned || protectedArtifact(directory_, worker->job.key, worker->job.key + ".use-" + instanceId_ + ".lock") ||
                (QFileInfo::exists(worker->job.outputPath) && !QFile::remove(worker->job.outputPath))) {
                worker->writeLock.reset(); queue_.enqueue(worker->job); worker->job = {}; retry = true; continue;
            }
            QFile::remove(recordPath(worker->job.outputPath));
        }
        if (worker->job.needsProbe) {
            const QString probe = executablePath("ffprobe");
            if (probe.isEmpty()) { finishProbe(worker, false); continue; }
            worker->probing = true;
            worker->probeBuffer.clear();
            ++timingScans_;
            emit statusChanged("Checking source timing…");
            worker->probeTimer->start(10000);
            worker->process->start(probe, {"-v", "error", "-select_streams", "v:0", "-show_entries",
                "stream=r_frame_rate,avg_frame_rate,time_base:packet=pts", "-of", "json", worker->job.source});
        } else startEncoding(worker);
    }
    if (retry) {
        if (!waitingForCache_) { waitingForCache_ = true; emit statusChanged("Waiting for a shared preview cache copy…"); }
        QTimer::singleShot(100, this, &PreviewCache::startNext);
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
    if (!recordArtifact(worker->job.partialPath, worker->job.key, "partial")) {
        complete(worker, false, "Could not record ownership of the preview copy."); return;
    }
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
        .arg(std::clamp(budget_.previewWidth, 2, 3840)).arg(std::clamp(budget_.previewHeight, 2, 2160));
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
        QFile::remove(recordPath(worker->job.partialPath));
        worker->job.partialPath.clear();
        startEncoding(worker, true);
        return;
    }
    const Job job = worker->job;
    success = success && fresh;
    if (success && !usablePreview(job.outputPath)) {
        const auto guard = directoryLock(QFileInfo(job.outputPath).absolutePath());
        // Publish ownership before the atomic rename, so readers never observe an unrecorded new copy.
        success = guard && !QFileInfo::exists(job.outputPath) && usableFile(job.partialPath) &&
            recordArtifact(job.outputPath, job.key, "preview") && QFile::rename(job.partialPath, job.outputPath);
    }
    if (!job.partialPath.isEmpty()) {
        QFile::remove(job.partialPath);
        QFile::remove(recordPath(job.partialPath));
    }
    worker->writeLock.reset();
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
    releaseUnusedLeases();
    pruneStorage();
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
