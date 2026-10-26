#pragma once

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
    explicit PreviewCache(const QString &cacheDirectory = QString(), QObject *parent = nullptr);
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
    };
    QString sourceKey(const QString &source) const;
    QString fingerprint(const QString &source, int frames) const;
    QString jobKey(const Job &job) const;
    Job makeJob(const Request &request);
    bool usable(const Job &job) const;
    void cancelCurrent(bool requeue);
    void finishProbe(bool success);
    void notifyReady(const Job &job);
    void startNext();
    void complete(bool success, const QString &error = QString());
    void readProgress();
    void reportFailure(const Job &job, const QString &error);

    QString directory_;
    QString cancelledPartial_;
    QProcess *process_ = nullptr;
    QTimer *probeTimer_ = nullptr;
    QHash<QString, Job> sources_;
    QHash<QString, int> cadencePeriods_;
    QQueue<Job> queue_;
    QSet<QString> pending_, notified_, failed_;
    Job current_;
    QByteArray errors_, progressBuffer_;
    QByteArray probeBuffer_;
    QVector<Request> requests_;
    int lastPercent_ = -1;
    bool paused_ = false;
    bool probing_ = false;
};
