#pragma once
#include "project.h"
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

namespace Subtitles {
std::optional<QByteArray> whisperSrt(const QByteArray &json,int durationFrames,QStringList *warnings=nullptr,QString *error=nullptr);
}
QString subtitleEngineExecutable(bool cpuOnly=false);
QString subtitleRuntimeDirectory();
int runSubtitleAudioWorker(const QStringList &arguments);

class SubtitleJob final : public QObject {
    Q_OBJECT
public:
    SubtitleJob(const QString &appWorker,const QString &whisper,QObject *parent=nullptr);
    ~SubtitleJob() override;
    bool start(const Project &snapshot,const QString &destination,const QString &modelPath,bool cpuOnly,QString *error=nullptr);
    void cancel();
    void stopAndWait();
    QString stage() const {return stage_;}
    int progress() const {return progress_;}
    QString device() const {return device_;}
    QStringList warnings() const {return warnings_;}
    QString logPath() const {return log_.fileName();}
    bool capturedSourcesUnchanged() const;
signals:
    void changed();
    void finished(bool success,bool canceled,const QString &error);
private:
    void readOutput();
    void readErrors();
    void processFinished(int code,QProcess::ExitStatus status);
    void complete(bool success,const QString &error={});
    void writeLog(const QByteArray &bytes);
    void transcribe();
    QString appWorker_,whisper_,destination_,model_,stage_,device_,workerError_;
    QHash<QString,QPair<qint64,qint64>> sources_;
    std::unique_ptr<QTemporaryDir> staging_;
    QProcess process_;
    QTimer cancelDeadline_;
    QFile log_;
    QByteArray output_,errors_;
    QStringList warnings_;
    int frames_=0,progress_=-1;
    bool running_=false,canceled_=false,cpuOnly_=false,audio_=true,resultReceived_=false,resultOk_=false,protocolFailed_=false,gpuConfirmed_=false;
};
