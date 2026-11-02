#include "editor_controller.h"
#include "export_job.h"
#include "text_style.h"
#include "transition_presets.h"
#include "media_cache.h"
#include "mlt_engine.h"
#include "source_timing.h"
#include "preview.h"
#include "preview_cache.h"
#include "quick_timeline.h"
#include "text_renderer.h"
#include "recovery_store.h"
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QFontDatabase>
#include <QColorDialog>
#include <QDrag>
#include <QMimeData>
#include <QQuickWindow>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QRunnable>
#include <QSettings>
#include <QUrl>
#include <QUndoCommand>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
QString clockText(int frame) {
    frame=std::max(0,frame);
    return QString("%1:%2:%3").arg(frame/3600,2,10,QChar('0')).arg(frame/60%60,2,10,QChar('0')).arg(frame%60,2,10,QChar('0'));
}
QString moviesDirectory() { return QDir::home().filePath("Movies"); }
QString audioFilter() { return "Audio files (*.wav *.m4a *.mp3 *.aac *.flac *.aiff);;All files (*)"; }
class Edit final : public QUndoCommand {
public:
    Edit(const QString &label,std::function<void()> undo,std::function<void()> redo)
        : QUndoCommand(label),undo_(std::move(undo)),redo_(std::move(redo)) {}
    void undo() override { undo_(); }
    void redo() override { redo_(); }
private:
    std::function<void()> undo_,redo_;
};
bool sameMedia(const Project &a,const Project &b) {
    if(a.visualOverlays!=b.visualOverlays || a.visualLaneVisible!=b.visualLaneVisible || a.canvasWidth!=b.canvasWidth || a.canvasHeight!=b.canvasHeight)return false;
    if(a.clips.size()!=b.clips.size() || a.crossfadeFrames!=b.crossfadeFrames || a.voicePath!=b.voicePath ||
       a.voiceDurationFrames!=b.voiceDurationFrames || a.musicDurationFrames!=b.musicDurationFrames ||
       a.voiceStartSeconds!=b.voiceStartSeconds || a.musicPath!=b.musicPath || a.musicDb!=b.musicDb || !(a.voiceAudio==b.voiceAudio) ||
       a.musicMuted!=b.musicMuted || a.musicLoopToEnd!=b.musicLoopToEnd || a.musicFadeInFrames!=b.musicFadeInFrames || a.musicFadeOutFrames!=b.musicFadeOutFrames) return false;
    for(int i=0;i<a.clips.size();++i) {
        const auto &x=a.clips[i], &y=b.clips[i];
        if(x.path!=y.path || x.inSeconds!=y.inSeconds || x.outSeconds!=y.outSeconds ||
           x.hardCutBefore!=y.hardCutBefore || x.rotationDegrees!=y.rotationDegrees || !(x.audio==y.audio) ||
           x.transitionFrames!=y.transitionFrames || x.transitionType!=y.transitionType || x.id!=y.id || x.transform!=y.transform) return false;
    }
    return true;
}
bool sameProject(const Project &a,const Project &b) {return sameMedia(a,b)&&a.textOverlays==b.textOverlays&&a.captions==b.captions&&a.burnCaptions==b.burnCaptions&&a.assets==b.assets;}
QVariantList validIndices(const QVariantList &input,int count) {
    QList<int> list;
    for(const auto &v:input) { bool ok=false; int i=v.toInt(&ok); if(ok&&i>=0&&i<count&&!list.contains(i)) list.append(i); }
    std::sort(list.begin(),list.end()); QVariantList result;
    for(int i:list) result.append(i);
    return result;
}
}

EditorController::EditorController(QObject *parent,const Performance::Budget &budget):QObject(parent),performanceBudget_(budget) {
    automaticPreviewSize_={budget.previewWidth,budget.previewHeight};
    preparationPool_.setMaxThreadCount(1);sourcePool_.setMaxThreadCount(1);importPool_.setMaxThreadCount(1);
    connect(this,&EditorController::projectChanged,this,[this]{timelineData_=project_.timelineData();++projectRevision_;emit splitAvailabilityChanged();if(preparing_&&!sameProject(visibleProject(),preparationSnapshot_))rebuild();});
    connect(this,&EditorController::selectionChanged,this,&EditorController::splitAvailabilityChanged);
    connect(this,&EditorController::positionChanged,this,&EditorController::splitAvailabilityChanged);
    connect(this,&EditorController::sourceChanged,this,&EditorController::sourcePositionChanged);
    connect(this,&EditorController::projectChanged,this,&EditorController::compositionChanged);
    connect(this,&EditorController::selectionChanged,this,&EditorController::compositionChanged);
    connect(this,&EditorController::compositionChanged,this,&EditorController::transformChanged);
    connect(this,&EditorController::positionChanged,this,[this]{if(selectedOverlay_>=0||primary_>=0)emit transformChanged();});
    Media::initializeMlt();
    Media::configureOutputProfile(profile_,std::clamp(budget.previewWidth,2,3840),std::clamp(budget.previewHeight,2,2160));
    preview_=std::make_unique<Preview>(profile_,this,budget);
    Media::configureOutputProfile(sourceProfile_,std::clamp(budget.previewWidth,2,3840),std::clamp(budget.previewHeight,2,2160));
    connect(this,&EditorController::sourceChanged,this,&EditorController::scheduleRecovery);
    sourcePreview_=std::make_unique<Preview>(sourceProfile_,this,budget);
    connect(sourcePreview_.get(),&Preview::frameReady,this,[this](const QImage &image){if(sourceMode_){frame_=image;emit frameChanged();}});
    connect(sourcePreview_.get(),&Preview::positionChanged,this,[this](int frame){
        if(sourceMode_ && selectedSource_>=0 && selectedSource_<project_.assets.size()){
            auto &a=project_.assets[selectedSource_];const int next=qBound(0,frame,a.durationFrames-1);
            if(sourceScrubbing_)sourceScrubFrame_=next;
            else if(a.cursorFrame!=next){const bool wasDirty=dirty();a.cursorFrame=next;sourceViewDirty_=true;if(!wasDirty)emit stateChanged();}
            emit sourcePositionChanged();
        }
    });
    connect(sourcePreview_.get(),&Preview::playbackChanged,this,[this](bool playing){if(!playing)scheduleRecovery();emit stateChanged();});
    connect(sourcePreview_.get(),&Preview::errorOccurred,this,[this](const QString &message){
        if(sourceUsesProxy_&&sourceMode_&&selectedSource_>=0&&selectedSource_<project_.assets.size()){
            sourceUsesProxy_=false;proxyCache_->discard(project_.assets[selectedSource_].path);rebuildSource();return;
        }
        sourcePlayWhenPrepared_=false;report(message);
    });
    connect(sourcePreview_.get(),&Preview::seekSettled,this,[this](int,bool committed){if(sourcePlayWhenPrepared_&&sourceMode_&&committed){sourcePlayWhenPrepared_=false;sourcePreview_->play();}});
    connect(preview_.get(),&Preview::audioPeak,this,[this](double peak){
        if(!std::isfinite(peak)||peak<0)return;
        masterPeak_=peak>0?std::max(-60.0,20*std::log10(peak)):-60;masterClipped_|=peak>=1;emit audioLevelsChanged();
    });
    proxyCache_=new PreviewCache({},this,budget);
    mediaCache_=new MediaCache(this,budget);
    undo_.setUndoLimit(100);
    connect(&undo_,&QUndoStack::indexChanged,this,[this]{
        if(noticeCanUndo_){notice_.clear();noticeCanUndo_=false;}
        emit stateChanged();
    });
    connect(&undo_,&QUndoStack::cleanChanged,this,[this]{ emit stateChanged(); });
    connect(preview_.get(),&Preview::frameReady,this,[this](const QImage &image){ if(!sourceMode_){frame_=image; emit frameChanged();} });
    connect(preview_.get(),&Preview::positionChanged,this,[this](int frame){ if(sourceMode_||rebuilding_)return;position_=frame; prioritizeProxy(frame); emit positionChanged(); });
    connect(preview_.get(),&Preview::playbackChanged,this,[this](bool active){ mediaCache_->setInteractive(active||scrubbing_||skim_); emit stateChanged(); });
    connect(preview_.get(),&Preview::errorOccurred,this,[this](const QString &message){
        bool copies=false;
        for(int i=0;i<project_.clips.size();++i)if(proxyPaths_.value(i,project_.clips[i].path)!=project_.clips[i].path){copies=true;proxyCache_->discard(project_.clips[i].path);proxyPaths_[i]=project_.clips[i].path;}
        for(int i=0;i<project_.visualOverlays.size();++i)if(overlayProxyPaths_.value(i,project_.visualOverlays[i].path)!=project_.visualOverlays[i].path){copies=true;proxyCache_->discard(project_.visualOverlays[i].path);overlayProxyPaths_[i]=project_.visualOverlays[i].path;}
        if(copies){playWhenPrepared_|=resumeWhenSettled_;resumeWhenSettled_=false;rebuild();return;}
        resumeWhenSettled_=false;mediaCache_->setInteractive(playing()||scrubbing_||skim_||exporting());report(message);
    });
    loadingTimer_.setSingleShot(true); loadingTimer_.setInterval(200);
    connect(&loadingTimer_,&QTimer::timeout,this,[this]{ if(preview_->isSeeking()) { loading_=true; emit stateChanged(); } });
    connect(preview_.get(),&Preview::seekingChanged,this,[this](bool active){
        if(active) { if(!loadingTimer_.isActive()) loadingTimer_.start(); }
        else {
            loadingTimer_.stop(); loading_=false;
            mediaCache_->setInteractive(playing()||scrubbing_||skim_||exporting());
            QTimer::singleShot(0,this,&EditorController::refreshPendingProxy);
            emit stateChanged();
        }
    });
    connect(preview_.get(),&Preview::seekSettled,this,[this](int frame,bool committed){
        if(resumeWhenSettled_&&committed&&frame==position_) { resumeWhenSettled_=false; preview_->play(); }
        mediaCache_->setInteractive(playing()||scrubbing_||skim_);
    });
    connect(proxyCache_,&PreviewCache::statusChanged,this,[this](const QString &message){if(message=="Lighter preview ready"&&previewStatus_=="Smooth preview")return;previewStatus_=message;emit stateChanged();});
    connect(proxyCache_,&PreviewCache::failed,this,[this](const QString &,const QString &message){ previewStatus_="Using original media"; report(message); });
    connect(proxyCache_,&PreviewCache::ready,this,[this](const QString &source,const QString &path){
        bool needsRefresh=false;
        for(int row=0;row<project_.clips.size();++row) {
            const auto &clip=project_.clips[row];
            if(clip.path!=source) continue;
            const auto copy=proxyCache_->readyCopy(source,qRound(clip.inSeconds*60),qRound(clip.outSeconds*60));
            if(copy.path==path&&proxyPaths_.value(row)!=path) needsRefresh=true;
        }
        for(int row=0;row<project_.visualOverlays.size();++row){
            const auto &overlay=project_.visualOverlays[row];
            if(overlay.path!=source||!project_.visualLaneVisible[overlay.lane])continue;
            const auto copy=proxyCache_->readyCopy(source,overlay.sourceInFrame,overlay.sourceInFrame+project_.overlayFrames(row));
            if(copy.path==path&&overlayProxyPaths_.value(row)!=path)needsRefresh=true;
        }
        if(sourceMode_&&selectedSource_>=0&&selectedSource_<project_.assets.size()&&project_.assets[selectedSource_].path==source&&
           !sourcePreview_->isPlaying()&&!sourcePreview_->isSeeking()&&
           !proxyCache_->readyCopy(source,0,project_.assets[selectedSource_].durationFrames).path.isEmpty())rebuildSource();
        if(!needsRefresh) return;
        if(exporting()||preview_->isSeeking()||scrubbing_||skim_||visualBefore_) { proxyPending_=true; return; }
        rebuild();
    });
    const int quality=QSettings().value("preview/quality",0).toInt();
    if(quality==540||quality==1080||quality==2160)setPreviewQuality(quality);
}
EditorController::~EditorController() {
    ++preparationRevision_;++sourceRevision_;++importRevision_;
    preparationPool_.clear();sourcePool_.clear();importPool_.clear();
    preparationPool_.waitForDone();sourcePool_.waitForDone();importPool_.waitForDone();
    disconnect(proxyCache_,nullptr,this,nullptr);
    if(exportJob_) {disconnect(exportJob_,nullptr,this,nullptr);exportJob_->stopAndWait();}
    sourcePreview_->setProducer(nullptr);sourceProducer_.reset();sourceProxyLease_.reset();
    preview_->setProducer(nullptr); timeline_.reset();timelineProxyLeases_.clear(); undo_.clear();
}
EditorController::State EditorController::state() const { return {project_,selection_,primary_,selectedText_,selectedOverlay_,selectedCaption_,captionSelection_}; }
void EditorController::apply(const State &s,int position,bool forceRebuild) {
    finishSourceScrub(true);
    audioPreview_.reset();
    const bool textOnly=!forceRebuild&&!preparing_&&timeline_&&sameMedia(project_,s.project);
    const QString sourcePath=selectedSource_>=0 && selectedSource_<project_.assets.size()?project_.assets[selectedSource_].path:QString();
    resumeWhenSettled_=false; scrubbing_=skim_=false;
    project_=s.project;timelineData_=project_.timelineData();
    selectedSource_=-1; for(int i=0;i<project_.assets.size();++i)if(project_.assets[i].path==sourcePath)selectedSource_=i;
    if(selectedSource_<0 && sourceMode_){sourceMode_=false;++sourceRevision_;sourcePreparing_=false;sourcePreview_->setProducer(nullptr,false);sourceProducer_.reset();sourceProxyLease_.reset();}
    selection_=validIndices(s.selection,project_.clips.size());
    selectedOverlay_=s.overlay>=0&&s.overlay<project_.visualOverlays.size()?s.overlay:-1;
    selectedText_=s.text>=0&&s.text<project_.textOverlays.size()?s.text:-1;
    captionSelection_=validIndices(s.captions,project_.captions.size());
    selectedCaption_=captionSelection_.contains(s.caption)?s.caption:(captionSelection_.isEmpty()?-1:captionSelection_.last().toInt());
    captionAnchor_=selectedCaption_;
    primary_=selection_.contains(s.primary)?s.primary:(selection_.isEmpty()?-1:selection_.last().toInt());
    if(transitionIndex_>=project_.clips.size())transitionIndex_=-1;
    anchor_=primary_; position_=qBound(0,position,std::max(0,duration()-1));
    if(textOnly){preview_->pause(false);preview_->requestSeek(position_,true,[timeline=timeline_.get(),texts=project_.renderedTexts()]{TextRendering::update(*timeline,texts);});}else rebuild();
    if(sourceMode_)rebuildSource();
    emit projectChanged(); emit selectionChanged(); emit positionChanged(); emit stateChanged(); emit sourceChanged();
}
bool EditorController::commit(const Project &requested,const QVariantList &selection,int primary,const QString &label,bool force,int text,int overlay,int caption,const QVariantList &captions) {
    if(visualBefore_){report("Apply or cancel the visual preview before committing a different edit.");return false;}
    Project next=requested;
    for(auto &clip:next.clips){const int frames=next.clipFrames(clip);clip.audio.fadeInFrames=std::min(clip.audio.fadeInFrames,frames);clip.audio.fadeOutFrames=std::min(clip.audio.fadeOutFrames,frames);}
    for(int lane:{1,2}){
        auto controls=next.audioControls(lane);const int frames=audioPlayableFrames(next,lane);
        if(sourceDuration(lane==1?next.voicePath:next.musicPath)>0){controls.fadeInFrames=std::min(controls.fadeInFrames,frames);controls.fadeOutFrames=std::min(controls.fadeOutFrames,frames);next.setAudioControls(lane,controls);}
    }
    next.normalizeVisuals();
    next.reviewCaptionTimingAfter(project_);
    if(!force&&sameProject(project_,next)) return false;
    const auto error=next.validate(); if(!error.isEmpty()) { report(error); return false; }
    const State before=state();
    State after{next,validIndices(selection,next.clips.size()),primary,text==-2?selectedText_:text,overlay==-2?selectedOverlay_:overlay,caption==-2?selectedCaption_:caption,caption==-2?captionSelection_:validIndices(captions,next.captions.size())};
    if(text>=0||overlay>=0||primary>=0||!selection.isEmpty()){after.caption=-1;after.captions.clear();}
    const int position=position_;
    undo_.push(new Edit(label,[this,before,position]{apply(before,position);},[this,after,position]{apply(after,position);}));
    scheduleRecovery();
    return true;
}
double EditorController::sourceDuration(const QString &path) const {
    if(path.isEmpty()) return 0;
    if(!durations_.contains(path)){
        for(const auto &asset:project_.assets)if(asset.path==path){durations_.insert(path,asset.durationFrames/60.0);break;}
        if(!durations_.contains(path))durations_.insert(path,Media::mediaDurationSeconds(path));
    }
    if(durations_.value(path)>0)return durations_.value(path);
    int frames=0;
    if(path==project_.voicePath)frames=project_.voiceDurationFrames;
    if(path==project_.musicPath)frames=std::max(frames,project_.musicDurationFrames);
    for(const auto &asset:project_.assets)if(asset.path==path)frames=std::max(frames,asset.durationFrames);
    return frames/60.0;
}
void EditorController::restoreAudioDurations(Project &p) const {
    const auto restore=[this,&p](const QString &path,int &frames){
        if(path.isEmpty()||frames>0)return;
        if(!durations_.contains(path))durations_.insert(path,Media::mediaDurationSeconds(path));
        frames=qRound(durations_.value(path)*60);
        if(frames==0)for(const auto &asset:p.assets)if(asset.path==path)frames=std::max(frames,asset.durationFrames);
    };
    restore(p.voicePath,p.voiceDurationFrames);restore(p.musicPath,p.musicDurationFrames);
}
QVector<double> EditorController::sourceDurations() const {
    QVector<double> values; for(const auto &c:project_.clips) values.append(std::max(c.outSeconds,sourceDuration(c.path))); return values;
}
double EditorController::voiceDuration() const { return sourceDuration(project_.voicePath); }
QVariantList EditorController::clips() const {
    QVariantList result;
    for(int i=0;i<project_.clips.size();++i) {
        const auto &c=project_.clips[i];
        result.append(QVariantMap{{"index",i},{"id",c.id},{"attachmentLabel",QString("%1 · %2 · frame %3").arg(i+1).arg(QFileInfo(c.path).fileName()).arg(timelineData_.starts.value(i))},{"path",c.path},{"name",QFileInfo(c.path).fileName()},
            {"startFrame",timelineData_.starts.value(i)},{"frames",project_.clipFrames(c)},
            {"durationText",clockText(project_.clipFrames(c))},{"inSeconds",c.inSeconds},{"outSeconds",c.outSeconds},
            {"lengthSeconds",project_.clipFrames(c)/60.0},{"rotation",c.rotationDegrees},
            {"maximumSeconds",std::max(c.outSeconds,sourceDuration(c.path))}});
    }
    return result;
}
QVariantMap EditorController::currentClip() const { return primary_>=0&&primary_<project_.clips.size()?clips()[primary_].toMap():QVariantMap{}; }
QVariantList EditorController::textOverlays() const {
    QVariantList result;for(int i=0;i<project_.textOverlays.size();++i)result.append(timedTextInfo(project_,i));return result;
}
QVariantMap EditorController::currentText() const {return selectedText_>=0&&selectedText_<project_.textOverlays.size()?textOverlays()[selectedText_].toMap():QVariantMap{};}
QStringList EditorController::fontFamilies() const {static const auto families=QFontDatabase::families();return families;}
QVariantList EditorController::textPresets() const {return textPresetOptions();}
void EditorController::selectText(int index) {cancelPendingVisual();
    showProject();
    if(index<0||index>=project_.textOverlays.size())return;
    if(textDragBefore_)endTextDrag(false);
    selectedOverlay_=-1;transformMode_=false;
    selectedCaption_=-1;captionSelection_.clear();
    selectedText_=index;selection_.clear();primary_=anchor_=-1;emit selectionChanged();emit stateChanged();
    const auto &t=project_.textOverlays[index];const int start=project_.textStartFrame(index);if(position_<start||position_>=start+t.durationFrames)seek(start+std::min(30,t.durationFrames/2));
}
void EditorController::addText() {cancelPendingVisual();
    showProject();
    if(visualBefore_)endVisualGesture(false);
    if(audioPreview_)endAudioGesture(false);
    if(duration()<1)return;
    if(textDragBefore_)endTextDrag(false);
    auto next=project_;TextOverlay t;t.startFrame=position_;t.durationFrames=std::min(300,duration()-position_);
    if(!next.textOverlays.isEmpty()){t.style="box";t.fontSize=57;t.x=.24;t.y=.77;t.shadow=false;}
    next.textOverlays.append(t);if(commit(next,{},-1,"Add text",false,next.textOverlays.size()-1,-1)){transformMode_=false;emit compositionChanged();}
}
void EditorController::deleteText() {cancelPendingVisual();
    if(textDragBefore_)endTextDrag(true);
    if(selectedText_<0||selectedText_>=project_.textOverlays.size())return;
    auto next=project_;next.textOverlays.removeAt(selectedText_);commit(next,{},-1,"Delete text",false,-1);
}
void EditorController::updateText(int index,const QVariantMap &fields) {cancelPendingVisual();
    if(index<0||index>=project_.textOverlays.size())return;
    auto next=project_;auto &t=next.textOverlays[index];
    QString error;if(!setTextFields(t,fields,&error)){report(error);return;}
    commit(next,{},-1,"Edit text",false,index);
}
void EditorController::setTextTiming(int index,int start,int frames) {cancelPendingVisual();
    if(index<0||index>=project_.textOverlays.size()||start<0||frames<1||start>duration()-frames)return;
    auto next=project_;if(!next.setTextTiming(index,false,start,frames))return;
    if(textDragBefore_){
        project_=next;if(timeline_){preview_->requestSeek(position_,true,[timeline=timeline_.get(),texts=project_.renderedTexts()]{TextRendering::update(*timeline,texts);});}else if(preparing_)rebuild();emit projectChanged();emit stateChanged();
    }else commit(next,{},-1,"Change text timing",false,index);
}
void EditorController::chooseTextColor(){
    if(selectedText_<0)return;const int index=selectedText_;
    const auto color=QColorDialog::getColor(QColor(project_.textOverlays[index].color),nullptr,"Text color");
    if(color.isValid())updateText(index,{{"color",color.name()}});
}
void EditorController::previewTextContent(const QString &text){
    if(!textDragBefore_&&selectedText_>=0)beginTextDrag();
    if(!textDragBefore_||selectedText_<0||selectedText_>=project_.textOverlays.size()||text.size()>2000)return;
    project_.textOverlays[selectedText_].text=text;
    if(timeline_){preview_->requestSeek(position_,true,[timeline=timeline_.get(),texts=project_.renderedTexts()]{TextRendering::update(*timeline,texts);});}else if(preparing_)rebuild();emit projectChanged();emit stateChanged();
}
void EditorController::beginTextDrag(int index){cancelPendingVisual();
    if(textDragBefore_||index>=project_.textOverlays.size())return;
    const auto before=state();
    if(index>=0)selectText(index);
    if(selectedText_<0)return;
    preview_->pause(false);playWhenPrepared_=false;resumeWhenSettled_=false;endSkim();textDragBefore_=before;
}
void EditorController::dragTextTo(double x,double y){
    if(!textDragBefore_||!std::isfinite(x)||!std::isfinite(y))return;
    auto *selected=selectedTimedText(project_);if(!selected)return;auto &t=*selected;t.x=qBound(0.0,x,1.0);t.y=qBound(0.0,y,1.0);
    const QSize canvas=frame_.isNull()?QSize(960,540):frame_.size();const auto rect=TextRendering::bounds(t,canvas);
    t.x=rect.center().x()/canvas.width();t.y=rect.center().y()/canvas.height();
    if(timeline_){preview_->requestSeek(position_,true,[timeline=timeline_.get(),texts=project_.renderedTexts()]{TextRendering::update(*timeline,texts);});}else if(preparing_)rebuild();
    emit projectChanged();emit selectionChanged();
}
void EditorController::endTextDrag(bool cancel){
    if(!textDragBefore_)return;
    const auto after=state();const auto before=*textDragBefore_;textDragBefore_.reset();
    if(cancel){apply(before,position_);return;}
    if(sameProject(before.project,after.project)){emit stateChanged();return;}
    const bool caption=after.caption>=0;const int index=caption?after.caption:after.text;
    const auto &oldTexts=caption?before.project.captions:before.project.textOverlays;const auto &newTexts=caption?after.project.captions:after.project.textOverlays;
    const bool edited=index>=0&&index<oldTexts.size()&&index<newTexts.size()&&oldTexts[index].text!=newTexts[index].text;
    const QString label=caption?(edited?"Edit caption":"Move or trim caption"):(edited?"Edit text":"Move text");
    project_=before.project;selection_=before.selection;primary_=before.primary;selectedText_=before.text;selectedOverlay_=before.overlay;selectedCaption_=before.caption;captionSelection_=before.captions;
    commit(after.project,after.selection,after.primary,label,false,after.text,after.overlay,after.caption,after.captions);
}
QString EditorController::timeText() const { return clockText(position_)+" / "+clockText(duration()); }
QString EditorController::title() const { return (path_.isEmpty()?"Untitled":QFileInfo(path_).fileName())+(dirty()?" •":""); }
bool EditorController::playing() const { return sourceMode_?(sourcePreview_->isPlaying()||(sourcePreparing_&&sourcePlayWhenPrepared_)):(preview_->isPlaying()||(preparing_&&playWhenPrepared_)); }
int EditorController::splitCandidate() const {
    auto eligible=[this](int i){
        if(i<0||i>=project_.clips.size()) return false;
        int offset=position_-timelineData_.starts.value(i);
        return offset>timelineData_.transitions.value(i)&&offset<timelineData_.frames.value(i)-timelineData_.transitions.value(i+1);
    };
    if(selection_.contains(primary_)&&eligible(primary_)) return primary_;
    for(const auto &v:selection_) if(eligible(v.toInt())) return v.toInt();
    return -1;
}
bool EditorController::canSplit() const { return splitCandidate()>=0; }
void EditorController::report(const QString &message) { noticeCanUndo_=false;notice_=message; emit stateChanged(); }
void EditorController::dismissNotice() { noticeCanUndo_=false;notice_.clear(); emit stateChanged(); }

bool EditorController::addImported(Project &next,const QStringList &paths,bool append,QStringList &problems,const std::function<bool()> &canceled) {
    bool changed=false;
    const QSet<QString> video={"mp4","mov","m4v","mkv","avi","mts","webm"};
    const QSet<QString> audio={"wav","m4a","mp3","aac","flac","aiff","ogg"};
    for(const auto &input:paths) {
        if(canceled&&canceled())return false;
        QFileInfo file(input); const auto path=file.absoluteFilePath(); const auto suffix=file.suffix().toLower();
        if(!file.isFile() || (!video.contains(suffix)&&!audio.contains(suffix)) || (append&&!video.contains(suffix))) {
            problems.append(file.fileName()+": Not a supported "+(append?"video":"media")+" file.");continue;
        }
        const auto info=Media::retryMediaInfo(path);
        const bool hasVideo=info && info->width>0 && info->height>0;
        if(append && !hasVideo){problems.append(file.fileName()+": No video track; import it into Media as audio.");continue;}
        const int frames=info?qRound(info->durationSeconds*60):0;
        if(frames<1 || frames>86400*60) {problems.append(file.fileName()+": No usable media duration.");continue;}
        bool exists=false;for(const auto &a:next.assets)exists|=a.path==path;
        if(!exists){next.assets.append({path,hasVideo?"video":"audio",frames,0,0,frames});changed=true;}
        if(append){next.clips.append({path,0,frames/60.0,!next.clips.isEmpty()});changed=true;}
    }
    return changed;
}
bool EditorController::importMedia(const QStringList &paths) {cancelPendingVisual();
    if(textDragBefore_)endTextDrag(false);
    auto next=project_;QStringList problems;bool changed=false;
    if(addImported(next,paths,false,problems))changed=commit(next,selection_,primary_,"Import media");
    if(!problems.isEmpty())report(problems.join('\n'));
    if(changed && !next.assets.isEmpty())selectSource(next.assets.size()-1);
    return changed;
}
void EditorController::importMediaUrls(const QVariantList &urls){
    QStringList paths;for(const auto &v:urls){const auto url=v.toUrl();if(url.isLocalFile())paths.append(url.toLocalFile());}startImport(paths,false);
}
bool EditorController::importFiles(const QStringList &paths) {cancelPendingVisual();
    if(textDragBefore_)endTextDrag(false);
    auto next=project_;QStringList problems;bool changed=false;
    if(addImported(next,paths,true,problems)){showProject();changed=commit(next,{int(next.clips.size())-1},next.clips.size()-1,"Import clips",false,-1);}
    if(!problems.isEmpty())report(problems.join('\n'));
    return changed;
}
void EditorController::importUrls(const QVariantList &urls) {
    QStringList paths; for(const auto &v:urls) { auto url=v.toUrl(); if(url.isLocalFile()) paths.append(url.toLocalFile()); } startImport(paths,true);
}
void EditorController::selectClip(int index,int modifiers) {cancelPendingVisual();
    showProject();
    if(visualBefore_)endVisualGesture(false);
    if(index<0||index>=project_.clips.size()) return;
    if(textDragBefore_)endTextDrag(false);
    selectedOverlay_=-1;
    selectedText_=-1;selectedCaption_=-1;captionSelection_.clear();
    if(modifiers&Qt::ShiftModifier) { selectRange(anchor_<0?index:anchor_,index); primary_=index; }
    else if(modifiers&(Qt::ControlModifier|Qt::MetaModifier)) {
        if(selection_.contains(index)) selection_.removeAll(index); else selection_.append(index);
        selection_=validIndices(selection_,project_.clips.size()); primary_=selection_.contains(index)?index:(selection_.isEmpty()?-1:selection_.last().toInt()); anchor_=index;
    } else { selection_={index}; primary_=anchor_=index; }
    emit selectionChanged(); emit stateChanged();
}
void EditorController::clearSelection() {cancelPendingVisual(); selectedOverlay_=-1; if(textDragBefore_)endTextDrag(false);selectedText_=-1;selectedCaption_=-1;captionSelection_.clear();selection_.clear(); primary_=anchor_=-1; emit selectionChanged(); emit stateChanged(); }
void EditorController::selectAll() {cancelPendingVisual(); selectedOverlay_=-1; if(textDragBefore_)endTextDrag(false);selectedText_=-1;selectedCaption_=-1;captionSelection_.clear();selection_.clear(); for(int i=0;i<project_.clips.size();++i) selection_.append(i); primary_=selection_.isEmpty()?-1:0; anchor_=primary_; emit selectionChanged(); emit stateChanged(); }
void EditorController::selectRange(int first,int last) {cancelPendingVisual();
    if(textDragBefore_)endTextDrag(false);
    selectedOverlay_=-1;
    selectedText_=-1;selectedCaption_=-1;captionSelection_.clear();
    selection_.clear(); for(int i=std::max(0,std::min(first,last));i<=std::min(int(project_.clips.size())-1,std::max(first,last));++i) selection_.append(i);
    primary_=selection_.isEmpty()?-1:selection_.last().toInt(); emit selectionChanged(); emit stateChanged();
}
void EditorController::moveGroup(const QVariantList &input,int boundary) {cancelPendingVisual();
    const auto indices=validIndices(input,project_.clips.size()); if(indices.isEmpty()) return;
    boundary=qBound(0,boundary,int(project_.clips.size()));
    QVector<Clip> moved,remaining; QVector<int> order; int insertion=0;
    for(int i=0;i<project_.clips.size();++i) {
        if(indices.contains(i)) moved.append(project_.clips[i]);
        else { if(i<boundary) ++insertion; remaining.append(project_.clips[i]); order.append(i); }
    }
    Project next=project_; next.clips=remaining;
    QVariantList selected;
    for(int i=0;i<moved.size();++i) { next.clips.insert(insertion+i,moved[i]); order.insert(insertion+i,indices[i].toInt()); selected.append(insertion+i); }
    bool changed=false; for(int i=0;i<order.size();++i) changed |= order[i]!=i;
    next.normalizeVisuals();
    if(changed) commit(next,selected,insertion,"Reorder clips",true);
}
void EditorController::moveSelection(int direction) {cancelPendingVisual();
    if(selection_.isEmpty()) return;
    int first=selection_.first().toInt(),last=selection_.last().toInt();
    if(direction<0&&first>0) moveGroup(selection_,first-1);
    else if(direction>0&&last+1<project_.clips.size()) moveGroup(selection_,last+2);
}
void EditorController::removeSelection() {cancelPendingVisual();
    if(selectedOverlay_>=0){removeVisualOverlay();return;}
    if(selectedText_>=0){deleteText();return;}
    if(selectedCaption_>=0){deleteCaptions();return;}
    if(selection_.isEmpty())return;
    int connected=0;for(const auto &i:selection_){const auto id=project_.clips[i.toInt()].id;
        for(const auto &o:project_.visualOverlays)connected+=o.hostId==id;
        for(const auto *list:{&project_.textOverlays,&project_.captions})for(const auto &t:*list)connected+=t.hostId==id;
    }
    bool detach=false;
    if(connected){QMessageBox box(QMessageBox::Question,"Remove connected items",QString("Remove the selected clips and %1 attached visual, title or caption items? Detach keeps them at Project time.").arg(connected),QMessageBox::NoButton);
        auto *remove=box.addButton("Remove clips and attached items",QMessageBox::DestructiveRole);auto *keep=box.addButton("Detach, then remove clips",QMessageBox::ActionRole);box.addButton(QMessageBox::Cancel);box.exec();
        if(box.clickedButton()!=remove&&box.clickedButton()!=keep)return;detach=box.clickedButton()==keep;
    }
    removeSelectedHosts(detach);
}
bool EditorController::trimClip(int index,double in,double out) {cancelPendingVisual();
    if(index<0||index>=project_.clips.size()||!std::isfinite(in)||!std::isfinite(out)) return false;
    const auto original=project_.clips[index];
    const double maximumSeconds=std::max(original.outSeconds,sourceDuration(original.path));
    if(in<0||in>maximumSeconds||out>maximumSeconds+0.5/60||out<0) { report("Trim must stay inside the source."); return false; }
    const int first=qRound(in*60),last=qRound(out*60),maximum=qRound(maximumSeconds*60);
    if(first<0||last<=first||last>maximum) { report("Trim must stay inside the source and retain at least one frame."); return false; }
    Project next=project_; next.clips[index].inSeconds=first/60.0; next.clips[index].outSeconds=last/60.0;
    next.normalizeVisuals();
    return commit(next,selection_,primary_,"Trim clip");
}
void EditorController::setTrimStart(double s) { if(primary_>=0) trimClip(primary_,s,project_.clips[primary_].outSeconds); }
void EditorController::setTrimLength(double s) { if(primary_>=0) trimClip(primary_,project_.clips[primary_].inSeconds,project_.clips[primary_].inSeconds+s); }
void EditorController::nudgeTrim(int edge,int frames) {
    if(primary_<0) return; const auto c=project_.clips[primary_];
    trimClip(primary_,c.inSeconds+(edge==0?frames/60.0:0),c.outSeconds+(edge==1?frames/60.0:0));
}
void EditorController::split() {cancelPendingVisual();
    int i=splitCandidate(); if(i<0) { report("Select a clip and place the playhead inside it, away from ends and crossfades."); return; }
    Project next=project_; if(next.splitClip(i,position_-timelineData_.starts.value(i))) commit(next,{i+1},i+1,"Split clip");
}
void EditorController::rotate(int degrees) {cancelPendingVisual(); if(primary_<0) return; Project next=project_; if(next.rotateClip(primary_,degrees)) commit(next,selection_,primary_,"Rotate clip"); }
void EditorController::seek(int frame) {
    showProject();
    if(!timeline_&&!preparing_) return; skim_=false;
    position_=qBound(0,frame,std::max(0,duration()-1)); mediaCache_->setInteractive(true);
    if(!preparing_)preview_->requestSeek(position_); emit positionChanged();
}
void EditorController::step(int frames) { if(sourceMode_)stepSource(frames);else seek(position_+frames); }
void EditorController::beginScrub() {
    showProject();
    if(scrubbing_) return;
    scrubPosition_=position_; resumeAfterScrub_=playing()||resumeWhenSettled_;playWhenPrepared_=false; resumeWhenSettled_=false; scrubbing_=true; skim_=false;
    preview_->pause(false); mediaCache_->setInteractive(true);
}
void EditorController::finishScrub(bool cancel) {
    if(!scrubbing_) return; scrubbing_=false;
    if(preparing_){playWhenPrepared_=resumeAfterScrub_;resumeWhenSettled_=false;}else resumeWhenSettled_=resumeAfterScrub_; resumeAfterScrub_=false;
    if(cancel) position_=scrubPosition_;
    if(!preparing_)preview_->requestSeek(position_); emit positionChanged(); emit stateChanged();
}
void EditorController::skim(int frame) {
    if(sourceMode_||!timeline_||playing()||scrubbing_) return;
    skim_=true; mediaCache_->setInteractive(true); preview_->requestSeek(qBound(0,frame,std::max(0,duration()-1)),false);
}
void EditorController::endSkim() { if(!skim_) return; skim_=false; preview_->requestSeek(position_); }
void EditorController::togglePlayback() {
    if(sourceMode_){if(sourcePreparing_){sourcePlayWhenPrepared_=!sourcePlayWhenPrepared_;emit stateChanged();return;}if(sourcePreview_->isPlaying()){sourcePreview_->pause(false);sourcePreview_->requestSeek(sourcePosition());}else sourcePreview_->play();return;}
    if(preparing_){playWhenPrepared_=!playWhenPrepared_;emit stateChanged();return;}
    if(!timeline_) return; if(scrubbing_) finishScrub(true); endSkim(); resumeWhenSettled_=false;
    if(playing()){preview_->pause(false);preview_->requestSeek(position_);}else preview_->play();
}
void EditorController::undo() { if(visualBefore_)endVisualGesture(true);if(audioPreview_)endAudioGesture(true);if(textDragBefore_)endTextDrag(true);undo_.undo();scheduleRecovery(); }
void EditorController::redo() { if(visualBefore_)endVisualGesture(true);if(audioPreview_)endAudioGesture(true);if(textDragBefore_)endTextDrag(true);undo_.redo();scheduleRecovery(); }
void EditorController::setVoicePath(const QString &path) {cancelPendingVisual();
    if(!path.isEmpty()&&sourceDuration(path)<=0) { report("Could not read narration audio."); return; }
    auto next=project_; next.voicePath=path;next.voiceDurationFrames=qRound(sourceDuration(path)*60); commit(next,selection_,primary_,path.isEmpty()?"Clear narration":"Add narration");
}
void EditorController::setMusicPath(const QString &path) {cancelPendingVisual();
    if(!path.isEmpty()&&sourceDuration(path)<=0) { report("Could not read background music."); return; }
    auto next=project_; next.musicPath=path;next.musicDurationFrames=qRound(sourceDuration(path)*60); commit(next,selection_,primary_,path.isEmpty()?"Clear music":"Add music");
}
void EditorController::setVoiceStart(double s) {cancelPendingVisual(); if(!std::isfinite(s)||s<0||s>86400) return;updateAudio(1,{{"startFrame",qRound(s*60)}}); }
void EditorController::setMusicDb(double db) {cancelPendingVisual(); if(!std::isfinite(db))return;updateAudio(2,{{"gainDb",qBound(-60.0,db,12.0)}}); }
void EditorController::setCrossfade(double seconds) {
    if(!std::isfinite(seconds)||seconds<0) return;
    cancelPendingVisual();
    auto next=project_;next.crossfadeFrames=qRound(std::min(seconds,10.0)*Project::fps);
    for(auto &c:next.clips){c.hardCutBefore=false;c.transitionFrames=next.crossfadeFrames;c.transitionType="dissolve";}
    const bool changed=commit(next,selection_,primary_,"Crossfade duration");
    if(!changed&&!sameProject(next,project_))return;
    const int cuts=std::max(0,int(project_.clips.size())-1);
    int shortened=0;for(int i=1;i<project_.clips.size();++i)shortened+=project_.transitionFramesBefore(i)<project_.crossfadeFrames;
    const auto count=QString("%1 existing %2").arg(cuts).arg(cuts==1?"cut":"cuts");
    QString message=project_.crossfadeFrames==0
        ?QString("%1 crossfades from %2.").arg(changed?"Removed":"Already removed",count)
        :QString("%1 %2 s crossfades to %3.").arg(changed?"Applied":"Already applied",QString::number(project_.crossfadeFrames/double(Project::fps),'g',4),count);
    if(shortened>0)message+=QString(" %1 shortened to fit nearby clips.").arg(shortened);
    report(message);noticeCanUndo_=changed;emit stateChanged();
}
int EditorController::audioPlayableFrames(const Project &p,int lane) const {
    if(lane==0)return primary_>=0&&primary_<p.clips.size()?p.clipFrames(p.clips[primary_]):0;
    if(lane==1)return std::max(0,std::min(qRound(sourceDuration(p.voicePath)*60),p.durationFrames()-qRound(p.voiceStartSeconds*60)));
    return p.musicLoopToEnd?p.durationFrames():std::min(p.durationFrames(),qRound(sourceDuration(p.musicPath)*60));
}
QVariantMap EditorController::currentAudio() const {
    const auto &p=visibleProject();const auto a=p.audioControls(audioLane_,primary_);
    const bool enabled=audioLane_==0?primary_>=0&&primary_<p.clips.size():!(audioLane_==1?p.voicePath:p.musicPath).isEmpty();
    return {{"gainDb",a.gainDb},{"muted",a.muted},{"fadeInFrames",a.fadeInFrames},{"fadeOutFrames",a.fadeOutFrames},
        {"startFrame",qRound(p.voiceStartSeconds*60)},{"loopToEnd",p.musicLoopToEnd},{"playableFrames",audioPlayableFrames(p,audioLane_)},{"enabled",enabled}};
}
void EditorController::selectAudio(int lane){cancelPendingVisual();showProject();if(lane<0||lane>2)return;if(audioPreview_)endAudioGesture(false);audioLane_=lane;transitionIndex_=-1;emit stateChanged();}
void EditorController::beginAudioGesture(){cancelPendingVisual();
    showProject();
    if(visualBefore_)endVisualGesture(false);
    if(audioPreview_)return;if(textDragBefore_)endTextDrag(false);endSkim();audioGestureWasPlaying_=playing();audioPreview_=project_;
}
void EditorController::presentAudioEdit(const Project &next,const QString &label){
    if(audioPreview_){*audioPreview_=next;const bool resume=playing()||playWhenPrepared_;rebuild();playWhenPrepared_|=resume;emit stateChanged();}
    else commit(next,selection_,primary_,label);
}
void EditorController::endAudioGesture(bool cancel){
    if(!audioPreview_)return;const auto after=*audioPreview_;audioPreview_.reset();
    const bool resume=audioGestureWasPlaying_||playing();audioGestureWasPlaying_=false;
    if(cancel||sameProject(after,project_)){rebuild();playWhenPrepared_|=resume;emit stateChanged();return;}
    commit(after,selection_,primary_,"Edit audio or transition");playWhenPrepared_|=resume;
}
void EditorController::updateAudio(int lane,const QVariantMap &fields){cancelPendingVisual();
    if(lane<0||lane>2||(lane==0&&(primary_<0||primary_>=project_.clips.size())))return;
    auto next=visibleProject();auto a=next.audioControls(lane,primary_);
    for(auto it=fields.cbegin();it!=fields.cend();++it){
        const auto key=it.key();const auto value=it.value();
        if(key=="muted"||key=="loopToEnd"){
            if(value.metaType().id()!=QMetaType::Bool){if(audioPreview_)endAudioGesture(true);report("Audio switches need true or false.");return;}
            if(key=="muted")a.muted=value.toBool();else if(lane==2)next.musicLoopToEnd=value.toBool();else return;
        }else{
            bool ok=false;const double n=value.toDouble(&ok);
            if(!ok||!std::isfinite(n)||n < -60||n>86400*60){if(audioPreview_)endAudioGesture(true);report("Invalid audio value.");return;}
            if(key=="gainDb")a.gainDb=qBound(-60.0,n,12.0);
            else if(n<0||n!=std::floor(n)){if(audioPreview_)endAudioGesture(true);report("Audio timing uses whole frames.");return;}
            else if(key=="startFrame"&&lane==1)next.voiceStartSeconds=n/60;
            else if(key=="fadeInFrames")a.fadeInFrames=int(n);
            else if(key=="fadeOutFrames")a.fadeOutFrames=int(n);
            else {if(audioPreview_)endAudioGesture(true);report("Unknown audio control.");return;}
        }
    }
    const int playable=audioPlayableFrames(next,lane);
    a.fadeInFrames=std::min(a.fadeInFrames,playable);a.fadeOutFrames=std::min(a.fadeOutFrames,playable);
    next.setAudioControls(lane,a,primary_);presentAudioEdit(next,"Edit audio");
}
void EditorController::setSolo(int lane){
    if(lane < -1||lane>2||(lane==0&&primary_<0))return;
    soloLane_=soloLane_==lane?-1:lane;soloClip_=primary_;rebuild();emit stateChanged();
}
void EditorController::resetClipping(){masterClipped_=false;emit audioLevelsChanged();}
QVariantList EditorController::transitionPresets() const {
    QVariantList list;
    for (const auto &preset : Transitions::presets()) list.append(QVariantMap{{"id",preset.id},{"name",preset.name}});
    return list;
}
QVariantList EditorController::transitions() const {
    QVariantList list;const auto &p=visibleProject();
    for(int i=1;i<p.clips.size();++i){
        const int frames=p.transitionFramesBefore(i),requested=p.clips[i].transitionFrames<0?p.crossfadeFrames:p.clips[i].transitionFrames;
        // A short clip can leave no overlap while retaining its chosen mask preset.
        const auto &clip=p.clips[i];
        const bool cut=clip.hardCutBefore||clip.transitionType=="cut"||(frames==0&&clip.transitionType=="dissolve");
        list.append(QVariantMap{{"index",i},{"name",QString("Cut %1 → %2").arg(i).arg(i+1)},
            {"type",cut?QString("cut"):clip.transitionType},{"frames",frames},{"requestedFrames",requested},
            {"shortened",!p.clips[i].hardCutBefore&&p.clips[i].transitionType!="cut"&&frames<requested}});
    }
    return list;
}
QVariantMap EditorController::currentTransition() const {const auto list=transitions();return transitionIndex_>0&&transitionIndex_<=list.size()?list[transitionIndex_-1].toMap():QVariantMap{};}
void EditorController::selectTransition(int index){cancelPendingVisual();showProject();if(index<1||index>=project_.clips.size())return;transitionIndex_=index;emit stateChanged();}
void EditorController::setTransition(int index,const QString &type,int frames){cancelPendingVisual();
    auto next=audioPreview_?project_:visibleProject();
    if(!next.setTransition(index,type,frames)){
        if(audioPreview_)presentAudioEdit(next,"Change transition");
        if(index>0&&index<next.clips.size()&&Transitions::find(type)&&type!="cut"&&frames>next.transitionFramesBefore(index))report(QString("Transition remains at %1 frames; neighboring media leaves no more playable overlap.").arg(next.transitionFramesBefore(index)));
        return;
    }
    const int effective=next.transitionFramesBefore(index);transitionIndex_=index;presentAudioEdit(next,"Change transition");
    if(type!="cut"&&effective<frames)report(QString("Transition clamped to %1 frames to preserve playable media and its neighboring cut.").arg(effective));
}
void EditorController::removeTransition(){setTransition(transitionIndex_,"cut",0);}

QVariantList EditorController::recentMusic() const {
    QVariantList result; for(const auto &path:Media::recentAudioFiles(QDir::home().filePath("Downloads"))) result.append(QVariantMap{{"name",QFileInfo(path).fileName()},{"path",path}}); return result;
}
void EditorController::browseMusic() { QDesktopServices::openUrl(QUrl("https://mixkit.co/free-stock-music/")); }
void EditorController::chooseVoice() { auto p=QFileDialog::getOpenFileName(nullptr,"Choose narration",moviesDirectory(),audioFilter()); if(!p.isEmpty()) chooseAudioAsync(p,false); }
void EditorController::chooseMusic() { auto p=QFileDialog::getOpenFileName(nullptr,"Choose background music",moviesDirectory(),audioFilter()); if(!p.isEmpty()) chooseAudioAsync(p,true); }
void EditorController::importDialog() { startImport(QFileDialog::getOpenFileNames(nullptr,"Import to Media",moviesDirectory(),"Media files (*.mp4 *.mov *.m4v *.mkv *.avi *.mts *.webm *.wav *.m4a *.mp3 *.aac *.flac *.aiff *.ogg);;All files (*)"),false); }
void EditorController::importAndAppendDialog() { startImport(QFileDialog::getOpenFileNames(nullptr,"Import and append",moviesDirectory(),"Video files (*.mp4 *.mov *.m4v *.mkv *.avi *.mts *.webm);;All files (*)"),true); }
bool EditorController::saveTo(const QString &path) {
    if(path.isEmpty()) return false;
    if(audioPreview_)endAudioGesture(false);
    if(visualBefore_)endVisualGesture(false);
    if(textDragBefore_)endTextDrag(false);
    QString error; if(!saveProject(project_,path,&error)) { report(error); return false; }
    path_=path; sourceViewDirty_=false; undo_.setClean(); if(recovery_)recovery_->clear(); dismissNotice(); return true;
}
bool EditorController::loadFrom(const QString &path) {
    if(visualBefore_)endVisualGesture(true);
    QString error;auto loaded=loadProject(path,&error);if(!loaded){report(error);return false;}
    applyLoadedProject(*loaded,path,{});return true;
}
void EditorController::applyLoadedProject(Project loaded,const QString &path,const QHash<QString,double> &durations){
    if(visualBefore_)endVisualGesture(true);
    if(textDragBefore_)endTextDrag(true);
    if(audioPreview_)endAudioGesture(true);
    soloLane_=soloClip_=-1;transitionIndex_=-1;masterClipped_=false;masterPeak_=-60;emit audioLevelsChanged();
    showProject();selectedSource_=-1;sourceViewDirty_=false;
    durations_=durations;restoreAudioDurations(loaded);path_=path;undo_.clear();
    apply({loaded,loaded.clips.isEmpty()?QVariantList{}:QVariantList{0},loaded.clips.isEmpty()?-1:0},0);
    undo_.setClean();if(recovery_)recovery_->clear();
}

bool EditorController::saveDialog(bool choosePath) {
    QString path=path_;
    if(path.isEmpty()||choosePath) {
        path=QFileDialog::getSaveFileName(nullptr,"Save project",QDir(moviesDirectory()).filePath(QFileInfo(path).fileName()),"IsaacVideoEditor projects (*.isaac.json)");
        if(path.isEmpty()) return false;
        if(!path.endsWith(".isaac.json",Qt::CaseInsensitive)) { if(path.endsWith(".json",Qt::CaseInsensitive)) path.chop(5); path+=".isaac.json"; }
    }
    return saveTo(path);
}
bool EditorController::save() { return saveDialog(false); }
bool EditorController::saveAs() { return saveDialog(true); }
bool EditorController::maybeSave() {
    cancelPendingVisual();
    if(audioPreview_)endAudioGesture(false);
    if(textDragBefore_)endTextDrag(false);
    if(!dirty()) return true;
    auto result=QMessageBox::question(nullptr,"Save project?","Save your changes before continuing?",QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
    return result==QMessageBox::Discard||(result==QMessageBox::Save&&save());
}

void EditorController::newProject() {
    if(visualBefore_)endVisualGesture(true);
    if(!maybeSave())return;
    cancelImport();
    soloLane_=soloClip_=-1;transitionIndex_=-1;masterClipped_=false;masterPeak_=-60;emit audioLevelsChanged();
    showProject();selectedSource_=-1;sourceViewDirty_=false;path_.clear();undo_.clear();apply({},0);undo_.setClean();if(recovery_)recovery_->clear();
}
void EditorController::openProject() {
    if(!maybeSave()) return;
    auto p=QFileDialog::getOpenFileName(nullptr,"Open project",moviesDirectory(),"IsaacVideoEditor projects (*.isaac.json);;JSON files (*.json)"); if(!p.isEmpty()) loadProjectAsync(p);
}
void EditorController::rebuild() {
    QScopedValueRollback<bool> guard(rebuilding_,true);
    if(preparing_)++supersededJobs_;
    const quint64 revision=++preparationRevision_;
    preparationPool_.clear();
    playWhenPrepared_|=preview_->isPlaying();
    preview_->setProducer(nullptr,false);
    struct Prepared {
        QVector<std::shared_ptr<void>> leases;
        std::unique_ptr<Mlt::Profile> profile;
        std::unique_ptr<Mlt::Tractor> timeline;
        QVector<PreviewCache::Request> requests;
        QHash<int,QString> clips,overlays;
        QHash<QString,double> durations;
        QHash<QString,QString> identities;
        QString error,format;
        bool proxies=false,fallback=false;
        qint64 metadataMs=0,buildMs=0;
    };
    auto retired=std::make_shared<Prepared>();retired->leases=std::move(timelineProxyLeases_);retired->profile=std::move(preparedProfile_);retired->timeline=std::move(timeline_);
    const Project renderProject=visibleProject();preparationSnapshot_=renderProject;const auto data=renderProject.timelineData();
    const int desiredPosition=position_;
    proxyPriorityStart_=proxyPriorityEnd_=-1;proxyPending_=false;
    QSet<QString> retained;for(const auto &asset:project_.assets)retained.insert(asset.path);
    for(const auto &clip:renderProject.clips)retained.insert(clip.path);
    for(const auto &overlay:renderProject.visualOverlays)retained.insert(overlay.path);
    if(!renderProject.voicePath.isEmpty())retained.insert(renderProject.voicePath);
    if(!renderProject.musicPath.isEmpty())retained.insert(renderProject.musicPath);
    mediaCache_->retainSources(retained.values());
    if(renderProject.clips.isEmpty()) {
        preparing_=false;playWhenPrepared_=false;
        frame_=QImage();position_=0;previewStatus_="No media";format_="Auto resolution · 60 fps";
        proxyPaths_.clear();overlayProxyPaths_.clear();proxyCache_->setRequests({});
        emit frameChanged();emit stateChanged();return;
    }
    Project view=renderProject;auto prepared=std::make_shared<Prepared>();
    for(int row=0;row<view.clips.size();++row){
        auto &clip=view.clips[row];const auto copy=proxyCache_->readyCopy(clip.path,qRound(clip.inSeconds*60),qRound(clip.outSeconds*60));
        if(!copy.path.isEmpty()){clip.sourceFrameOffset=copy.startFrame;clip.path=copy.path;clip.inSeconds-=copy.startFrame/60.0;clip.outSeconds-=copy.startFrame/60.0;prepared->proxies=true;prepared->leases.append(copy.lease);}
        prepared->clips[row]=clip.path;
    }
    for(int row=0;row<view.visualOverlays.size();++row){
        auto &overlay=view.visualOverlays[row];
        if(view.visualLaneVisible[overlay.lane]){
            const auto copy=proxyCache_->readyCopy(overlay.path,overlay.sourceInFrame,overlay.sourceInFrame+view.overlayFrames(row));
            if(!copy.path.isEmpty()){overlay.path=copy.path;overlay.sourceFrameOffset=copy.startFrame;overlay.sourceInFrame-=copy.startFrame;prepared->proxies=true;prepared->leases.append(copy.lease);}
        }
        prepared->overlays[row]=overlay.path;
    }
    const int soloLane=soloLane_,soloClip=soloClip_;
    const auto budget=performanceBudget_;
    ++preparationJobs_;preparationClock_.start();
    preparing_=true;previewStatus_="Preparing preview…";emit stateChanged();
    preparationPool_.start(QRunnable::create([this,prepared,retired,renderProject,view=std::move(view),data,desiredPosition,soloLane,soloClip,budget,revision]() mutable {
        const auto canceled=[this,revision]{return revision!=preparationRevision_.load();};
        if(canceled())return;
        for(const auto &clip:renderProject.clips)prepared->identities[clip.path]=SourceTiming::fingerprint(clip.path);
        for(const auto &overlay:renderProject.visualOverlays)prepared->identities[overlay.path]=SourceTiming::fingerprint(overlay.path);
        if(!renderProject.voicePath.isEmpty())prepared->identities[renderProject.voicePath]=SourceTiming::fingerprint(renderProject.voicePath);
        if(!renderProject.musicPath.isEmpty())prepared->identities[renderProject.musicPath]=SourceTiming::fingerprint(renderProject.musicPath);
        QElapsedTimer stage;stage.start();
        QHash<QString,std::optional<Media::MediaInfo>> metadata;
        for(const auto &clip:renderProject.clips){
            if(canceled())return;
            if(!metadata.contains(clip.path))metadata[clip.path]=Media::retryMediaInfo(clip.path);
        }
        const auto output=Media::projectOutputSize(renderProject,&prepared->error,true);
        if(output&&!canceled()){
            prepared->format=QString("%1×%2 · 60 fps").arg(output->width).arg(output->height);
            const double scale=std::min({1.0,double(std::clamp(budget.previewWidth,2,3840))/output->width,double(std::clamp(budget.previewHeight,2,2160))/output->height});
            const int width=std::max(2,qRound(output->width*scale/2)*2),height=std::max(2,qRound(output->height*scale/2)*2);
            Mlt::Tractor *previous=nullptr;
            if(retired->profile&&retired->profile->width()==width&&retired->profile->height()==height){
                prepared->profile=std::move(retired->profile);previous=retired->timeline.get();
            }else prepared->profile=std::make_unique<Mlt::Profile>();
            Media::configureOutputProfile(*prepared->profile,width,height);
            struct Range {PreviewCache::Request request;int distance;};QVector<Range> ranges;
            const auto addRange=[&](const QString &path,int in,int out,int start,int frames){
                if(frames<1||canceled())return;
                if(!metadata.contains(path))metadata[path]=Media::retryMediaInfo(path);
                const auto &info=metadata[path];
                if(!info||qint64(info->width)*info->height<=1920*1080)return;
                const int end=start+frames;
                ranges.append({{path,qRound(info->durationSeconds*60),in,out},desiredPosition<start?start-desiredPosition:desiredPosition<end?0:desiredPosition-end});
            };
            for(int row=0;row<renderProject.visualOverlays.size()&&!canceled();++row){const auto &overlay=renderProject.visualOverlays[row];const int frames=renderProject.overlayFrames(row);if(renderProject.visualLaneVisible[overlay.lane]&&!QImageReader(overlay.path).canRead())addRange(overlay.path,overlay.sourceInFrame,overlay.sourceInFrame+frames,renderProject.overlayStartFrame(row),frames);}
            for(int row=0;row<renderProject.clips.size()&&!canceled();++row){const auto &clip=renderProject.clips[row];addRange(clip.path,qRound(clip.inSeconds*60),qRound(clip.outSeconds*60),data.starts[row],data.frames[row]);}
            std::stable_sort(ranges.begin(),ranges.end(),[](const Range &a,const Range &b){return a.distance<b.distance;});
            for(const auto &range:ranges)prepared->requests.append(range.request);
            const auto applySolo=[soloLane,soloClip](Project &mix){if(soloLane>=0){for(int i=0;i<mix.clips.size();++i)if(soloLane!=0||i!=soloClip)mix.clips[i].audio.muted=true;if(soloLane!=1)mix.voiceAudio.muted=true;if(soloLane!=2)mix.musicMuted=true;}};
            for(auto it=metadata.cbegin();it!=metadata.cend();++it)if(it.value())prepared->durations[it.key()]=it.value()->durationSeconds;
            applySolo(view);prepared->metadataMs=stage.elapsed();stage.restart();
            if(!canceled())prepared->timeline=Media::buildTimeline(view,*prepared->profile,&prepared->error,true,previous);
            if(!prepared->timeline&&prepared->proxies&&!canceled()){
                Project original=renderProject;applySolo(original);
                prepared->timeline=Media::buildTimeline(original,*prepared->profile,&prepared->error,true,previous);prepared->fallback=true;
            }
            prepared->buildMs=stage.elapsed();
        }
        retired->timeline.reset();retired->profile.reset();retired->leases.clear();
        if(canceled())return;
        QMetaObject::invokeMethod(this,[this,prepared,renderProject,revision]{
            if(revision!=preparationRevision_.load())return;
            for(auto it=prepared->identities.cbegin();it!=prepared->identities.cend();++it)if(SourceTiming::fingerprint(it.key())!=it.value()){rebuild();return;}
            QScopedValueRollback<bool> guard(rebuilding_,true);preparing_=false;preparationMs_=preparationClock_.elapsed();metadataMs_=prepared->metadataMs;timelineBuildMs_=prepared->buildMs;
            if(!prepared->timeline){playWhenPrepared_=false;report(prepared->error.isEmpty()?"Could not preview this project.":prepared->error);return;}
            preparedProfile_=std::move(prepared->profile);timeline_=std::move(prepared->timeline);timelineProxyLeases_=std::move(prepared->leases);
            proxyPaths_=prepared->clips;overlayProxyPaths_=prepared->overlays;
            if(prepared->fallback){
                for(int i=0;i<renderProject.clips.size();++i){const auto &clip=renderProject.clips[i];if(proxyPaths_[i]!=clip.path)proxyCache_->discard(clip.path);proxyPaths_[i]=clip.path;}
                for(int i=0;i<renderProject.visualOverlays.size();++i){const auto &overlay=renderProject.visualOverlays[i];if(overlayProxyPaths_[i]!=overlay.path)proxyCache_->discard(overlay.path);overlayProxyPaths_[i]=overlay.path;}
            }
            if(!preview_->setProducer(timeline_.get(),false)){
                playWhenPrepared_=false;
                if(prepared->proxies&&!prepared->fallback){for(const auto &clip:renderProject.clips)proxyCache_->discard(clip.path);for(const auto &overlay:renderProject.visualOverlays)proxyCache_->discard(overlay.path);rebuild();}
                else report("Could not connect the project preview.");
                return;
            }
            for(auto it=prepared->durations.cbegin();it!=prepared->durations.cend();++it)durations_[it.key()]=it.value();
            format_=prepared->format;
            proxyCache_->setRequests(prepared->requests);
            const bool allReady=std::all_of(prepared->requests.cbegin(),prepared->requests.cend(),[this](const PreviewCache::Request &request){return !proxyCache_->readyCopy(request.source,request.inFrame,request.outFrame).path.isEmpty();});
            previewStatus_=prepared->fallback?"Using original media":prepared->requests.isEmpty()?"Original media":allReady?"Smooth preview":proxyCache_->isBusy()?"Preparing smooth preview…":"Using original media";
            const int settled=qBound(0,position_,std::max(0,duration()-1));
            if(position_!=settled){position_=settled;emit positionChanged();}
            resumeWhenSettled_=playWhenPrepared_&&!sourceMode_&&!exporting();playWhenPrepared_=false;
            if(!sourceMode_)preview_->requestSeek(settled);mediaCache_->setInteractive(false);emit sourceMetadataChanged();emit selectionChanged();emit stateChanged();
        },Qt::QueuedConnection);
    }));
}
void EditorController::exportDialog(bool optimizeForIphone) {
    if(exporting()||project_.clips.isEmpty()) return;
    auto p=QFileDialog::getSaveFileName(nullptr,"Export MP4 · "+exportFormat(optimizeForIphone),moviesDirectory(),"MP4 video (*.mp4)");
    if(p.isEmpty()) return; if(!p.endsWith(".mp4",Qt::CaseInsensitive)) p+=".mp4"; exportTo(p,optimizeForIphone);
}

void EditorController::attachTimeline(QObject *object) {
    auto *timeline=qobject_cast<QuickTimeline*>(object); if(!timeline) return;
    auto refresh=[this,timeline]{if(!audioPreview_)timeline->setProject(project_,sourceDurations(),voiceDuration(),sourceDuration(project_.musicPath));};
    connect(this,&EditorController::projectChanged,timeline,refresh);
    connect(this,&EditorController::sourceMetadataChanged,timeline,refresh);
    refresh();
}

void EditorController::refreshPendingProxy() {
    if(!proxyPending_||exporting()||preview_->isSeeking()||scrubbing_||skim_||visualBefore_) return;
    proxyPending_=false;
    const bool resume=playing()||resumeWhenSettled_;
    resumeWhenSettled_=false;
    rebuild();
    playWhenPrepared_|=resume;
}

void EditorController::retryPreviews() {
    QStringList paths;
    for(const auto &clip:project_.clips) if(!paths.contains(clip.path)) paths.append(clip.path);
    for(const auto &overlay:project_.visualOverlays) if(!paths.contains(overlay.path)) paths.append(overlay.path);
    if(!project_.voicePath.isEmpty()) paths.append(project_.voicePath);
    if(!project_.musicPath.isEmpty()) paths.append(project_.musicPath);
    for(const auto &path:paths) mediaCache_->retry(path);
    if(!project_.clips.isEmpty())rebuild();
}

void EditorController::prioritizeProxy(int frame) {
    if(frame>=proxyPriorityStart_&&frame<proxyPriorityEnd_) return;
    proxyPriorityStart_=frame;proxyPriorityEnd_=duration();
    for(int row=0;row<project_.clips.size();++row) {
        const auto &clip=project_.clips[row]; const int start=timelineData_.starts.value(row),end=start+project_.clipFrames(clip);
        if(frame>=start&&frame<end) {
            proxyPriorityEnd_=end;
            proxyCache_->prioritize(clip.path,qRound(clip.inSeconds*60),qRound(clip.outSeconds*60));break;
        }
    }
    for(int row=0;row<project_.visualOverlays.size();++row){
        const auto &o=project_.visualOverlays[row];if(!project_.visualLaneVisible[o.lane])continue;
        const int start=project_.overlayStartFrame(row),frames=project_.overlayFrames(row),end=start+frames;
        if(start>frame)proxyPriorityEnd_=std::min(proxyPriorityEnd_,start);
        if(frame>=start&&frame<end){proxyPriorityEnd_=std::min(proxyPriorityEnd_,end);proxyCache_->prioritize(o.path,o.sourceInFrame,o.sourceInFrame+frames);}
    }
}

QVariantMap EditorController::sourceAsset(int index) const {
    if(index<0||index>=project_.assets.size())return {};
    const auto &a=project_.assets[index];
    return {{"index",index},{"path",a.path},{"name",QFileInfo(a.path).fileName()},
        {"kind",a.kind},{"durationFrames",a.durationFrames},{"durationText",clockText(a.durationFrames)},
        {"cursorFrame",a.cursorFrame},{"inFrame",a.inFrame},{"outFrame",a.outFrame},
        {"rangeFrames",std::max(0,a.outFrame-a.inFrame)},{"validRange",a.outFrame>a.inFrame},
        {"uses",timelineData_.sourceUses.value(a.path)},{"offline",!QFileInfo::exists(a.path)}};
}
QVariantList EditorController::assets() const {
    QVariantList result;result.reserve(project_.assets.size());
    for(int i=0;i<project_.assets.size();++i)result.append(sourceAsset(i));
    return result;
}
QVariantList EditorController::filteredAssets(const QString &search,const QString &kind,const QString &usage) const {
    QVariantList result;
    for(const auto &v:assets()){
        const auto a=v.toMap();const int uses=a["uses"].toInt();
        if(!a["name"].toString().contains(search,Qt::CaseInsensitive))continue;
        if(kind!="all" && a["kind"].toString()!=kind)continue;
        if((usage=="used"&&uses==0)||(usage=="unused"&&uses>0))continue;
        result.append(v);
    }
    return result;
}
QVariantMap EditorController::currentSource() const {
    return sourceAsset(selectedSource_);
}
int EditorController::sourcePosition() const {
    if(sourceScrubbing_)return sourceScrubFrame_;
    return selectedSource_>=0&&selectedSource_<project_.assets.size()?project_.assets[selectedSource_].cursorFrame:0;
}
QString EditorController::sourceTimeText() const {
    return clockText(sourcePosition())+" / "+clockText(selectedSource_>=0&&selectedSource_<project_.assets.size()?project_.assets[selectedSource_].durationFrames:0);
}
void EditorController::selectSource(int index) {cancelPendingVisual();
    finishSourceScrub(true);
    if(index<0||index>=project_.assets.size())return;
    if(textDragBefore_)endTextDrag(false);
    preview_->pause(false);playWhenPrepared_=false;resumeWhenSettled_=false;endSkim();
    // Legacy files know the last used frame; discover the full source when it is available.
    auto &asset=project_.assets[index];const int completeFrames=qRound(sourceDuration(asset.path)*60);
    if(completeFrames>asset.durationFrames && completeFrames<=86400*60){
        if(asset.inFrame==0 && asset.outFrame==asset.durationFrames)asset.outFrame=completeFrames;
        asset.durationFrames=completeFrames;emit projectChanged();
    }
    selectedSource_=index;sourceMode_=true;rebuildSource();emit sourceChanged();emit stateChanged();
}
void EditorController::rebuildSource() {
    if(sourcePreparing_)++supersededJobs_;
    const quint64 revision=++sourceRevision_;sourcePool_.clear();
    sourcePlayWhenPrepared_|=sourcePreview_->isPlaying();sourcePreview_->setProducer(nullptr,false);sourceUsesProxy_=false;
    struct PreparedSource {
        std::shared_ptr<void> lease;
        std::unique_ptr<Mlt::Profile> profile;
        std::unique_ptr<Mlt::Producer> producer;
        int completeFrames=0;bool discardedProxy=false,usesProxy=false;QString identity;
    };
    auto retired=std::make_shared<PreparedSource>();retired->lease=std::move(sourceProxyLease_);retired->profile=std::move(preparedSourceProfile_);retired->producer=std::move(sourceProducer_);
    if(!sourceMode_||selectedSource_<0||selectedSource_>=project_.assets.size()){sourcePreparing_=false;return;}
    const MediaAsset asset=project_.assets[selectedSource_];
    if(!QFileInfo::exists(asset.path)){sourcePreparing_=false;frame_=QImage();emit frameChanged();report("Source is offline. Locate its original file to preview it.");return;}
    const auto copy=proxyCache_->readyCopy(asset.path,0,asset.durationFrames);
    QStringList candidates;if(!copy.path.isEmpty())candidates.append(copy.path);candidates.append(asset.path);
    auto prepared=std::make_shared<PreparedSource>();prepared->lease=copy.lease;const auto budget=performanceBudget_;
    ++sourceJobs_;sourceClock_.start();sourcePreparing_=true;emit stateChanged();
    sourcePool_.start(QRunnable::create([this,retired,prepared,asset,candidates,budget,revision]{
        retired->producer.reset();retired->profile.reset();retired->lease.reset();
        const auto canceled=[this,revision]{return revision!=sourceRevision_.load();};if(canceled())return;
        prepared->identity=SourceTiming::fingerprint(asset.path);const auto info=Media::retryMediaInfo(asset.path);prepared->completeFrames=info?qRound(info->durationSeconds*60):0;
        const int frames=std::clamp(std::max(asset.durationFrames,prepared->completeFrames),1,86400*60);
        prepared->profile=std::make_unique<Mlt::Profile>();Media::configureOutputProfile(*prepared->profile,std::clamp(budget.previewWidth,2,3840),std::clamp(budget.previewHeight,2,2160));
        for(const auto &path:candidates){
            if(canceled())return;
            auto producer=std::make_unique<Mlt::Producer>(*prepared->profile,"avformat",path.toUtf8().constData());
            const bool proxy=path!=asset.path;
            if(producer->is_valid()&&(!proxy||producer->get_length()>=frames)){
                producer->set("eof","pause");producer->set("length",frames);producer->set_in_and_out(0,frames-1);
                Media::setSourceFiles(*producer,{asset.path});prepared->usesProxy=proxy;prepared->producer=std::move(producer);break;
            }
            prepared->discardedProxy|=proxy;
        }
        if(canceled())return;
        QMetaObject::invokeMethod(this,[this,prepared,asset,revision]{
            if(revision!=sourceRevision_.load()||!sourceMode_||selectedSource_<0||selectedSource_>=project_.assets.size()||project_.assets[selectedSource_].path!=asset.path)return;
            if(SourceTiming::fingerprint(asset.path)!=prepared->identity){rebuildSource();return;}
            sourcePreparing_=false;sourcePreparationMs_=sourceClock_.elapsed();
            if(prepared->discardedProxy)proxyCache_->discard(asset.path);
            if(!prepared->producer){sourcePlayWhenPrepared_=false;report("Could not open source preview.");return;}
            auto &current=project_.assets[selectedSource_];
            if(prepared->completeFrames>current.durationFrames&&prepared->completeFrames<=86400*60){
                if(current.inFrame==0&&current.outFrame==current.durationFrames)current.outFrame=prepared->completeFrames;
                current.durationFrames=prepared->completeFrames;durations_[asset.path]=current.durationFrames/60.0;emit projectChanged();emit sourceChanged();
            }
            preparedSourceProfile_=std::move(prepared->profile);sourceProducer_=std::move(prepared->producer);sourceProxyLease_=std::move(prepared->lease);sourceUsesProxy_=prepared->usesProxy;
            if(!sourcePreview_->setProducer(sourceProducer_.get(),false)){
                sourcePlayWhenPrepared_=false;
                if(!prepared->discardedProxy&&!proxyCache_->readyCopy(asset.path,0,asset.durationFrames).path.isEmpty()){proxyCache_->discard(asset.path);rebuildSource();}
                else report("Could not open source preview.");
                return;
            }
            sourcePreview_->requestSeek(sourcePosition());emit stateChanged();
        },Qt::QueuedConnection);
    }));
}
void EditorController::showProject() {
    finishSourceScrub(true);
    if(!sourceMode_)return;
    sourceMode_=false;++sourceRevision_;sourcePool_.clear();sourcePreparing_=false;sourcePlayWhenPrepared_=false;sourcePreview_->pause(false);
    if(timeline_)preview_->requestSeek(position_);
    else {frame_=QImage();emit frameChanged();}
    emit sourceChanged();emit stateChanged();
}
void EditorController::seekSource(int frame) {
    if(selectedSource_<0||selectedSource_>=project_.assets.size())return;
    if(!sourceMode_)selectSource(selectedSource_);
    auto &a=project_.assets[selectedSource_];const int next=qBound(0,frame,a.durationFrames-1);
    if(sourceScrubbing_)sourceScrubFrame_=next;
    else if(next!=a.cursorFrame){a.cursorFrame=next;sourceViewDirty_=true;}
    sourcePreview_->pause(false);if(sourceProducer_)sourcePreview_->requestSeek(next);
    emit sourceChanged();emit stateChanged();
}
void EditorController::stepSource(int frames){seekSource(sourcePosition()+frames);}
void EditorController::beginSourceScrub(){
    if(sourceScrubbing_||!sourceMode_||selectedSource_<0)return;
    sourcePreview_->pause(false);sourceScrubFrame_=sourcePosition();sourceScrubbing_=true;
}
void EditorController::finishSourceScrub(bool cancel){
    if(!sourceScrubbing_)return;
    const int target=sourceScrubFrame_;sourceScrubbing_=false;
    if(selectedSource_<0||selectedSource_>=project_.assets.size())return;
    if(cancel){if(sourceProducer_)sourcePreview_->requestSeek(project_.assets[selectedSource_].cursorFrame);emit sourceChanged();return;}
    seekSource(target);
}
void EditorController::setSourceRange(int first,int exclusiveEnd) {cancelPendingVisual();
    if(selectedSource_<0||selectedSource_>=project_.assets.size())return;
    auto next=project_;auto &a=next.assets[selectedSource_];
    if(first<0||first>=a.durationFrames||exclusiveEnd<1||exclusiveEnd>a.durationFrames)return;
    a.inFrame=first;a.outFrame=exclusiveEnd;commit(next,selection_,primary_,"Mark source range");
}
void EditorController::markSourceIn(){const auto a=currentSource();if(!a.isEmpty())setSourceRange(sourcePosition(),a["outFrame"].toInt());}
void EditorController::markSourceOut(){const auto a=currentSource();if(!a.isEmpty())setSourceRange(a["inFrame"].toInt(),sourcePosition()+1);}
void EditorController::clearSourceRange(){const auto a=currentSource();if(!a.isEmpty())setSourceRange(0,a["durationFrames"].toInt());}
QVariantMap EditorController::sourceInsertionPreview(int frame) const {
    QVariantMap result{{"valid",false},{"frame",frame},{"durationFrames",duration()}};
    if(selectedSource_<0||selectedSource_>=project_.assets.size()){result["reason"]="Choose a source in Media.";return result;}
    const auto &a=project_.assets[selectedSource_];
    if(a.kind!="video"){result["reason"]="Audio sources can be assigned as narration or music in the Audio inspector.";return result;}
    if(a.outFrame<=a.inFrame){result["reason"]="Mark a nonempty source range before adding it.";return result;}
    auto next=project_;const int index=next.insertClipAtFrame({a.path,a.inFrame/60.0,a.outFrame/60.0},frame);
    if(index<0){result["reason"]="Cannot insert here: move the project playhead outside the crossfade or onto a hard cut.";return result;}
    result["valid"]=true;result["index"]=index;result["durationFrames"]=next.durationFrames();
    result["reason"]=QString("Insert at %1 · Result %2").arg(clockText(frame),clockText(next.durationFrames()));return result;
}
bool EditorController::appendSource(){cancelPendingVisual();return insertSourceAt(duration());}
bool EditorController::insertSource(){return insertSourceAt(position_);}
bool EditorController::insertSourceAt(int frame) {cancelPendingVisual();
    const auto feedback=sourceInsertionPreview(frame);
    if(!feedback["valid"].toBool()){report(feedback["reason"].toString());return false;}
    const auto &a=project_.assets[selectedSource_];auto next=project_;
    const int index=next.insertClipAtFrame({a.path,a.inFrame/60.0,a.outFrame/60.0},frame);
    return index>=0&&commit(next,{index},index,frame==duration()?"Append source range":"Insert source range",false,-1);
}
bool EditorController::dropMediaAt(const QStringList &paths,int frame) {cancelPendingVisual();
    auto next=project_;QStringList problems;
    if(frame<0||frame>duration())return false;
    addImported(next,paths,false,problems);
    int insertAt=frame,last=-1;
    for(const auto &input:paths){
        const QString path=QFileInfo(input).absoluteFilePath();
        auto it=std::find_if(next.assets.cbegin(),next.assets.cend(),[&](const MediaAsset &a){return a.path==path;});
        if(it==next.assets.cend()||it->kind!="video"){report("Drop video on the main sequence. Import audio into Media to assign it in the Audio inspector.");return false;}
        const int added=it->outFrame-it->inFrame;
        if(added<1){report("Mark a nonempty source range before adding it.");return false;}
        last=next.insertClipAtFrame({path,it->inFrame/60.0,it->outFrame/60.0},insertAt);
        if(last<0){report("Cannot insert inside a crossfade. Drop outside the overlap or on a hard cut.");return false;}
        insertAt+=added;
    }
    if(last<0)return false;
    if(!problems.isEmpty())report(problems.join('\n'));
    showProject();return commit(next,{last},last,"Drop media into sequence",false,-1);
}
bool EditorController::removeSource(int index,bool confirmed) {cancelPendingVisual();
    if(index<0||index>=project_.assets.size())return false;
    const int uses=timelineData_.sourceUses.value(project_.assets[index].path);
    if(uses>0&&!confirmed){report(QString("This source has %1 use(s) in the project. Confirm Remove from Media; existing instances and the original file are preserved.").arg(uses));return false;}
    auto next=project_;next.assets.removeAt(index);return commit(next,selection_,primary_,"Remove from Media");
}
void EditorController::confirmRemoveSource(int index){
    if(index<0||index>=project_.assets.size())return;
    const auto a=sourceAsset(index);const int uses=a["uses"].toInt();
    if(uses==0){removeSource(index);return;}
    const auto answer=QMessageBox::question(nullptr,"Remove from Media?",QString("%1 has %2 use(s) in the project. Remove only its Media entry? Existing sequence instances and the original file are preserved.").arg(a["name"].toString()).arg(uses),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel);
    if(answer==QMessageBox::Yes)removeSource(index,true);
}
void EditorController::startSourceDrag(int index){
    selectSource(index);if(index<0||index>=project_.assets.size())return;
    auto *window=qobject_cast<QQuickWindow*>(QGuiApplication::focusWindow());if(!window)return;
    QDrag drag(window);auto *mime=new QMimeData;mime->setUrls({QUrl::fromLocalFile(project_.assets[index].path)});drag.setMimeData(mime);drag.exec(Qt::CopyAction);
}
