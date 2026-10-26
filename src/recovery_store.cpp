#include "recovery_store.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>

RecoveryStore::RecoveryStore(const QString &directory,QObject *parent):QObject(parent),directory_(directory) {
    snapshotPath_=QDir(directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+".recovery.json");
    acquireLock();
    idle_.setSingleShot(true);idle_.setInterval(2000);
    maximum_.setSingleShot(true);maximum_.setInterval(30000);
    connect(&idle_,&QTimer::timeout,this,[this]{flush();});
    connect(&maximum_,&QTimer::timeout,this,[this]{flush();});
}
RecoveryStore::~RecoveryStore()=default;
bool RecoveryStore::acquireLock(){
    if(lock_)return true;
    if(!QDir().mkpath(directory_))return false;
    auto candidate=std::make_unique<QLockFile>(snapshotPath_+".lock");candidate->setStaleLockTime(0);
    if(!candidate->tryLock())return false;lock_=std::move(candidate);return true;
}
void RecoveryStore::setStatus(const QString &value){if(status_==value)return;status_=value;emit statusChanged();}
void RecoveryStore::schedule(const Project &project,const QString &path){
    pending_=project;projectPath_=path;idle_.start();if(!maximum_.isActive())maximum_.start();setStatus("Recovery pending");
}
bool RecoveryStore::flush(){
    if(!pending_)return true;
    idle_.stop();maximum_.stop();setStatus("Saving recovery…");
    auto fail=[this](const QString &error){setStatus("Recovery failed: "+error+". Choose Retry recovery.");return false;};
    if(!acquireLock())return fail("the recovery folder is not writable");
    QTemporaryFile temporary(QDir(directory_).filePath(".snapshot-XXXXXX"));
    if(!temporary.open())return fail(temporary.errorString());
    const auto temp=temporary.fileName();temporary.close();QString error;
    if(!saveProject(*pending_,temp,&error))return fail(error);
    QFile input(temp);if(!input.open(QIODevice::ReadOnly))return fail(input.errorString());
    auto object=QJsonDocument::fromJson(input.readAll()).object();
    object.insert("_recovery",QJsonObject{{"projectPath",projectPath_},{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
    QSaveFile output(snapshotPath_);const auto bytes=QJsonDocument(object).toJson();
    if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit())return fail(output.errorString());
    pending_.reset();setStatus("Recovery saved");return true;
}
void RecoveryStore::clear(){idle_.stop();maximum_.stop();pending_.reset();if(QFile::exists(snapshotPath_)&&!QFile::remove(snapshotPath_)){setStatus("Recovery failed: could not remove an old snapshot.");return;}setStatus("Recovery ready");}
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
