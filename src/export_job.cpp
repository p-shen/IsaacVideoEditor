#include "export_job.h"
#include "mlt_engine.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUuid>
#include <cstdio>
#include <filesystem>

namespace {
constexpr qint64 maxLogBytes=512*1024;
QString normalized(const QString &path){QFileInfo f(path);return f.exists()?f.canonicalFilePath():f.absoluteFilePath();}
void sendEvent(const QJsonObject &event){const auto data=QJsonDocument(event).toJson(QJsonDocument::Compact)+'\n';fwrite(data.constData(),1,data.size(),stdout);fflush(stdout);}
}
QString exportWorkerExecutable(){
    const auto current=QCoreApplication::applicationFilePath();
    if(QFileInfo(current).completeBaseName()=="IsaacVideoEditor")return current;
#ifdef Q_OS_MACOS
    return QDir(QCoreApplication::applicationDirPath()).filePath("IsaacVideoEditor.app/Contents/MacOS/IsaacVideoEditor");
#elif defined(Q_OS_WIN)
    return QDir(QCoreApplication::applicationDirPath()).filePath("IsaacVideoEditor.exe");
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath("IsaacVideoEditor");
#endif
}
int runExportWorker(const QStringList &arguments){
    if(arguments.size()<5||arguments[1]!="--export-worker")return 2;
    QString error;auto project=loadProject(arguments[2],&error);
    if(!project){sendEvent({{"event","result"},{"ok",false},{"error",error}});return 1;}
    int last=-1;
    const bool ok=Media::exportMp4(*project,arguments[3],[&](int value){if(value==last)return;last=value;sendEvent({{"event","progress"},{"value",value}});},&error,
        [&]{return QFileInfo::exists(arguments[4]);},
        [](const QString &message){sendEvent({{"event","diagnostic"},{"message",message}});},arguments.contains("--software"));
    sendEvent({{"event","result"},{"ok",ok},{"error",error}});
    return ok?0:1;
}
ExportJob::ExportJob(const QString &program,QObject *parent):QObject(parent),program_(program){
    process_.setParent(this);process_.setObjectName("exportWorker");
    cancelDeadline_.setSingleShot(true);cancelDeadline_.setInterval(2000);
    connect(&cancelDeadline_,&QTimer::timeout,this,[this]{if(process_.state()!=QProcess::NotRunning)process_.kill();});
    connect(&process_,&QProcess::readyReadStandardOutput,this,&ExportJob::readOutput);
    connect(&process_,&QProcess::readyReadStandardError,this,&ExportJob::readErrors);
    connect(&process_,&QProcess::finished,this,&ExportJob::workerFinished);
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart)complete(false,"Could not start export worker: "+process_.errorString());
    });
}
ExportJob::~ExportJob(){disconnect(&process_,nullptr,this,nullptr);stopAndWait();}
void ExportJob::log(const QByteArray &message){
    if(!log_.isOpen()||log_.size()>=maxLogBytes)return;
    log_.write(message.left(maxLogBytes-log_.size()));log_.flush();
}
bool ExportJob::start(const Project &snapshot,const QString &destination,QString *error){
    const auto fail=[&](const QString &message){if(error)*error=message;return false;};
    if(running_||destination.isEmpty())return fail("Choose an output file.");
    const auto problem=snapshot.validate();if(!problem.isEmpty())return fail(problem);
    if(snapshot.clips.isEmpty())return fail("Add at least one video clip.");
    const auto target=normalized(destination);QStringList sources={snapshot.voicePath,snapshot.musicPath};
    for(const auto &c:snapshot.clips)sources.append(c.path);for(const auto &a:snapshot.assets)sources.append(a.path);for(const auto &o:snapshot.visualOverlays)sources.append(o.path);
    for(const auto &source:sources)if(!source.isEmpty()&&target.compare(normalized(source),Qt::CaseInsensitive)==0)return fail("Choose a different file from your source media.");
    destination_=QFileInfo(destination).absoluteFilePath();
    staging_=std::make_unique<QTemporaryDir>(QFileInfo(destination_).absoluteDir().filePath(".isaac-export-XXXXXX"));
    if(!staging_->isValid()){staging_.reset();return fail("Cannot write the output folder.");}
    if(!saveProject(snapshot,staging_->filePath("snapshot.isaac.json"),error)){staging_.reset();return false;}
    const auto directory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/exports";
    log_.setFileName(QDir(directory).filePath("export-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".log"));
    if(!QDir().mkpath(directory)||!log_.open(QIODevice::WriteOnly|QIODevice::NewOnly)){
        QTemporaryFile fallback(QDir::tempPath()+"/isaac-export-XXXXXX.log");
        if(fallback.open()){fallback.setAutoRemove(false);log_.setFileName(fallback.fileName());fallback.close();log_.open(QIODevice::WriteOnly);}
    }
    log((QDateTime::currentDateTimeUtc().toString(Qt::ISODate)+"\nExporting "+QString::number(snapshot.durationFrames())+" frames to "+destination_+"\n").toUtf8());
    running_=true;canceled_=hardware_=retried_=false;
    launch(false);return true;
}
void ExportJob::launch(bool software){
    resultReceived_=resultSuccess_=protocolFailed_=hardware_=false;workerError_.clear();output_.clear();
    QStringList args={"--export-worker",staging_->filePath("snapshot.isaac.json"),staging_->filePath("render.mp4"),staging_->filePath("cancel")};
    if(software)args.append("--software");
    auto environment=QProcessEnvironment::systemEnvironment();environment.insert("QT_QPA_PLATFORM","offscreen");
    process_.setProcessEnvironment(environment);process_.setWorkingDirectory(QDir::currentPath());process_.start(program_,args);
}
void ExportJob::readErrors(){log(process_.readAllStandardError());}
void ExportJob::readOutput(){
    output_+=process_.readAllStandardOutput();
    while(output_.contains('\n')){
        const auto end=output_.indexOf('\n');const auto line=output_.left(end);output_.remove(0,end+1);log(line+'\n');
        if(line.size()>65536||resultReceived_){protocolFailed_=true;continue;}
        const auto doc=QJsonDocument::fromJson(line);const auto event=doc.object();const auto kind=event["event"].toString();
        if(!doc.isObject()){protocolFailed_=true;continue;}
        if(kind=="progress"&&event["value"].isDouble()){
            const double value=event["value"].toDouble();if(value<0||value>100||int(value)!=value){protocolFailed_=true;continue;}
            emit progressChanged(std::min(99,int(value)));
        }else if(kind=="diagnostic"&&event["message"].isString()){
            const auto message=event["message"].toString();
            if(message.startsWith("Encoder: "))hardware_=message.contains("h264_videotoolbox");
            emit diagnostic(message);
        }else if(kind=="result"&&event["ok"].isBool()){
            resultReceived_=true;resultSuccess_=event["ok"].toBool();workerError_=event["error"].toString();
        }else protocolFailed_=true;
    }
    if(output_.size()>65536){protocolFailed_=true;output_.clear();}
}
void ExportJob::workerFinished(int code,QProcess::ExitStatus status){
    readOutput();readErrors();if(!running_)return;
    if(!output_.trimmed().isEmpty())protocolFailed_=true;
    if(!canceled_&&status==QProcess::CrashExit&&hardware_&&!retried_&&!protocolFailed_){
        retried_=true;log("Hardware worker crashed. Retrying with software.\n");emit diagnostic("Retrying with software after an encoder crash");emit progressChanged(0);launch(true);return;
    }
    if(canceled_){complete(false,"Export canceled.");return;}
    const auto staged=staging_->filePath("render.mp4");const QFileInfo file(staged);
    if(status!=QProcess::NormalExit||code!=0||protocolFailed_||!resultReceived_||!resultSuccess_||!file.isFile()||file.isSymLink()||file.size()==0){
        QString error=workerError_;
        if(status==QProcess::CrashExit)error="The export worker crashed. The editor and existing output were preserved.";
        else if(protocolFailed_||!resultReceived_)error="The export worker stopped without a valid result.";
        if(error.isEmpty())error="The export worker failed (exit "+QString::number(code)+").";
        complete(false,error);return;
    }
    std::error_code error;
    std::filesystem::rename(std::filesystem::path(staged.toStdU16String()),std::filesystem::path(destination_.toStdU16String()),error);
    complete(!error,error?"Could not save MP4: "+QString::fromLocal8Bit(error.message().c_str()):QString{});
}
void ExportJob::complete(bool success,const QString &error){
    if(!running_)return;
    running_=false;cancelDeadline_.stop();log((success?QString("Completed\n"):error+'\n').toUtf8());log_.close();staging_.reset();
    if(success)emit progressChanged(100);
    emit finished(success,!success&&canceled_,error);
}
void ExportJob::cancel(){
    if(!running_||canceled_)return;
    canceled_=true;if(staging_){QFile marker(staging_->filePath("cancel"));if(marker.open(QIODevice::WriteOnly))marker.close();}
    cancelDeadline_.start();
}
void ExportJob::stopAndWait(){
    cancel();
    if(process_.state()!=QProcess::NotRunning&&!process_.waitForFinished(1500)){process_.kill();process_.waitForFinished(1000);}
    if(running_)complete(false,"Export canceled.");
}
