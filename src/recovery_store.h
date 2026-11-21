#pragma once
#include "project.h"
#include <QObject>
#include <QMutex>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <memory>
class QLockFile;

// Recovery is a separate, process-owned journal; it never writes the saved project.
class RecoveryStore final : public QObject {
    Q_OBJECT
public:
    explicit RecoveryStore(const QString &directory,QObject *parent=nullptr);
    ~RecoveryStore() override;
    void schedule(const Project &,const QString &projectPath);
    // Explicit retry/recovery callers synchronously drain and save the latest revision.
    bool flush();
    void clear();
    QString snapshotPath() const { return snapshotPath_; }
    QString status() const { return status_; }
    static QVariantList candidates(const QString &directory);
signals:
    void statusChanged();
private:
    bool acquireLock();
    void startSave();
    QString writeSnapshot(const Project &,const QString &projectPath,quint64 generation);
    quint64 invalidate();
    void setStatus(const QString &);
    QString directory_,snapshotPath_,projectPath_,status_="Recovery ready";
    std::unique_ptr<QLockFile> lock_;
    std::optional<Project> pending_;
    QTimer idle_,maximum_;
    QThreadPool worker_;
    QMutex publicationMutex_;
    quint64 generation_=0,revision_=0,job_=0;
    bool active_=false;
};
