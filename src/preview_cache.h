#pragma once

#include "performance.h"

#include <QObject>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class QProcess;
class QTimer;

class PreviewCache final : public QObject {
    Q_OBJECT
public:
    struct Request {
        QString source;
        int sourceFrames = 0, inFrame = 0, outFrame = 0; // Exclusive out, on the 60 fps source timeline.
    };
    struct Copy {
        QString path;
        int startFrame = 0;
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
        QTimer *probeTimer = nullptr;
        Job job;
        QByteArray errors, progressBuffer, probeBuffer;
        int lastPercent = -1;
        bool probing = false, cancelled = false, hardware = false;
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

    QString directory_;
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
