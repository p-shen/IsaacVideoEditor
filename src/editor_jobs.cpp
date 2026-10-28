#include "editor_controller.h"
#include "export_job.h"
#include "recovery_store.h"
#include "media_cache.h"
#include "mlt_engine.h"
#include "preview.h"
#include "preview_cache.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

QString EditorController::recoveryStatus() const {return recovery_?recovery_->status():QString{};}
void EditorController::enableRecovery(const QString &directory){
    if(recovery_)return;
    const auto root=directory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/recovery":directory;
    recoveryCandidates_=RecoveryStore::candidates(root);
    recovery_=new RecoveryStore(root,this);connect(recovery_,&RecoveryStore::statusChanged,this,&EditorController::stateChanged);
    emit stateChanged();
}
void EditorController::scheduleRecovery(){
    if(!recovery_)return;
    if(textDragBefore_||audioPreview_||visualBefore_)return;
    if(!dirty()){recovery_->clear();return;}
    // All callers are command boundaries, never transient pointer/text previews.
    recovery_->schedule(project_,path_);
}
void EditorController::retryRecovery(){if(recovery_)recovery_->flush();}
bool EditorController::recoverSnapshot(const QString &path){
    QVariantMap offered;
    for(const auto &value:recoveryCandidates_)if(value.toMap()["path"].toString()==path)offered=value.toMap();
    if(offered.isEmpty()||!maybeSave())return false;
    QString error;auto recovered=loadProject(path,&error);if(!recovered){report(error);return false;}
    if(textDragBefore_)endTextDrag(true);
    showProject();selectedSource_=-1;sourceViewDirty_=false;durations_.clear();undo_.clear();path_=offered["projectPath"].toString();
    restoreAudioDurations(*recovered);apply({*recovered,{},-1},0);undo_.resetClean();scheduleRecovery();
    if(recovery_&&recovery_->flush())discardRecovery(path);
    report("Recovered as unsaved work. Your saved project was preserved.");return true;
}
void EditorController::discardRecovery(const QString &path){
    for(int i=0;i<recoveryCandidates_.size();++i)if(recoveryCandidates_[i].toMap()["path"].toString()==path){
        if(QFile::remove(path)||!QFile::exists(path)){recoveryCandidates_.removeAt(i);emit stateChanged();}
        else report("Could not remove the recovery snapshot.");
        return;
    }
}
QVariantList EditorController::missingMedia() const {
    QStringList sources;for(const auto &clip:project_.clips)sources.append(clip.path);
    for(const auto &asset:project_.assets)sources.append(asset.path);
    for(const auto &overlay:project_.visualOverlays)sources.append(overlay.path);
    sources.append(project_.voicePath);sources.append(project_.musicPath);sources.removeDuplicates();
    QVariantList result;for(const auto &path:sources)if(!path.isEmpty()&&!QFileInfo(path).isFile())
        result.append(QVariantMap{{"path",path},{"name",QFileInfo(path).fileName()}});
    return result;
}
bool EditorController::relinkMedia(const QString &missing,const QString &replacement){
    cancelPendingVisual();
    bool referenced=project_.voicePath==missing||project_.musicPath==missing;
    double required=0;bool needsVideo=false,needsMovie=false,needsAudio=referenced;
    if(project_.voicePath==missing)required=std::max(required,project_.voiceDurationFrames/60.0);
    if(project_.musicPath==missing)required=std::max(required,project_.musicDurationFrames/60.0);
    for(const auto &clip:project_.clips)if(clip.path==missing){referenced=true;needsVideo=needsMovie=true;required=std::max(required,clip.outSeconds);}
    for(const auto &overlay:project_.visualOverlays)if(overlay.path==missing){referenced=true;needsVideo=true;required=std::max(required,(overlay.sourceInFrame+overlay.durationFrames)/60.0);}
    for(const auto &asset:project_.assets)if(asset.path==missing){
        referenced=true;needsVideo|=asset.kind=="video";needsMovie|=asset.kind=="video";needsAudio|=asset.kind=="audio";
        required=std::max(required,std::max({asset.inFrame+1,asset.outFrame,asset.cursorFrame+1})/60.0);
    }
    if(missing.isEmpty()||!referenced)return false;
    QString error;QImageReader reader(replacement);const bool still=reader.canRead();std::optional<Media::MediaInfo> info;
    if(still){const auto image=reader.read();if(!image.isNull()){Media::MediaInfo imageInfo;imageInfo.durationSeconds=86400;imageInfo.width=image.width();imageInfo.height=image.height();info=imageInfo;}}
    else info=Media::mediaInfo(replacement,&error);
    if(!info||info->durationSeconds+0.5/60<required||(needsVideo&&(info->width<1||info->height<1))||(needsAudio&&!info->hasAudio)||(needsMovie&&still)){
        report(info?"The replacement must contain the required audio/video and cover every saved trim and source range.":error);return false;
    }
    auto next=project_;const auto absolute=QFileInfo(replacement).absoluteFilePath();
    for(const auto &old:next.assets)if(old.path==missing)for(const auto &existing:next.assets)
        if(existing.path==absolute&&existing.path!=missing&&(old.inFrame!=existing.inFrame||old.outFrame!=existing.outFrame||old.cursorFrame!=existing.cursorFrame)){
            report("That file is already in Media with a different marked range. Remove its Media entry first if you want to keep this source's range; existing sequence instances and files are preserved.");return false;
        }
    bool existingAsset=false;for(const auto &asset:next.assets)if(asset.path==absolute&&asset.path!=missing)existingAsset=true;
    for(int i=next.assets.size()-1;i>=0;--i)if(next.assets[i].path==missing){
        if(existingAsset){next.assets.removeAt(i);continue;}
        auto &asset=next.assets[i];asset.path=absolute;asset.durationFrames=std::max(1,qRound(info->durationSeconds*60));
    }
    for(auto &clip:next.clips)if(clip.path==missing)clip.path=absolute;
    for(auto &overlay:next.visualOverlays)if(overlay.path==missing)overlay.path=absolute;
    if(next.voicePath==missing){next.voicePath=absolute;next.voiceDurationFrames=qRound(info->durationSeconds*60);}
    if(next.musicPath==missing){next.musicPath=absolute;next.musicDurationFrames=qRound(info->durationSeconds*60);}
    durations_.remove(missing);durations_.remove(absolute);
    if(!commit(next,selection_,primary_,"Relink media"))return false;
    for(int i=relinkProposals_.size()-1;i>=0;--i)if(relinkProposals_[i].toMap()["missing"].toString()==missing)relinkProposals_.removeAt(i);
    emit stateChanged();return true;
}
void EditorController::locateMissing(const QString &path){
    const auto replacement=QFileDialog::getOpenFileName(nullptr,"Locate "+QFileInfo(path).fileName(),QDir::homePath());
    if(!replacement.isEmpty())relinkMedia(path,replacement);
}
void EditorController::chooseRelinkFolder(){
    const auto folder=QFileDialog::getExistingDirectory(nullptr,"Search folder for missing media",QDir::homePath());if(folder.isEmpty())return;
    searchRelinkFolder(folder);
}
void EditorController::searchRelinkFolder(const QString &folder){
    if(!QFileInfo(folder).isDir()){report("Choose a readable folder to search.");return;}
    QHash<QString,QStringList> names;QDirIterator files(folder,QDir::Files|QDir::Readable,QDirIterator::Subdirectories);int scanned=0;
    while(files.hasNext()&&scanned++<10000){const auto path=files.next();names[QFileInfo(path).fileName().toCaseFolded()].append(path);}
    relinkProposals_.clear();
    for(const auto &entry:missingMedia()){
        const auto missing=entry.toMap();const auto matches=names.value(missing["name"].toString().toCaseFolded());
        if(matches.isEmpty())continue;
        relinkProposals_.append(QVariantMap{{"missing",missing["path"]},{"candidates",matches},{"ambiguous",matches.size()>1}});
    }
    if(relinkProposals_.isEmpty())report("No matching filenames found in that folder. Use Locate file to choose a renamed source.");
    else report("Review suggested files before relinking. Choose the correct path when more than one matches.");
    emit stateChanged();
}
bool EditorController::requestClose(){
    if(exporting()){
        if(QMessageBox::question(nullptr,"Export is running","Cancel this export and close the editor? Existing output files will be preserved.",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return false;
        cancelExport();if(exportJob_)exportJob_->stopAndWait();
    }
    if(!maybeSave())return false;if(recovery_)recovery_->clear();return true;
}
bool EditorController::exportTo(const QString &path){
    if(visualBefore_)endVisualGesture(false);
    if(audioPreview_)endAudioGesture(false);
    if(textDragBefore_)endTextDrag(false);
    return startExport(project_,path,title()+(dirty()?" · unsaved revision":" · saved revision")+QString(" · %1 frames").arg(duration()));
}
bool EditorController::startExport(const Project &snapshot,const QString &path,const QString &revision){
    if(exporting()||snapshot.clips.isEmpty()||path.isEmpty())return false;
    const auto normalized=[](const QString &p){const QFileInfo f(p);return f.exists()?f.canonicalFilePath():f.absoluteFilePath();};
    const auto output=normalized(path);QStringList sources={snapshot.voicePath,snapshot.musicPath};for(const auto &c:snapshot.clips)sources.append(c.path);for(const auto &asset:snapshot.assets)sources.append(asset.path);
    for(const auto &overlay:snapshot.visualOverlays)sources.append(overlay.path);
    for(const auto &source:sources)if(!source.isEmpty()&&output.compare(normalized(source),Qt::CaseInsensitive)==0){report("Choose a different file from your source media.");return false;}
    exportDestination_=QFileInfo(path).absoluteFilePath();exportRevision_=revision;retryExportProject_=snapshot;exportLog_.clear();
    QString error=snapshot.validate();const auto size=error.isEmpty()?Media::projectOutputSize(snapshot,&error):std::nullopt;
    if(!size){exportState_="Failed";report(error);return false;}
    exportDetails_=QString("%1×%2 · 60 fps · H.264 / AAC · original media").arg(size->width).arg(size->height);
    scrubbing_=skim_=resumeAfterScrub_=resumeWhenSettled_=false;preview_->pause();proxyCache_->setPaused(true);
    sourcePreview_->pause();mediaCache_->setInteractive(true);
    auto *job=new ExportJob(exportWorkerExecutable(),this);exportJob_=job;
    const auto details=exportDetails_;
    connect(job,&ExportJob::progressChanged,this,[this](int percent){exportProgress_=percent;emit stateChanged();});
    connect(job,&ExportJob::diagnostic,this,[this,details](const QString &message){
        if(message.startsWith("Encoder: ")){
            exportDetails_=details+(message.contains("h264_videotoolbox")?" · Apple hardware encoding":" · software encoding");emit stateChanged();
        }else if(message.startsWith("Retrying with software")){
            exportDetails_=details+" · retrying with software";emit stateChanged();
        }
    });
    connect(job,&ExportJob::finished,this,[this,job,path](bool success,bool canceled,const QString &error){
        exportJob_=nullptr;job->deleteLater();proxyCache_->setPaused(false);
        if(proxyPending_){proxyPending_=false;rebuild();}
        mediaCache_->setInteractive(playing()||scrubbing_||skim_);
        exportState_=success?"Completed":canceled?"Canceled":"Failed";
        if(success)exportProgress_=100;
        report(success?"Exported "+QFileInfo(path).fileName():canceled?"Export canceled. Existing output was preserved.":"Export failed: "+error);emit stateChanged();
    });
    exportProgress_=0;exportState_="Exporting";
    if(!job->start(snapshot,path,&error)){
        exportJob_=nullptr;delete job;proxyCache_->setPaused(false);mediaCache_->setInteractive(playing()||scrubbing_||skim_);
        exportState_="Failed";report(error);emit stateChanged();return false;
    }
    exportLog_=job->logPath();emit stateChanged();return true;
}
void EditorController::cancelExport(){if(exportJob_){exportState_="Canceling";exportJob_->cancel();emit stateChanged();}}
void EditorController::openExportLog(){if(!exportLog_.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(exportLog_));}
bool EditorController::retryExport(){return retryExportProject_&&startExport(*retryExportProject_,exportDestination_,exportRevision_);}
void EditorController::revealExport(){if(exportState_=="Completed")QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(exportDestination_).absolutePath()));}
void EditorController::openExport(){if(exportState_=="Completed")QDesktopServices::openUrl(QUrl::fromLocalFile(exportDestination_));}
