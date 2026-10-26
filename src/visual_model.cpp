#include "project.h"
#include <QtMath>
#include <algorithm>
bool VisualOverlay::operator==(const VisualOverlay &o) const {
    return path==o.path&&sourceInFrame==o.sourceInFrame&&durationFrames==o.durationFrames&&startFrame==o.startFrame&&lane==o.lane&&hostId==o.hostId&&hostSourceFrame==o.hostSourceFrame&&transform==o.transform;
}
int Project::overlayStartFrame(int index) const {
    if(index<0||index>=visualOverlays.size())return -1;
    const auto &o=visualOverlays[index];
    return attachedFrame(o.hostId,o.hostSourceFrame,o.startFrame);
}
int Project::overlayFrames(int index) const {
    if(index<0||index>=visualOverlays.size())return 0;
    return std::clamp(durationFrames()-overlayStartFrame(index),0,visualOverlays[index].durationFrames);
}
void Project::normalizeVisuals() {
    for(auto *list:{&textOverlays,&captions})for(auto &t:*list)
        for(const auto &c:clips)if(c.id==t.hostId)t.hostSourceFrame=std::clamp(t.hostSourceFrame,qRound(c.inSeconds*fps)+c.sourceFrameOffset,qRound(c.outSeconds*fps)+c.sourceFrameOffset-1);
    for(int i=0;i<visualOverlays.size();++i){auto &o=visualOverlays[i];
        for(const auto &c:clips)if(c.id==o.hostId)o.hostSourceFrame=std::clamp(o.hostSourceFrame,qRound(c.inSeconds*fps),qRound(c.outSeconds*fps)-1);
        // Keep detached absolute starts even if the sequence is temporarily shorter.
        const int available=durationFrames()-overlayStartFrame(i);if(available>0)o.durationFrames=std::min(o.durationFrames,available);
    }
}
bool Project::detachOverlay(int index) {
    if(index<0||index>=visualOverlays.size()||visualOverlays[index].hostId.isEmpty())return false;
    visualOverlays[index].startFrame=overlayStartFrame(index);visualOverlays[index].hostId.clear();return true;
}
bool Project::removeClip(int index) {
    if(index<0||index>=clips.size())return false;
    const auto id=clips[index].id;
    visualOverlays.erase(std::remove_if(visualOverlays.begin(),visualOverlays.end(),[&](const auto &o){return o.hostId==id;}),visualOverlays.end());
    for(auto *list:{&textOverlays,&captions})list->erase(std::remove_if(list->begin(),list->end(),[&](const auto &t){return t.hostId==id;}),list->end());
    clips.removeAt(index);normalizeVisuals();return true;
}

int Project::attachedFrame(const QString &hostId,int sourceFrame,int fallback) const {
    if(hostId.isEmpty())return fallback;
    for(int i=0;i<clips.size();++i)if(clips[i].id==hostId){const auto &c=clips[i];const int first=qRound(c.inSeconds*fps)+c.sourceFrameOffset,last=qRound(c.outSeconds*fps)+c.sourceFrameOffset-1;
        return clipStartFrame(i)+std::clamp(sourceFrame,first,last)-first;}
    return fallback;
}
int Project::textStartFrame(int index,bool caption) const {
    const auto &list=caption?captions:textOverlays;if(index<0||index>=list.size())return -1;
    const auto &t=list[index];return attachedFrame(t.hostId,t.hostSourceFrame,t.startFrame);
}
int Project::textFrames(int index,bool caption) const {
    const auto &list=caption?captions:textOverlays;if(index<0||index>=list.size())return 0;
    return std::clamp(durationFrames()-textStartFrame(index,caption),0,list[index].durationFrames);
}
QVector<TextOverlay> Project::renderedTexts() const {
    QVector<TextOverlay> result;
    for(bool caption:{false,true}){if(caption&&!burnCaptions)continue;const auto &list=caption?captions:textOverlays;
        for(int i=0;i<list.size();++i){auto t=list[i];t.startFrame=textStartFrame(i,caption);if(caption||!t.hostId.isEmpty())t.durationFrames=textFrames(i,caption);result.append(t);}}
    return result;
}
bool Project::attachText(int index,bool caption,int hostIndex) {
    auto &list=caption?captions:textOverlays;if(index<0||index>=list.size()||hostIndex<0||hostIndex>=clips.size())return false;
    const int start=textStartFrame(index,caption);auto &t=list[index];const auto &host=clips[hostIndex];
    t.hostId=host.id;t.hostSourceFrame=qRound(host.inSeconds*fps)+host.sourceFrameOffset+std::clamp(start-clipStartFrame(hostIndex),0,clipFrames(host)-1);
    t.timingNeedsReview=false;return true;
}
bool Project::detachText(int index,bool caption) {
    auto &list=caption?captions:textOverlays;if(index<0||index>=list.size()||list[index].hostId.isEmpty())return false;
    const int start=textStartFrame(index,caption);list[index].startFrame=start;list[index].hostId.clear();list[index].hostSourceFrame=0;return true;
}
void Project::reviewCaptionTimingAfter(const Project &before) {
    bool changed=durationFrames()!=before.durationFrames();
    if(clips.size()==before.clips.size())for(int i=0;i<clips.size();++i)changed|=clips[i].id!=before.clips[i].id;
    if(changed)for(auto &t:captions)if(t.hostId.isEmpty())t.timingNeedsReview=true;
}

bool Project::setTextTiming(int index,bool caption,int start,int frames) {
    auto &list=caption?captions:textOverlays;
    if(index<0||index>=list.size()||start<0||frames<1||qint64(start)+frames>86400*fps)return false;
    const int previousStart=textStartFrame(index,caption);auto text=list[index];
    if(start==previousStart&&frames==text.durationFrames)return false;
    text.durationFrames=frames;
    if(start!=previousStart){
        text.startFrame=start;
        if(!text.hostId.isEmpty()){
            const auto contains=[&](int host){return start>=clipStartFrame(host)&&start<clipStartFrame(host)+clipFrames(clips[host]);};
            int host=-1;
            for(int i=0;i<clips.size();++i)if(clips[i].id==text.hostId&&contains(i)){host=i;break;}
            if(host<0)for(int i=0;i<clips.size();++i)if(contains(i)){host=i;break;}
            if(host<0)return false;
            text.hostId=clips[host].id;text.hostSourceFrame=qRound(clips[host].inSeconds*fps)+clips[host].sourceFrameOffset+start-clipStartFrame(host);
        }
    }
    if(caption)text.timingNeedsReview=false;
    list[index]=text;return true;
}
