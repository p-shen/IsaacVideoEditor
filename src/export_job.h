#pragma once
#include "project.h"
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

QString exportWorkerExecutable();
int runExportWorker(const QStringList &arguments);

class ExportJob final : public QObject {
    Q_OBJECT
public:
    explicit ExportJob(const QString &program, QObject *parent=nullptr);
    ~ExportJob() override;
    bool start(const Project &snapshot, const QString &destination, QString *error=nullptr);
    void cancel();
    void stopAndWait();
    QString logPath() const { return log_.fileName(); }
signals:
    void progressChanged(int percent);
    void diagnostic(const QString &message);
    void finished(bool success, bool canceled, const QString &error);
private:
    void launch(bool software);
    void readOutput();
    void readErrors();
    void workerFinished(int code, QProcess::ExitStatus status);
    void complete(bool success, const QString &error={});
    void log(const QByteArray &message);
    QString program_, destination_, workerError_;
    std::unique_ptr<QTemporaryDir> staging_;
    QProcess process_;
    QTimer cancelDeadline_;
    QFile log_;
    QByteArray output_;
    bool running_=false, canceled_=false, hardware_=false, retried_=false;
    bool resultReceived_=false, resultSuccess_=false, protocolFailed_=false;
};
