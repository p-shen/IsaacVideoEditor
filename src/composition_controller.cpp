#include "editor_controller.h"
#include "mlt_engine.h"
#include "preview.h"
#include "preview_cache.h"
#include "visual_renderer.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QUrl>
#include <QtMath>
#include <algorithm>
#include <cmath>
namespace {
TransformCurve *curve(VisualTransform &t,const QString &name){if(name=="x"||name=="position")return &t.x;if(name=="y")return &t.y;if(name=="scale")return &t.scale;if(name=="rotation")return &t.rotation;if(name=="opacity")return &t.opacity;return nullptr;}
}
QVariantList EditorController::visualOverlays() const {
    QVariantList list;for(int i=0;i<project_.visualOverlays.size();++i){const auto &o=project_.visualOverlays[i];
        list.append(QVariantMap{{"index",i},{"name",QFileInfo(o.path).fileName()},{"path",o.path},{"startFrame",project_.overlayStartFrame(i)},{"durationFrames",project_.overlayFrames(i)},{"sourceInFrame",o.sourceInFrame},{"lane",o.lane},{"attached",!o.hostId.isEmpty()},{"hostId",o.hostId},{"hostSourceFrame",o.hostSourceFrame},{"visible",project_.visualLaneVisible[o.lane]}});}
    return list;
}
QVariantList EditorController::visualLanes() const {QVariantList result;for(bool v:project_.visualLaneVisible)result.append(v);return result;}
void EditorController::cancelPendingVisual(){if(visualBefore_){endVisualGesture(true);report("Visual preview canceled before starting another edit.");}}
QVariantMap EditorController::canvas() const {return {{"width",project_.canvasWidth},{"height",project_.canvasHeight}};}
VisualTransform *EditorController::selectedTransform(Project &p) const {
    if(selectedOverlay_>=0&&selectedOverlay_<p.visualOverlays.size())return &p.visualOverlays[selectedOverlay_].transform;
    if(primary_>=0&&primary_<p.clips.size())return &p.clips[primary_].transform;return nullptr;
}
int EditorController::transformSourceFrame() const {
    if(selectedOverlay_>=0&&selectedOverlay_<project_.visualOverlays.size()){const auto &o=project_.visualOverlays[selectedOverlay_];return o.sourceInFrame+std::clamp(position_-project_.overlayStartFrame(selectedOverlay_),0,o.durationFrames-1);}
    if(primary_>=0&&primary_<project_.clips.size())return qRound(project_.clips[primary_].inSeconds*60)+std::clamp(position_-project_.clipStartFrame(primary_),0,project_.clipFrames(project_.clips[primary_])-1);
    return 0;
}
QVariantMap EditorController::currentTransform() const {
    Project copy=project_;auto *t=selectedTransform(copy);if(!t)return {};
    const int f=transformSourceFrame();QVariantMap result{{"sourceFrame",f},{"framing",t->framing},{"cropLeft",t->cropLeft},{"cropTop",t->cropTop},{"cropRight",t->cropRight},{"cropBottom",t->cropBottom}};
    for(const auto &name:{"x","y","scale","rotation","opacity"}){auto *c=curve(*t,name);result[name]=c->valueAt(f);result[QString(name)+"Animated"]=!c->keys.isEmpty();QVariantList frames;bool here=false,ease=false;for(const auto &k:c->keys){frames.append(k.frame);if(k.frame==f){here=true;ease=k.ease;}}result[QString(name)+"Keys"]=frames;result[QString(name)+"Key"]=here;result[QString(name)+"Ease"]=ease;}
    if(selectedOverlay_<0&&primary_>=0)result["rotation"]=result["rotation"].toDouble()+project_.clips[primary_].rotationDegrees;
    int start=0,length=0;
    if(selectedOverlay_>=0){start=project_.overlayStartFrame(selectedOverlay_);length=project_.overlayFrames(selectedOverlay_);}
    else {start=project_.clipStartFrame(primary_);length=project_.clipFrames(project_.clips[primary_]);}
    result["active"]=position_>=start&&position_<start+length;return result;
}
void EditorController::setTransformMode(bool enabled){if(transformMode_==enabled)return;if(visualBefore_)endVisualGesture(true);transformMode_=enabled;emit compositionChanged();}
void EditorController::applyComposition(const Project &p,const QString &label){
    const auto error=p.validate();if(!error.isEmpty()){report(error);if(visualBefore_)endVisualGesture(true);return;}
    if(visualBefore_){
        bool canUpdate=p.canvasWidth==project_.canvasWidth&&p.canvasHeight==project_.canvasHeight&&p.visualLaneVisible==project_.visualLaneVisible&&p.visualOverlays.size()==project_.visualOverlays.size()&&p.clips.size()==project_.clips.size();
        for(int i=0;canUpdate&&i<p.clips.size();++i){
            const auto &next=p.clips[i],&before=project_.clips[i];
            canUpdate=next.id==before.id&&next.path==before.path&&next.rotationDegrees==before.rotationDegrees;
        }
        for(int i=0;canUpdate&&i<p.visualOverlays.size();++i){
            const auto &next=p.visualOverlays[i],&before=project_.visualOverlays[i];
            canUpdate=next.path==before.path&&next.lane==before.lane&&((p.overlayFrames(i)>0)==(project_.overlayFrames(i)>0));
            if(canUpdate&&overlayProxyPaths_.value(i,next.path)!=next.path){
                const auto copy=proxyCache_->readyCopy(next.path,next.sourceInFrame,next.sourceInFrame+p.overlayFrames(i));
                canUpdate=copy.path==overlayProxyPaths_.value(i);
            }
        }
        project_=p;
        if(timeline_&&canUpdate){
            auto *timeline=timeline_.get();
            preview_->requestSeek(position_,true,[timeline,p]{VisualRendering::update(*timeline,p);});
        }else rebuild();
        emit projectChanged();emit selectionChanged();
    }
    else commit(p,selection_,primary_,label);
}
void EditorController::beginVisualGesture(int overlay){showProject();if(audioPreview_)endAudioGesture(false);if(visualBefore_)return;if(textDragBefore_)endTextDrag(false);const auto before=state();if(overlay>=0)selectVisualOverlay(overlay);preview_->pause();endSkim();visualNoticeBefore_=notice_;visualBefore_=before;}
void EditorController::endVisualGesture(bool cancel){
    if(!visualBefore_)return;const State after=state(),before=*visualBefore_;visualBefore_.reset();
    if(cancel){notice_=visualNoticeBefore_;apply(before,position_,true);return;}
    // Restore the authoritative state before creating one command for the whole gesture.
    project_=before.project;selection_=before.selection;primary_=before.primary;selectedOverlay_=before.overlay;selectedText_=before.text;selectedCaption_=before.caption;captionSelection_=before.captions;
    if(!commit(after.project,after.selection,after.primary,"Edit visual",false,after.text,after.overlay,after.caption,after.captions))apply(before,position_,true);
    notice_=visualNoticeBefore_;emit stateChanged();
}
namespace {
bool prepareVisualOverlay(const Project &p,const QString &path,int frame,int lane,int sourceInFrame,int sourceOutFrame,VisualOverlay &o,QString &error){
    if(p.durationFrames()<1||lane<0||lane>2||!QFileInfo(path).isFile())return false;
    const bool still=QImageReader(path).canRead();auto info=still?std::optional<Media::MediaInfo>{}:Media::mediaInfo(path,&error);
    if(!still&&(!info||info->width<1)){error=error.isEmpty()?QString("Choose a video or image overlay."):error;return false;}
    frame=std::clamp(frame<0?0:frame,0,p.durationFrames()-1);o.path=path;o.lane=lane;o.startFrame=frame;
    const int sourceLength=still?86400*60:qRound(info->durationSeconds*60);
    if(sourceInFrame<0||sourceInFrame>=sourceLength)return false;
    if(sourceOutFrame<0)sourceOutFrame=still?sourceInFrame+300:sourceLength;
    if(sourceOutFrame<=sourceInFrame||sourceOutFrame>sourceLength)return false;
    o.sourceInFrame=sourceInFrame;o.durationFrames=std::min(sourceOutFrame-sourceInFrame,p.durationFrames()-frame);
    for(int i=0;i<p.clips.size();++i)if(frame>=p.clipStartFrame(i)&&frame<p.clipStartFrame(i)+p.clipFrames(p.clips[i])){o.hostId=p.clips[i].id;o.hostSourceFrame=qRound(p.clips[i].inSeconds*60)+frame-p.clipStartFrame(i);break;}
    return true;
}
}
bool EditorController::addVisualOverlay(const QString &path,int frame,int lane,int sourceInFrame,int sourceOutFrame){cancelPendingVisual();
    if(visualBefore_)endVisualGesture(false);if(audioPreview_)endAudioGesture(false);if(textDragBefore_)endTextDrag(false);showProject();
    VisualOverlay overlay;QString error;
    if(!prepareVisualOverlay(project_,path,frame<0?position_:frame,lane,sourceInFrame,sourceOutFrame,overlay,error)){if(!error.isEmpty())report(error);return false;}
    auto next=project_;next.visualOverlays.append(overlay);
    return commit(next,{},-1,"Add connected overlay",false,-1,next.visualOverlays.size()-1);
}
bool EditorController::dropVisualMedia(const QVariantList &urls,int frame,int lane){cancelPendingVisual();
    if(visualBefore_)endVisualGesture(false);if(audioPreview_)endAudioGesture(false);if(textDragBefore_)endTextDrag(false);
    if(urls.isEmpty())return false;
    auto next=project_;QString error;
    for(const auto &value:urls){
        const auto url=value.toUrl();if(!url.isLocalFile()){report("Drop local video or image files into an overlay lane.");return false;}
        const auto path=QFileInfo(url.toLocalFile()).absoluteFilePath();int first=0,last=-1;
        for(const auto &asset:project_.assets)if(QFileInfo(asset.path).absoluteFilePath()==path){first=asset.inFrame;last=asset.outFrame;break;}
        VisualOverlay overlay;
        if(!prepareVisualOverlay(project_,path,frame,lane,first,last,overlay,error)){report(error.isEmpty()?"The overlay or marked source range is unavailable.":error);return false;}
        next.visualOverlays.append(overlay);
    }
    showProject();return commit(next,{},-1,"Drop connected overlays",false,-1,next.visualOverlays.size()-1);
}
void EditorController::chooseVisualOverlay(){const auto path=QFileDialog::getOpenFileName(nullptr,"Add connected video or image",{},"Visual media (*.mp4 *.mov *.m4v *.mkv *.avi *.webm *.png *.jpg *.jpeg *.webp *.bmp);;All files (*)");if(!path.isEmpty())addVisualOverlay(path);}
void EditorController::selectVisualOverlay(int index){showProject();if(index<0||index>=project_.visualOverlays.size())return;if(visualBefore_)endVisualGesture(true);if(textDragBefore_)endTextDrag(false);selectedOverlay_=index;selectedText_=-1;selectedCaption_=-1;captionSelection_.clear();selection_.clear();primary_=-1;emit selectionChanged();emit stateChanged();const int start=project_.overlayStartFrame(index);if(position_<start||position_>=start+project_.overlayFrames(index))seek(start);}
void EditorController::removeVisualOverlay(){cancelPendingVisual();if(selectedOverlay_<0||selectedOverlay_>=project_.visualOverlays.size())return;auto next=project_;next.visualOverlays.removeAt(selectedOverlay_);commit(next,{},-1,"Remove overlay",false,-1,-1);}
void EditorController::detachVisualOverlay(){cancelPendingVisual();auto next=project_;if(next.detachOverlay(selectedOverlay_))commit(next,selection_,primary_,"Detach overlay to project time");}
void EditorController::detachSelectedHosts(){cancelPendingVisual();if(textDragBefore_)endTextDrag(false);auto next=project_;
    for(const auto &index:selection_){const auto id=project_.clips[index.toInt()].id;
        for(int j=0;j<next.visualOverlays.size();++j)if(next.visualOverlays[j].hostId==id)next.detachOverlay(j);
        for(bool caption:{false,true}){auto &list=caption?next.captions:next.textOverlays;for(int j=0;j<list.size();++j)if(list[j].hostId==id)next.detachText(j,caption);}
    }commit(next,selection_,primary_,"Detach connected items");}
void EditorController::setVisualLaneVisible(int lane,bool visible){cancelPendingVisual();if(lane<0||lane>=3)return;auto next=project_;next.visualLaneVisible[lane]=visible;commit(next,selection_,primary_,"Change overlay lane visibility");}
void EditorController::setOverlayLane(int index,int lane){if(index<0||index>=project_.visualOverlays.size()||lane<0||lane>2)return;auto next=project_;next.visualOverlays[index].lane=lane;applyComposition(next,"Change overlay layer");}
void EditorController::setOverlayTiming(int index,int start,int sourceIn,int frames){
    if(index<0||index>=project_.visualOverlays.size()||sourceIn<0||sourceIn>86400*60||frames<1)return;
    const int requestedStart=start,requestedFrames=frames;
    auto next=project_;auto &o=next.visualOverlays[index];start=std::clamp(start,0,std::max(0,duration()-1));frames=std::min(frames,duration()-start);
    if(!QImageReader(o.path).canRead()){const int available=qRound(sourceDuration(o.path)*60)-sourceIn;if(available<1)return;frames=std::min(frames,available);}
    o.sourceInFrame=sourceIn;o.durationFrames=frames;
    if(!o.hostId.isEmpty()){
        // Moving a connected start reconnects it to the main source frame under that start.
        for(int i=0;i<next.clips.size();++i)if(start>=next.clipStartFrame(i)&&start<next.clipStartFrame(i)+next.clipFrames(next.clips[i])){o.hostId=next.clips[i].id;o.hostSourceFrame=qRound(next.clips[i].inSeconds*60)+start-next.clipStartFrame(i);break;}
    }else o.startFrame=start;
    report(QString("Overlay %1 · start %2 · %3 frames · end %4%5").arg(visualBefore_?"preview":"timing").arg(start).arg(frames).arg(start+frames).arg(frames!=requestedFrames||start!=requestedStart?" · clamped to source and project end":""));
    applyComposition(next,"Move or trim overlay");
}
void EditorController::moveOverlayByPixels(int index,double delta,double pixelsPerSecond,int edge){
    if(!visualBefore_||index<0||index>=visualBefore_->project.visualOverlays.size()||!std::isfinite(delta)||pixelsPerSecond<=0)return;
    const auto &p=visualBefore_->project;const auto &o=p.visualOverlays[index];int shift=qRound(delta*60/pixelsPerSecond),start=p.overlayStartFrame(index),frames=o.durationFrames,in=o.sourceInFrame;
    if(edge<0){shift=std::clamp(shift,std::max(-start,-in),frames-1);start+=shift;in+=shift;frames-=shift;}else if(edge>0)frames=std::max(1,frames+shift);else start+=shift;
    setOverlayTiming(index,start,in,frames);
}
void EditorController::setCanvas(int width,int height){auto next=project_;next.canvasWidth=width;next.canvasHeight=height;applyComposition(next,"Change canvas");}
void EditorController::setTransform(const QVariantMap &fields){
    auto next=project_;auto *t=selectedTransform(next);if(!t)return;const int f=transformSourceFrame();
    for(auto i=fields.begin();i!=fields.end();++i){if(i.key()=="framing"){t->framing=i.value().toString();continue;}bool ok=false;const double v=i.value().toDouble(&ok);if(!ok||!std::isfinite(v)){report("Enter a finite transform value.");return;}
        if(auto *c=curve(*t,i.key()))c->edit(f,v-(i.key()=="rotation"&&selectedOverlay_<0&&primary_>=0?project_.clips[primary_].rotationDegrees:0));
        else if(i.key()=="cropLeft")t->cropLeft=v;else if(i.key()=="cropTop")t->cropTop=v;else if(i.key()=="cropRight")t->cropRight=v;else if(i.key()=="cropBottom")t->cropBottom=v;else return;
    }
    if((fields.contains("x")||fields.contains("y"))&&!t->x.keys.isEmpty()){t->x.edit(f,t->x.valueAt(f));t->y.edit(f,t->y.valueAt(f));}
    applyComposition(next,"Change transform");
}
void EditorController::resetTransform(){auto next=project_;auto *t=selectedTransform(next);if(t){*t={};if(selectedOverlay_<0&&primary_>=0)next.clips[primary_].rotationDegrees=0;applyComposition(next,"Reset transform");}}
void EditorController::editKeyframe(const QString &property,const QString &action,int target){
    auto next=project_;auto *t=selectedTransform(next);if(!t)return;auto *c=curve(*t,property);if(!c)return;const int f=transformSourceFrame();
    if(action=="previous"||action=="next"){
        const int start=selectedOverlay_>=0?project_.overlayStartFrame(selectedOverlay_):project_.clipStartFrame(primary_);
        const int sourceIn=selectedOverlay_>=0?project_.visualOverlays[selectedOverlay_].sourceInFrame:qRound(project_.clips[primary_].inSeconds*60);
        const int length=selectedOverlay_>=0?project_.overlayFrames(selectedOverlay_):project_.clipFrames(project_.clips[primary_]);
        int frame=-1;for(const auto &k:c->keys){if(k.frame<sourceIn||k.frame>=sourceIn+length)continue;if(action=="previous"&&k.frame<f)frame=k.frame;else if(action=="next"&&k.frame>f){frame=k.frame;break;}}
        if(frame>=0)seek(start+frame-sourceIn);return;
    }
    QVector<TransformCurve*> curves{c};if(property=="position")curves.append(&t->y);
    for(auto *item:curves){const double v=item->valueAt(f);
        if(action=="add")item->setKey(f,v);
        else if(action=="delete"){item->keys.erase(std::remove_if(item->keys.begin(),item->keys.end(),[&](const auto &k){return k.frame==f;}),item->keys.end());if(item->keys.isEmpty())item->value=v;}
        else if(action=="linear"||action=="ease"){for(auto &k:item->keys)if(k.frame==f)k.ease=action=="ease";}
        else if(action=="move"&&target>=0&&target<=86400*60){auto found=std::find_if(item->keys.begin(),item->keys.end(),[&](const auto &k){return k.frame==f;});if(found!=item->keys.end()){const auto key=*found;item->keys.erase(found);item->setKey(target,key.value,key.ease);}}
        else return;
    }
    applyComposition(next,"Edit transform keyframe");
}
