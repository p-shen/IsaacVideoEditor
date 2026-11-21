#include "editor_controller.h"
#include "subtitle_models.h"
#include "subtitle_job.h"
#include "export_job.h"
#include "media_cache.h"
#include "preview.h"
#include "preview_cache.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QUrl>

QObject *EditorController::subtitleModels() const {return subtitleModels_;}
bool EditorController::subtitleEngineAvailable() const {return QFileInfo(subtitleEngineExecutable(subtitleCpuOnly_)).isExecutable();}
bool EditorController::canImportGeneratedSubtitles() const {
    return !generatingSubtitles()&&subtitleState_=="Completed"&&!subtitleImported_&&subtitleResultJob_&&subtitleRevision_==projectRevision_;
}
void EditorController::setSubtitleCpuOnly(bool cpu){
    if(generatingSubtitles()||cpu==subtitleCpuOnly_)return;subtitleCpuOnly_=cpu;QSettings().setValue("subtitles/cpuOnly",cpu);emit subtitleChanged();
}
void EditorController::generateSrtDialog(){
    if(generatingSubtitles()||exporting())return;
    const auto destination=QFileDialog::getSaveFileName(nullptr,"Generate SRT from edited timeline",QDir::home().filePath("Movies/captions-"+subtitleModels_->selectedModel()+".srt"),"SubRip captions (*.srt)");
    if(!destination.isEmpty())generateSrtTo(destination);
}
bool EditorController::generateSrtTo(const QString &destination){
    if(generatingSubtitles()||exporting()||destination.isEmpty())return false;
    if(!subtitleModels_->ready()){subtitleModels_->refresh();report("Download and verify the selected subtitle model first.");return false;}
    if(!subtitleEngineAvailable()){report("This build does not include the local subtitle engine. See the build setup guide.");return false;}
    if(visualBefore_)endVisualGesture(false);if(audioPreview_)endAudioGesture(false);if(textDragBefore_)endTextDrag(false);
    const auto normalized=[](const QString &path){QFileInfo f(path);return f.exists()?f.canonicalFilePath():f.absoluteFilePath();};
    if(!path_.isEmpty()&&normalized(destination).compare(normalized(path_),Qt::CaseInsensitive)==0){report("Choose a different file from your saved project.");return false;}
    if(subtitleResultJob_){subtitleResultJob_->deleteLater();subtitleResultJob_=nullptr;}
    auto *job=new SubtitleJob(exportWorkerExecutable(),subtitleEngineExecutable(subtitleCpuOnly_),this);subtitleJob_=job;
    subtitleRevision_=projectRevision_;subtitleModel_=subtitleModels_->selectedModel();subtitleDestination_=QFileInfo(destination).absoluteFilePath();subtitleImported_=false;subtitleWarnings_.clear();subtitleLog_.clear();
    connect(job,&SubtitleJob::changed,this,[this,job]{subtitleState_=job->stage();subtitleProgress_=job->progress();subtitleDevice_=job->device();emit subtitleChanged();});
    connect(job,&SubtitleJob::finished,this,[this,job](bool ok,bool canceled,const QString &error){
        subtitleJob_=nullptr;subtitleWarnings_=job->warnings();subtitleLog_=job->logPath();
        if(!ok&&!canceled&&!error.isEmpty())subtitleWarnings_.append(error);
        if(ok){subtitleResultJob_=job;if(subtitleRevision_!=projectRevision_)subtitleWarnings_.append("The project changed. This SRT belongs to the captured revision; generate again before importing here.");}
        else job->deleteLater();
        proxyCache_->setPaused(false);mediaCache_->setInteractive(playing()||scrubbing_||skim_);
        if(proxyPending_){proxyPending_=false;rebuild();}
        report(ok?"SRT saved. Review it, then import the generated captions.":canceled?"Subtitle generation canceled. Existing output was preserved.":error);
        emit subtitleChanged();emit stateChanged();
    });
    scrubbing_=skim_=resumeAfterScrub_=resumeWhenSettled_=playWhenPrepared_=sourcePlayWhenPrepared_=false;
    preview_->requestPause(false);sourcePreview_->requestPause(false);proxyCache_->setPaused(true);mediaCache_->setInteractive(true);
    QString error;
    if(!job->start(project_,subtitleDestination_,subtitleModels_->modelPath(),subtitleCpuOnly_,&error)){
        subtitleJob_=nullptr;delete job;subtitleState_="Failed";subtitleWarnings_={error};proxyCache_->setPaused(false);mediaCache_->setInteractive(playing()||scrubbing_||skim_);report(error);emit subtitleChanged();emit stateChanged();return false;
    }
    subtitleLog_=job->logPath();emit stateChanged();return true;
}
void EditorController::cancelSubtitleGeneration(){if(subtitleJob_)subtitleJob_->cancel();}
void EditorController::confirmImportGeneratedSubtitles(){
    if(!canImportGeneratedSubtitles())return;
    if(!project_.captions.isEmpty()&&QMessageBox::question(nullptr,"Append generated captions?","The project already has captions. Import appends another set and may create duplicates. You can undo the import.",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    importGeneratedSubtitles(true);
}
bool EditorController::importGeneratedSubtitles(bool confirmedAppend){
    if(visualBefore_)endVisualGesture(false);if(audioPreview_)endAudioGesture(false);if(textDragBefore_)endTextDrag(false);
    if(!canImportGeneratedSubtitles()){report("These subtitles were already imported or belong to another project revision. Review the saved SRT or generate again.");return false;}
    if(!subtitleResultJob_->capturedSourcesUnchanged()){report("Source media changed. Generate subtitles again before importing them.");return false;}
    if(!project_.captions.isEmpty()&&!confirmedAppend){report("Import will append to existing captions and may create duplicates. Confirm append to continue.");return false;}
    if(!importSrt(subtitleDestination_))return false;subtitleImported_=true;emit subtitleChanged();return true;
}
void EditorController::revealGeneratedSubtitles(){if(subtitleState_=="Completed")QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(subtitleDestination_).absolutePath()));}
void EditorController::openSubtitleLog(){if(!subtitleLog_.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(subtitleLog_));}
