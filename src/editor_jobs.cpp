#include "editor_controller.h"
#include "export_job.h"
#include "recovery_store.h"
#include "media_cache.h"
#include "mlt_engine.h"
#include "source_timing.h"
#include "encoding.h"
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
#include <QRunnable>
#include <QSettings>
#include <QtMath>
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
void EditorController::recoverSnapshotAsync(const QString &path){
    QVariantMap offered;
    for(const auto &value:recoveryCandidates_)if(value.toMap()["path"].toString()==path)offered=value.toMap();
    if(offered.isEmpty()||!maybeSave())return;
    loadProjectAsync(path,offered);
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
bool EditorController::relinkMedia(const QString &missing,const QString &replacement){return relinkInspectedMedia(missing,replacement,{});}
bool EditorController::relinkInspectedMedia(const QString &missing,const QString &replacement,const QVariantMap &inspection){
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
    QString error;bool still=false;std::optional<Media::MediaInfo> info;
    if(!inspection.isEmpty()){
        const QFileInfo file(replacement);
        if(!file.isFile()||file.size()!=inspection["bytes"].toLongLong()||file.lastModified().toMSecsSinceEpoch()!=inspection["modified"].toLongLong()){
            report("The replacement changed while it was being checked. Choose it again.");return false;
        }
        still=inspection["still"].toBool();
        info=Media::MediaInfo{inspection["duration"].toDouble(),inspection["width"].toInt(),inspection["height"].toInt(),inspection["audio"].toBool()};
    }else{
        QImageReader reader(replacement);still=reader.canRead();
        if(still){const auto image=reader.read();if(!image.isNull()){Media::MediaInfo imageInfo;imageInfo.durationSeconds=86400;imageInfo.width=image.width();imageInfo.height=image.height();info=imageInfo;}else error=reader.errorString();}
        else info=Media::retryMediaInfo(replacement,&error);
    }
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
    if(!replacement.isEmpty())relinkMediaAsync(path,replacement);
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
    if(!maybeSave())return false;cancelImport();++preparationRevision_;++sourceRevision_;if(recovery_)recovery_->clear();return true;
}
QString EditorController::exportFormat(bool optimizeForIphone) const {
    const auto size=Media::mp4OutputSize(project_,optimizeForIphone);
    return size?QString("%1×%2 · 60 fps · H.264 / AAC").arg(size->width).arg(size->height):QString{};
}
bool EditorController::exportTo(const QString &path,bool optimizeForIphone){
    if(visualBefore_)endVisualGesture(false);
    if(audioPreview_)endAudioGesture(false);
    if(textDragBefore_)endTextDrag(false);
    return startExport(project_,path,title()+(dirty()?" · unsaved revision":" · saved revision")+QString(" · %1 frames").arg(duration()),optimizeForIphone);
}
bool EditorController::startExport(const Project &snapshot,const QString &path,const QString &revision,bool optimizeForIphone){
    if(exporting()||snapshot.clips.isEmpty()||path.isEmpty())return false;
    const auto normalized=[](const QString &p){const QFileInfo f(p);return f.exists()?f.canonicalFilePath():f.absoluteFilePath();};
    const auto output=normalized(path);QStringList sources={snapshot.voicePath,snapshot.musicPath};for(const auto &c:snapshot.clips)sources.append(c.path);for(const auto &asset:snapshot.assets)sources.append(asset.path);
    for(const auto &overlay:snapshot.visualOverlays)sources.append(overlay.path);
    for(const auto &source:sources)if(!source.isEmpty()&&output.compare(normalized(source),Qt::CaseInsensitive)==0){report("Choose a different file from your source media.");return false;}
    exportDestination_=QFileInfo(path).absoluteFilePath();exportRevision_=revision;retryExportProject_=snapshot;retryExportForIphone_=optimizeForIphone;exportLog_.clear();
    QString error=snapshot.validate();const auto size=error.isEmpty()?Media::mp4OutputSize(snapshot,optimizeForIphone,&error):std::nullopt;
    if(!size){exportState_="Failed";report(error);return false;}
    exportDetails_=QString("%1×%2 · 60 fps · H.264 / AAC · original media").arg(size->width).arg(size->height);
    if(optimizeForIphone)exportDetails_+=" · optimized for iPhone";
    scrubbing_=skim_=resumeAfterScrub_=resumeWhenSettled_=playWhenPrepared_=sourcePlayWhenPrepared_=false;preview_->pause(false);proxyCache_->setPaused(!performanceBudget_.prepareDuringExport);
    sourcePreview_->pause(false);mediaCache_->setInteractive(!performanceBudget_.prepareDuringExport);
    auto *job=new ExportJob(exportWorkerExecutable(),this);exportJob_=job;
    const auto details=exportDetails_;
    connect(job,&ExportJob::progressChanged,this,[this](int percent){exportProgress_=percent;emit stateChanged();});
    connect(job,&ExportJob::diagnostic,this,[this,details](const QString &message){
        if(message.startsWith("Encoder: ")){
            exportDetails_=details+" · "+Media::h264EncoderLabel(message.mid(9).trimmed());emit stateChanged();
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
    if(!job->start(snapshot,path,&error,optimizeForIphone)){
        exportJob_=nullptr;delete job;proxyCache_->setPaused(false);mediaCache_->setInteractive(playing()||scrubbing_||skim_);
        exportState_="Failed";report(error);emit stateChanged();return false;
    }
    exportLog_=job->logPath();emit stateChanged();return true;
}
void EditorController::cancelExport(){if(exportJob_){exportState_="Canceling";exportJob_->cancel();emit stateChanged();}}
void EditorController::openExportLog(){if(!exportLog_.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(exportLog_));}
bool EditorController::retryExport(){return retryExportProject_&&startExport(*retryExportProject_,exportDestination_,exportRevision_,retryExportForIphone_);}
void EditorController::revealExport(){if(exportState_=="Completed")QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(exportDestination_).absolutePath()));}
void EditorController::openExport(){if(exportState_=="Completed")QDesktopServices::openUrl(QUrl::fromLocalFile(exportDestination_));}

void EditorController::cancelImport(){
    if(importing_)++supersededJobs_;
    ++importRevision_;importPool_.clear();
    if(importing_){importing_=false;emit stateChanged();}
}
void EditorController::startImport(const QStringList &paths,bool append,int insertFrame){
    if(paths.isEmpty())return;
    cancelPendingVisual();if(textDragBefore_)endTextDrag(false);
    cancelImport();const quint64 generation=importRevision_.load(),revision=projectRevision_;
    const Project snapshot=project_;++importJobs_;importClock_.start();importing_=true;emit stateChanged();
    importPool_.start(QRunnable::create([this,paths,append,insertFrame,snapshot,generation,revision]{
        auto next=std::make_shared<Project>(snapshot);QStringList problems;QHash<QString,QString> identities;
        for(const auto &input:paths){const auto path=QFileInfo(input).absoluteFilePath();identities[path]=SourceTiming::fingerprint(path);}
        const auto canceled=[this,generation]{return generation!=importRevision_.load();};
        bool changed=addImported(*next,paths,append,problems,canceled);int last=-1;
        if(insertFrame>=0&&!canceled()){
            int at=insertFrame;
            for(const auto &input:paths){
                const auto path=QFileInfo(input).absoluteFilePath();
                const auto source=std::find_if(next->assets.cbegin(),next->assets.cend(),[&](const MediaAsset &a){return a.path==path;});
                if(source==next->assets.cend()||source->kind!="video"||source->outFrame<=source->inFrame){problems.append("Drop a valid video range on the main sequence.");last=-1;break;}
                const int frames=source->outFrame-source->inFrame;
                last=next->insertClipAtFrame({path,source->inFrame/60.0,source->outFrame/60.0},at);
                if(last<0){problems.append("Cannot insert inside a crossfade. Drop outside the overlap or on a hard cut.");break;}
                at+=frames;
            }
            changed=last>=0;
        }
        if(canceled())return;
        QMetaObject::invokeMethod(this,[this,next,problems,identities,append,insertFrame,last,changed,generation,revision]{
            if(generation!=importRevision_.load())return;
            importing_=false;importMs_=importClock_.elapsed();emit stateChanged();
            if(revision!=projectRevision_){report("Import canceled because the project changed. Import the files again to add them to this revision.");return;}
            if(changed){
                for(auto it=identities.cbegin();it!=identities.cend();++it)if(SourceTiming::fingerprint(it.key())!=it.value()){report("Media changed while it was being imported. Import it again.");return;}
                for(const auto &asset:next->assets)durations_[asset.path]=asset.durationFrames/60.0;
                if(append||insertFrame>=0){showProject();const int selected=insertFrame>=0?last:next->clips.size()-1;commit(*next,{selected},selected,insertFrame>=0?"Drop media into sequence":"Import clips",false,-1);}
                else if(commit(*next,selection_,primary_,"Import media")&&!next->assets.isEmpty())selectSource(next->assets.size()-1);
            }
            if(!problems.isEmpty())report(problems.join('\n'));
        },Qt::QueuedConnection);
    }));
}
void EditorController::dropMediaAtAsync(const QStringList &paths,int frame){
    if(frame<0||frame>duration())return;
    startImport(paths,false,frame);
}
void EditorController::chooseAudioAsync(const QString &path,bool music){
    if(path.isEmpty())return;
    cancelImport();const quint64 generation=importRevision_.load(),revision=projectRevision_;
    ++importJobs_;importClock_.start();importing_=true;emit stateChanged();
    importPool_.start(QRunnable::create([this,path,music,generation,revision]{
        const auto identity=SourceTiming::fingerprint(path);const auto info=Media::retryMediaInfo(path);const auto seconds=info?info->durationSeconds:0;
        QMetaObject::invokeMethod(this,[this,path,music,seconds,identity,generation,revision]{
            if(generation!=importRevision_.load())return;
            importing_=false;importMs_=importClock_.elapsed();emit stateChanged();if(revision!=projectRevision_)return;
            if(SourceTiming::fingerprint(path)!=identity){report("Audio changed while it was being checked. Choose it again.");return;}
            durations_[path]=seconds;if(music)setMusicPath(path);else setVoicePath(path);
        },Qt::QueuedConnection);
    }));
}

QString EditorController::cacheDirectory() const {return proxyCache_->cacheDirectory();}
int EditorController::cacheLimitGiB() const {return int(proxyCache_->storageLimit()/(qint64(1024)*1024*1024));}
bool EditorController::setCacheDirectory(const QString &directory){
    QString error;if(!proxyCache_->setCacheDirectory(directory,&error)){report(error);return false;}
    QSettings().setValue("preview/cacheDirectory",proxyCache_->cacheDirectory());
    rebuild();if(sourceMode_)rebuildSource();emit stateChanged();return true;
}
void EditorController::chooseCacheDirectory(){
    const auto directory=QFileDialog::getExistingDirectory(nullptr,"Choose preview cache folder",cacheDirectory());
    if(!directory.isEmpty())setCacheDirectory(directory);
}
void EditorController::setCacheLimitGiB(int gib){
    gib=std::clamp(gib,0,1024);proxyCache_->setStorageLimit(qint64(gib)*1024*1024*1024);
    QSettings().setValue("preview/storageLimitBytes",proxyCache_->storageLimit());emit stateChanged();
}
void EditorController::setPreviewQuality(int maxHeight){
    if(maxHeight!=0&&maxHeight!=540&&maxHeight!=1080&&maxHeight!=2160)return;
    const QSize size=maxHeight?QSize(maxHeight*16/9,maxHeight):automaticPreviewSize_;
    previewQuality_=maxHeight;performanceBudget_.previewWidth=size.width();performanceBudget_.previewHeight=size.height();
    QSettings settings;settings.setValue("preview/quality",maxHeight);settings.setValue("preview/width",size.width());settings.setValue("preview/height",size.height());
    preview_->setQualityLimit(size);sourcePreview_->setQualityLimit(size);proxyCache_->setQualityLimit(size);
    rebuild();if(sourceMode_)rebuildSource();emit stateChanged();
}
QVariantMap EditorController::performanceDiagnostics() const {
    const auto memory=Performance::processMemory();const auto resources=Performance::resources();
    return {{"preview",preview_->diagnostics()},{"sourcePreview",sourcePreview_->diagnostics()},
        {"media",mediaCache_->diagnostics()},{"proxy",proxyCache_->diagnostics()},
        {"preparingTimeline",preparing_},{"preparingSource",sourcePreparing_},{"importing",importing_},
        {"projectRevision",qulonglong(projectRevision_)},{"clipCount",project_.clips.size()},
        {"timelinePreparationJobs",qulonglong(preparationJobs_)},{"sourcePreparationJobs",qulonglong(sourceJobs_)},{"importJobs",qulonglong(importJobs_)},
        {"supersededPreparationJobs",qulonglong(supersededJobs_)},{"timelinePreparationMs",preparationMs_},{"sourcePreparationMs",sourcePreparationMs_},{"importMs",importMs_},
        {"metadataMs",metadataMs_},{"timelineBuildMs",timelineBuildMs_},
        {"residentBytes",qulonglong(memory.residentBytes)},{"peakResidentBytes",qulonglong(memory.peakResidentBytes)},
        {"logicalCpus",resources.logicalCpus},{"availableMemoryBytes",qulonglong(resources.availableMemoryBytes)},
        {"totalMemoryBytes",qulonglong(resources.totalMemoryBytes)}};
}

void EditorController::setVoicePathAsync(const QString &path){if(path.isEmpty())setVoicePath(path);else chooseAudioAsync(path,false);}
void EditorController::setMusicPathAsync(const QString &path){if(path.isEmpty())setMusicPath(path);else chooseAudioAsync(path,true);}
void EditorController::relinkMediaAsync(const QString &missing,const QString &replacement){
    if(missing.isEmpty()||replacement.isEmpty())return;
    cancelImport();const quint64 generation=importRevision_.load(),revision=projectRevision_;
    ++importJobs_;importClock_.start();importing_=true;emit stateChanged();
    importPool_.start(QRunnable::create([this,missing,replacement,generation,revision]{
        QString error;const QFileInfo file(replacement);const auto bytes=file.size(),modified=file.lastModified().toMSecsSinceEpoch();QImageReader reader(replacement);const bool still=reader.canRead();std::optional<Media::MediaInfo> info;
        if(still){const auto image=reader.read();if(!image.isNull())info=Media::MediaInfo{86400,image.width(),image.height()};else error=reader.errorString();}
        else info=Media::retryMediaInfo(replacement,&error);
        QVariantMap inspection;
        if(info)inspection={{"duration",info->durationSeconds},{"width",info->width},{"height",info->height},{"audio",info->hasAudio},{"still",still},{"bytes",bytes},{"modified",modified}};
        QMetaObject::invokeMethod(this,[this,missing,replacement,inspection,error,generation,revision]{
            if(generation!=importRevision_.load())return;
            importing_=false;importMs_=importClock_.elapsed();emit stateChanged();if(revision!=projectRevision_)return;
            if(inspection.isEmpty()){report(error.isEmpty()?"Could not inspect the replacement media.":error);return;}
            relinkInspectedMedia(missing,replacement,inspection);
        },Qt::QueuedConnection);
    }));
}

void EditorController::loadProjectAsync(const QString &path,const QVariantMap &recoveryOffer){
    cancelImport();const quint64 generation=importRevision_.load(),revision=projectRevision_;
    ++importJobs_;importClock_.start();importing_=true;emit stateChanged();
    importPool_.start(QRunnable::create([this,path,recoveryOffer,generation,revision]{
        QString error;const auto parsed=loadProject(path,&error);std::shared_ptr<Project> loaded;
        QHash<QString,double> durations;QHash<QString,QString> identities;
        if(parsed){
            loaded=std::make_shared<Project>(*parsed);QStringList audio{loaded->voicePath,loaded->musicPath};audio.removeAll(QString{});audio.removeDuplicates();
            for(const auto &source:audio){
                if(generation!=importRevision_.load())return;
                identities[source]=SourceTiming::fingerprint(source);const auto info=Media::retryMediaInfo(source);durations[source]=info?info->durationSeconds:0;
            }
        }
        QMetaObject::invokeMethod(this,[this,path,loaded,durations,identities,error,recoveryOffer,generation,revision]{
            if(generation!=importRevision_.load())return;
            importing_=false;importMs_=importClock_.elapsed();emit stateChanged();if(revision!=projectRevision_)return;
            if(!loaded){report(error);return;}
            for(auto it=identities.cbegin();it!=identities.cend();++it)if(SourceTiming::fingerprint(it.key())!=it.value()){
                report("Audio changed while the project was being checked. Open the project again.");return;
            }
            applyLoadedProject(*loaded,recoveryOffer.isEmpty()?path:recoveryOffer["projectPath"].toString(),durations);
            if(!recoveryOffer.isEmpty()){
                undo_.resetClean();scheduleRecovery();
                if(recovery_&&recovery_->flush())discardRecovery(path);
                report("Recovered as unsaved work. Your saved project was preserved.");
            }
        },Qt::QueuedConnection);
    }));
}
