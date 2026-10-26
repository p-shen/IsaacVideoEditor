#pragma once
#include "project.h"
#include <QObject>
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
    bool flush();
    void clear();
    QString snapshotPath() const { return snapshotPath_; }
    QString status() const { return status_; }
    static QVariantList candidates(const QString &directory);
signals:
    void statusChanged();
private:
    bool acquireLock();
    void setStatus(const QString &);
    QString directory_,snapshotPath_,projectPath_,status_="Recovery ready";
    std::unique_ptr<QLockFile> lock_;
    std::optional<Project> pending_;
    QTimer idle_,maximum_;
};
