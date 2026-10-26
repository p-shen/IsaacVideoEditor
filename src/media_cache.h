#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QStringList>
#include <QVector>

class QProcess;

class MediaCache final : public QObject {
    Q_OBJECT
public:
    explicit MediaCache(QObject *parent = nullptr);
    ~MediaCache() override;
    QImage thumbnail(const QString &path, double sourceSeconds, int rotationDegrees = 0, bool priority = false);
    QVector<float> waveform(const QString &path);
    QString waveformState(const QString &path) const;
    void setInteractive(bool interactive);
    void retainSources(const QStringList &sources);
    void beginViewport();
    void endViewport();
    Q_INVOKABLE void retry(const QString &path);

signals:
    void changed();

private:
    enum Kind { Thumbnail, Waveform };
    struct Source {
        QString path, identity, waveformState;
        quint64 generation = 0;
    };
    struct Job {
        QString source, path, identity, key;
        quint64 generation = 0;
        qint64 frame = 0;
        int rotation = 0;
        Kind kind = Thumbnail;
        bool priority = false;
        bool cancelled = false;
    };
    static QString sourceKey(const QString &path);
    static QString fingerprint(const QString &path);
    Source &source(const QString &path);
    void discardSource(const QString &key);
    bool enqueue(const Job &job);
    void schedule();
    void startNext();
    void readOutput();
    void complete(bool success);
    bool currentIsFresh() const;

    QCache<QString, QImage> images_{16 * 1024 * 1024};
    QCache<QString, QVector<float>> waveforms_{8 * 1024 * 1024};
    QCache<QString, bool> failures_{4096};
    QHash<QString, Source> sources_;
    QQueue<Job> queue_;
    QSet<QString> pending_, viewportRequests_;
    QProcess *process_ = nullptr;
    Job current_;
    QByteArray imageBytes_;
    QVector<float> peaks_;
    quint64 nextGeneration_ = 0;
    int binSamples_ = 0;
    float binPeak_ = 0;
    int pcmLowByte_ = -1;
    bool interactive_ = false;
    bool viewport_ = false;
    bool scheduled_ = false;
    bool outputOverflow_ = false;
    bool ffmpegUnavailable_ = false;
};
