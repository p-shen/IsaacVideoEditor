#include "recovery_store.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>

RecoveryStore::RecoveryStore(const QString &directory,QObject *parent):QObject(parent),directory_(directory) {
    snapshotPath_=QDir(directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+".recovery.json");
    acquireLock();
    worker_.setMaxThreadCount(1);
    idle_.setSingleShot(true);idle_.setInterval(2000);
    maximum_.setSingleShot(true);maximum_.setInterval(30000);
    connect(&idle_,&QTimer::timeout,this,&RecoveryStore::startSave);
    connect(&maximum_,&QTimer::timeout,this,&RecoveryStore::startSave);
}
RecoveryStore::~RecoveryStore(){idle_.stop();maximum_.stop();worker_.waitForDone();}
bool RecoveryStore::acquireLock(){
    if(lock_)return true;
    if(!QDir().mkpath(directory_))return false;
    auto candidate=std::make_unique<QLockFile>(snapshotPath_+".lock");candidate->setStaleLockTime(0);
    if(!candidate->tryLock())return false;lock_=std::move(candidate);return true;
}
void RecoveryStore::setStatus(const QString &value){if(status_==value)return;status_=value;emit statusChanged();}
quint64 RecoveryStore::invalidate(){QMutexLocker guard(&publicationMutex_);return ++generation_;}
void RecoveryStore::schedule(const Project &project,const QString &path){
    ++revision_;
    pending_=project;projectPath_=path;idle_.start();if(!maximum_.isActive())maximum_.start();setStatus("Recovery pending");
}
QString RecoveryStore::writeSnapshot(const Project &project,const QString &path,quint64 generation){
    const auto problem=project.validate();if(!problem.isEmpty())return problem;
    if(!acquireLock())return "the recovery folder is not writable";
    auto object=projectJson(project);
    object.insert("_recovery",QJsonObject{{"projectPath",path},{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
    QSaveFile output(snapshotPath_);const auto bytes=QJsonDocument(object).toJson();
    if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size())return output.errorString();
    // Clear and explicit saves invalidate staged bytes before their atomic publication.
    QMutexLocker guard(&publicationMutex_);
    if(generation!=generation_)return {};
    return output.commit()?QString{}:output.errorString();
}
void RecoveryStore::startSave(){
    if(active_||!pending_)return;
    idle_.stop();maximum_.stop();
    const auto project=*pending_;const auto path=projectPath_;const auto generation=generation_;const auto revision=revision_;const auto job=++job_;
    active_=true;setStatus("Saving recovery…");
    worker_.start([this,project,path,generation,revision,job]{
        const auto error=writeSnapshot(project,path,generation);
        QMetaObject::invokeMethod(this,[this,generation,revision,job,error]{
            if(!active_||job!=job_)return;
            active_=false;
            if(generation!=generation_||revision!=revision_){if(pending_)startSave();return;}
            if(!error.isEmpty()){setStatus("Recovery failed: "+error+". Choose Retry recovery.");return;}
            pending_.reset();setStatus("Recovery saved");
        },Qt::QueuedConnection);
    });
}
bool RecoveryStore::flush(){
    idle_.stop();maximum_.stop();const auto generation=invalidate();
    worker_.waitForDone();active_=false;++job_;
    if(!pending_)return true;
    const auto project=*pending_;const auto path=projectPath_;const auto revision=revision_;setStatus("Saving recovery…");
    const auto error=writeSnapshot(project,path,generation);
    if(generation!=generation_)return true;
    if(revision!=revision_){startSave();return error.isEmpty();}
    if(!error.isEmpty()){setStatus("Recovery failed: "+error+". Choose Retry recovery.");return false;}
    pending_.reset();setStatus("Recovery saved");return true;
}
void RecoveryStore::clear(){idle_.stop();maximum_.stop();invalidate();pending_.reset();if(QFile::exists(snapshotPath_)&&!QFile::remove(snapshotPath_)){setStatus("Recovery failed: could not remove an old snapshot.");return;}setStatus("Recovery ready");}
QVariantList RecoveryStore::candidates(const QString &directory){
    QVariantList result;
    for(const auto &file:QDir(directory).entryInfoList({"*.recovery.json"},QDir::Files,QDir::Time)){
        QLockFile lock(file.absoluteFilePath()+".lock");lock.setStaleLockTime(0);
        if(!lock.tryLock())continue;
        QFile input(file.absoluteFilePath());if(!input.open(QIODevice::ReadOnly))continue;
        const auto data=QJsonDocument::fromJson(input.readAll()).object();
        const auto meta=data["_recovery"].toObject();if(meta.isEmpty())continue;
        const auto project=meta["projectPath"].toString();
        result.append(QVariantMap{{"path",file.absoluteFilePath()},{"projectPath",project},
            {"name",project.isEmpty()?QString("Untitled"):QFileInfo(project).fileName()},{"time",meta["time"].toString()}});
    }
    return result;
}
