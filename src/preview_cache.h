#pragma once

#include "performance.h"

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QSize>
#include <QVariantMap>
#include <memory>

class QLockFile;
class QProcess;
class QTimer;

class PreviewCache final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 storageLimit READ storageLimit WRITE setStorageLimit)
public:
    struct Request {
        QString source;
        int sourceFrames = 0, inFrame = 0, outFrame = 0; // Exclusive out, on the 60 fps source timeline.
    };
    struct Copy {
        QString path;
        int startFrame = 0;
        // Hold this pin for as long as a decoder or queued preparation can reopen the copy.
        std::shared_ptr<void> lease;
    };
    explicit PreviewCache(const QString &cacheDirectory = QString(), QObject *parent = nullptr,
                          const Performance::Budget &budget = Performance::budget());
    ~PreviewCache() override;
    // sourceFrames expresses the original duration on the editor's 60 fps timeline.
    void request(const QString &source, int sourceFrames);
    void retainSources(const QStringList &sources);
    void discard(const QString &source);
    QString readyPath(const QString &source) const;
    bool isBusy() const;
    void setRequests(const QVector<Request> &requests);
    Copy readyCopy(const QString &source, int inFrame, int outFrame) const;
    void prioritize(const QString &source, int inFrame, int outFrame);
    void setPaused(bool paused);
    QString cacheDirectory() const { return directory_; }
    qint64 storageLimit() const { return storageLimit_; }
    void setStorageLimit(qint64 bytes);
    bool setCacheDirectory(const QString &directory, QString *error = nullptr);
    QSize qualityLimit() const { return {budget_.previewWidth, budget_.previewHeight}; }
    void setQualityLimit(const QSize &size);
    Q_INVOKABLE void pruneStorage();
    QVariantMap diagnostics() const;

signals:
    void ready(const QString &source, const QString &path);
    void statusChanged(const QString &message);
    void failed(const QString &source, const QString &message);

private:
    struct Job {
        QString source, key, outputPath, partialPath;
        int frames = 0, start = 0, end = 0;
        bool needsProbe = false;
        quint64 generation = 0;
    };
    struct Worker {
        QProcess *process = nullptr;
        QElapsedTimer elapsed;
        QTimer *probeTimer = nullptr;
        Job job;
        QByteArray errors, progressBuffer, probeBuffer;
        int lastPercent = -1;
        bool probing = false, cancelled = false, hardware = false;
        std::shared_ptr<QLockFile> writeLock;
    };
    QString sourceKey(const QString &source) const;
    QString fingerprint(const QString &source, int frames) const;
    QString jobKey(const Job &job) const;
    Job makeJob(const Request &request);
    bool usable(const Job &job) const;
    void cancel(Worker *worker, bool requeue);
    void finishProbe(Worker *worker, bool success);
    void startEncoding(Worker *worker, bool software = false);
    bool isFresh(const Worker *worker) const;
    void registerJob(Job &job);
    void notifyReady(const Job &job);
    void startNext();
    void complete(Worker *worker, bool success, const QString &error = QString());
    void readProgress(Worker *worker);
    void reportFailure(const Job &job, const QString &error);

    bool protect(const Job &job);
    void releaseUnusedLeases();
    bool loadTiming(const QString &source);
    void saveTiming(const QString &source, int period);
    void touchArtifact(const QString &path);

    QString directory_, instanceId_, storageError_;
    bool waitingForCache_ = false;
    bool persistentSettings_ = false;
    qint64 storageLimit_ = 10LL * 1024 * 1024 * 1024;
    qint64 storageBytes_ = 0, evictedBytes_ = 0, protectedBytes_ = 0, preparationMs_ = 0;
    quint64 processCount_ = 0;
    quint64 cacheHits_ = 0, cacheMisses_ = 0, timingHits_ = 0, timingScans_ = 0;
    QHash<QString, std::shared_ptr<QLockFile>> leases_;
    QHash<QString, std::weak_ptr<QLockFile>> knownLeases_;
    QHash<QString, qint64> lastTouched_;
    Performance::Budget budget_;
    QVector<Worker *> workers_;
    quint64 nextGeneration_ = 0;
    bool hardwareFailed_ = false;
    QHash<QString, Job> sources_;
    QHash<QString, int> cadencePeriods_;
    QQueue<Job> queue_;
    QSet<QString> pending_, notified_, failed_, wanted_;
    QVector<Request> requests_;
    bool paused_ = false;
};
