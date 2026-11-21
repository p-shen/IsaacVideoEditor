#include "editor_controller.h"
#include "captions.h"
#include "preview.h"
#include "text_renderer.h"
#include "text_style.h"
#include <QColorDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <algorithm>

QVariantList EditorController::captions() const {
    return captionModel_->records();
}
QVariantMap EditorController::currentCaption() const {return timedTextInfo(project_,selectedCaption_,true);}
QStringList EditorController::captionWarnings() const {
    return captionModel_->warnings();
}
void EditorController::updateTextPreview(){
    if(selectedCaption_>=0)captionModel_->updateCaption(project_,selectedCaption_);
    if(!textPreviewTimer_.isActive())textPreviewTimer_.start();
    emit stateChanged();
}
void EditorController::flushTextPreview(){
    const bool caption=selectedCaption_>=0;const int index=caption?selectedCaption_:selectedText_;
    const auto &texts=caption?project_.captions:project_.textOverlays;
    if(index<0||index>=texts.size()||(caption&&!project_.burnCaptions))return;
    if(!timeline_){if(preparing_)rebuild();return;}
    auto text=texts[index];text.startFrame=project_.textStartFrame(index,caption);
    if(caption||!text.hostId.isEmpty())text.durationFrames=std::clamp(duration()-text.startFrame,0,text.durationFrames);
    const int renderIndex=caption?project_.textOverlays.size()+index:index;
    preview_->requestSeek(position_,true,[timeline=timeline_.get(),renderIndex,text]{TextRendering::updateOne(*timeline,renderIndex,text);},renderIndex);
}
TextOverlay *EditorController::selectedTimedText(Project &project) const {
    if(selectedCaption_>=0&&selectedCaption_<project.captions.size())return &project.captions[selectedCaption_];
    return selectedText_>=0&&selectedText_<project.textOverlays.size()?&project.textOverlays[selectedText_]:nullptr;
}
bool EditorController::importSrt(const QString &path) {
    cancelPendingVisual();if(audioPreview_)endAudioGesture(false);if(textDragBefore_)endTextDrag(false);
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){report(file.errorString());return false;}
    if(file.size()>128*1024*1024){report("SRT files must be smaller than 128 MB.");return false;}
    QString error;const auto imported=Subtitles::parseSrt(file.readAll(),&error);if(!imported){report(error);return false;}
    if(project_.captions.size()+imported->size()>10000){report("A project supports up to 10000 captions.");return false;}
    auto next=project_;const int first=next.captions.size();next.captions+=*imported;
    showProject();return commit(next,{},-1,"Import SRT captions",false,-1,-1,first,{first});
}
bool EditorController::exportSrt(const QString &path) {
    if(path.isEmpty())return false;if(textDragBefore_)endTextDrag(false);
    const auto normalized=[](const QString &name){QFileInfo f(name);return f.exists()?f.canonicalFilePath():f.absoluteFilePath();};
    QStringList sources{project_.voicePath,project_.musicPath,path_};
    for(const auto &c:project_.clips)sources.append(c.path);for(const auto &a:project_.assets)sources.append(a.path);for(const auto &o:project_.visualOverlays)sources.append(o.path);
    for(const auto &source:sources)if(!source.isEmpty()&&normalized(path).compare(normalized(source),Qt::CaseInsensitive)==0){report("Choose a different file from your source media or saved project.");return false;}
    QSaveFile file(path);const auto bytes=Subtitles::writeSrt(project_);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){report(file.errorString());return false;}
    report("SRT captions exported. Manual titles and the video burn option are independent.");return true;
}
void EditorController::importSrtDialog(){const auto path=QFileDialog::getOpenFileName(nullptr,"Import SRT captions (append)",QDir::home().filePath("Movies"),"SubRip captions (*.srt);;All files (*)");if(!path.isEmpty())importSrt(path);}
void EditorController::exportSrtDialog(){const auto path=QFileDialog::getSaveFileName(nullptr,"Export SRT captions",QDir::home().filePath("Movies/captions.srt"),"SubRip captions (*.srt)");if(!path.isEmpty())exportSrt(path);}
void EditorController::addCaption(){
    cancelPendingVisual();if(audioPreview_)endAudioGesture(false);if(textDragBefore_)endTextDrag(false);showProject();
    if(project_.captions.size()>=10000){report("A project supports up to 10000 captions.");return;}
    auto next=project_;TextOverlay cue;cue.text="New caption";cue.fontSize=57;cue.y=.85;cue.startFrame=position_;cue.durationFrames=duration()>position_?std::min(180,duration()-position_):180;
    next.captions.append(cue);const int index=next.captions.size()-1;commit(next,{},-1,"Add caption",false,-1,-1,index,{index});
}
void EditorController::deleteCaptions(){
    cancelPendingVisual();if(textDragBefore_)endTextDrag(true);if(captionSelection_.isEmpty())return;
    auto next=project_;QVector<int> indices;
    for(const auto &value:captionSelection_){bool valid=false;const int index=value.toInt(&valid);if(valid&&index>=0&&index<next.captions.size())indices.append(index);}
    std::sort(indices.begin(),indices.end(),std::greater<int>());indices.erase(std::unique(indices.begin(),indices.end()),indices.end());
    for(int index:indices)next.captions.removeAt(index);
    commit(next,{},-1,"Delete captions",false,-1,-1,-1,{});
}
void EditorController::selectCaption(int index,int modifiers){
    if(index<0||index>=project_.captions.size())return;
    cancelPendingVisual();if(audioPreview_)endAudioGesture(false);if(textDragBefore_)endTextDrag(false);showProject();
    if(modifiers&Qt::ShiftModifier){captionSelection_.clear();const int anchor=captionAnchor_>=0&&captionAnchor_<project_.captions.size()?captionAnchor_:index;for(int i=std::min(index,anchor);i<=std::max(index,anchor);++i)captionSelection_.append(i);}
    else if(modifiers&(Qt::ControlModifier|Qt::MetaModifier)){
        if(captionSelection_.contains(index))captionSelection_.removeAll(index);else captionSelection_.append(index);
        std::sort(captionSelection_.begin(),captionSelection_.end(),[](const QVariant &a,const QVariant &b){return a.toInt()<b.toInt();});captionAnchor_=index;
    }else{captionSelection_={index};captionAnchor_=index;}
    selectedCaption_=captionSelection_.contains(index)?index:(captionSelection_.isEmpty()?-1:captionSelection_.last().toInt());
    selectedText_=selectedOverlay_=primary_=anchor_=-1;selection_.clear();transformMode_=false;
    emit selectionChanged();emit compositionChanged();emit stateChanged();
}
void EditorController::navigateCaption(int direction){
    QVector<int> order;for(int i=0;i<project_.captions.size();++i)order.append(i);if(order.isEmpty())return;
    const auto timing=project_.textTiming();
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){return timing.captionStarts[a]<timing.captionStarts[b];});
    const int current=order.indexOf(selectedCaption_);const int next=current<0?(direction<0?order.size()-1:0):std::clamp(current+(direction<0?-1:1),0,int(order.size()-1));selectCaption(order[next]);
}
void EditorController::previewCaption(){
    if(selectedCaption_<0)return;preview_->requestPause();seek(project_.textStartFrame(selectedCaption_,true));
}
void EditorController::beginCaptionEdit(int index,int modifiers){
    cancelPendingVisual();if(index<0||index>=project_.captions.size())return;if(audioPreview_)endAudioGesture(false);if(textDragBefore_)endTextDrag(false);
    const auto before=state();selectCaption(index,modifiers);preview_->requestPause();resumeWhenSettled_=false;endSkim();textDragBefore_=before;
}
void EditorController::previewCaptionContent(const QString &text){
    if(!textDragBefore_&&selectedCaption_>=0)beginCaptionEdit(selectedCaption_);
    if(!textDragBefore_||selectedCaption_<0||selectedCaption_>=project_.captions.size()||text.size()>2000)return;
    const bool formatted=!project_.captions[selectedCaption_].spans.isEmpty();
    replaceTextContent(project_.captions[selectedCaption_],text);updateTextPreview();if(formatted)emit selectionChanged();
}
void EditorController::setCaptionTiming(int index,int start,int frames){
    cancelPendingVisual();if(index<0||index>=project_.captions.size()||start<0||frames<1||qint64(start)+frames>86400*60)return;
    auto next=project_;
    if(!next.setTextTiming(index,true,start,frames)){
        if(!project_.captions[index].hostId.isEmpty()&&start!=project_.textStartFrame(index,true))report("An attached caption must start inside a host clip. Detach to use Project time.");
        return;
    }
    if(textDragBefore_){project_=next;updateTextPreview();emit projectChanged();}
    else commit(next,{},-1,"Change caption timing",false,-1,-1,index,captionSelection_.contains(index)?captionSelection_:QVariantList{index});
}
void EditorController::styleCaptions(const QVariantMap &fields,bool all){
    cancelPendingVisual();if(textDragBefore_)endTextDrag(false);
    for(auto i=fields.begin();i!=fields.end();++i)if(!QStringList{"preset","style","fontFamily","fontSize","color","bold","shadow","fade","italic","uppercase","animation","x","y"}.contains(i.key())){report("Batch styling only changes caption appearance and animation.");return;}
    auto next=project_;QString error;
    for(int i=0;i<next.captions.size();++i)if(all||captionSelection_.contains(i))if(!setTextFields(next.captions[i],fields,&error)){report(error);return;}
    commit(next,{},-1,all?"Apply caption style to all":"Style selected captions",false,-1,-1,selectedCaption_,captionSelection_);
}
void EditorController::chooseCaptionColor(){if(selectedCaption_<0)return;const auto color=QColorDialog::getColor(QColor(project_.captions[selectedCaption_].color),nullptr,"Caption color");if(color.isValid())styleCaptions({{"color",color.name()}});}
QVariantMap EditorController::captionTextStyle(int start,int end) const {
    if(selectedCaption_<0||selectedCaption_>=project_.captions.size())return {};
    return textRangeFields(project_.captions[selectedCaption_],start,end);
}
void EditorController::styleCaptionText(int start,int end,const QVariantMap &fields,bool editing) {
    cancelPendingVisual();if(selectedCaption_<0||selectedCaption_>=project_.captions.size())return;
    auto next=project_;QString error;
    if(!setTextRangeFields(next.captions[selectedCaption_],start,end,fields,&error)){report(error);return;}
    if(editing&&!textDragBefore_)beginCaptionEdit(selectedCaption_);
    if(textDragBefore_){project_=next;updateTextPreview();emit selectionChanged();}
    else commit(next,{},-1,"Format caption text",false,-1,-1,selectedCaption_,captionSelection_);
}
void EditorController::chooseCaptionTextColor(int start,int end,bool editing) {
    const auto fields=captionTextStyle(start,end);if(fields.isEmpty())return;
    const int index=selectedCaption_;const auto before=project_.captions[index];
    const auto color=QColorDialog::getColor(QColor(fields["color"].toString().isEmpty()?before.color:fields["color"].toString()),nullptr,"Selected caption text color");
    if(color.isValid()&&selectedCaption_==index&&index<project_.captions.size()&&project_.captions[index]==before)styleCaptionText(start,end,{{"color",color.name()}},editing);
}
void EditorController::setBurnCaptions(bool enabled){cancelPendingVisual();if(textDragBefore_)endTextDrag(false);auto next=project_;next.burnCaptions=enabled;commit(next,selection_,primary_,"Change caption burn option");}
void EditorController::reviewCaptions(){cancelPendingVisual();if(textDragBefore_)endTextDrag(false);auto next=project_;for(auto &cue:next.captions)cue.timingNeedsReview=false;commit(next,selection_,primary_,"Mark caption timing reviewed");}
void EditorController::attachTimedText(bool caption,int hostIndex){
    cancelPendingVisual();if(textDragBefore_)endTextDrag(false);auto next=project_;
    if(caption){for(const auto &index:captionSelection_)next.attachText(index.toInt(),true,hostIndex);}
    else next.attachText(selectedText_,false,hostIndex);
    commit(next,selection_,primary_,caption?"Attach captions to clip":"Attach title to clip");
}
void EditorController::detachTimedText(bool caption){
    cancelPendingVisual();if(textDragBefore_)endTextDrag(false);auto next=project_;
    if(caption){for(const auto &index:captionSelection_)next.detachText(index.toInt(),true);}else next.detachText(selectedText_,false);
    commit(next,selection_,primary_,caption?"Detach captions to Project time":"Detach title to Project time");
}
void EditorController::removeSelectedHosts(bool detach){
    cancelPendingVisual();if(textDragBefore_)endTextDrag(false);if(selection_.isEmpty())return;auto next=project_;
    if(detach)for(const auto &index:selection_){const auto id=project_.clips[index.toInt()].id;
        for(int i=0;i<next.visualOverlays.size();++i)if(next.visualOverlays[i].hostId==id)next.detachOverlay(i);
        for(bool caption:{false,true}){auto &list=caption?next.captions:next.textOverlays;for(int i=0;i<list.size();++i)if(list[i].hostId==id)next.detachText(i,caption);}
    }
    for(int i=selection_.size()-1;i>=0;--i)next.removeClip(selection_[i].toInt());
    const int row=std::min(selection_.first().toInt(),int(next.clips.size())-1);commit(next,row<0?QVariantList{}:QVariantList{row},row,"Remove clips and handle attachments");
}
