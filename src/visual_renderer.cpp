#include "visual_renderer.h"
#include <QPainter>
#include <QImageReader>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <memory>
namespace VisualRendering {
QRectF contentRect(const QSize &canvas,const QSizeF &source) {
    if(source.isEmpty())return QRectF(QPointF{},canvas);
    const double scale=std::min(canvas.width()/source.width(),canvas.height()/source.height());
    const QSizeF size=source*scale;return QRectF((canvas.width()-size.width())/2,(canvas.height()-size.height())/2,size.width(),size.height());
}
void paint(QImage &canvas,const QImage &source,const VisualTransform &t,int frame) {
    if(source.isNull())return;
    QRectF crop=source.rect();
    if(t.framing=="crop")crop=QRectF(source.width()*t.cropLeft,source.height()*t.cropTop,source.width()*(1-t.cropLeft-t.cropRight),source.height()*(1-t.cropTop-t.cropBottom));
    const double fit=t.framing=="fill"?std::max(canvas.width()/crop.width(),canvas.height()/crop.height()):std::min(canvas.width()/crop.width(),canvas.height()/crop.height());
    const QSizeF size=crop.size()*fit*t.scale.valueAt(frame);
    QPainter p(&canvas);p.setRenderHint(QPainter::SmoothPixmapTransform);p.setOpacity(t.opacity.valueAt(frame));
    p.translate(t.x.valueAt(frame)*canvas.width(),t.y.valueAt(frame)*canvas.height());p.rotate(t.rotation.valueAt(frame));
    p.drawImage(QRectF(-size.width()/2,-size.height()/2,size.width(),size.height()),source,crop);
}
namespace {
struct TransformState {QString id;VisualTransform transform;QSizeF sourceSize;int sourceFrameOffset=0;};
struct OverlaySource {VisualOverlay overlay;int index=0,start=0,frames=0;QImage still;QSizeF size;std::unique_ptr<Mlt::Producer> producer;};
struct OverlayState {QMutex mutex;std::vector<OverlaySource> sources;};
struct LiveState {std::vector<TransformState*> clips;OverlayState *overlays=nullptr;};
LiveState &liveState(Mlt::Tractor &timeline) {
    auto *state=static_cast<LiveState*>(timeline.get_data("isaac.visual.live"));
    if(!state){state=new LiveState;mlt_properties_set_data(timeline.get_properties(),"isaac.visual.live",state,0,[](void *p){delete static_cast<LiveState*>(p);},nullptr);}
    return *state;
}
constexpr auto transformKey="isaac.visual.transform";
constexpr auto overlayKey="isaac.visual.overlays";
int transformImage(mlt_frame frame,uint8_t **pixels,mlt_image_format *format,int *width,int *height,int) {
    const int position=mlt_frame_pop_service_int(frame);auto *state=static_cast<TransformState*>(mlt_frame_pop_service(frame));
    *format=mlt_image_rgba;const int error=mlt_frame_get_image(frame,pixels,format,width,height,1);
    if(error||!*pixels||*format!=mlt_image_rgba)return error?error:1;
    QImage target(*pixels,*width,*height,*width*4,QImage::Format_RGBA8888);
    QImage source=target.copy(contentRect(target.size(),state->sourceSize).toAlignedRect().intersected(target.rect()));
    target.fill(Qt::black);paint(target,source,state->transform,position+state->sourceFrameOffset);return 0;
}
mlt_frame transformProcess(mlt_filter filter,mlt_frame frame) {
    auto *state=static_cast<TransformState*>(mlt_properties_get_data(MLT_FILTER_PROPERTIES(filter),transformKey,nullptr));
    if(state->transform==VisualTransform{})return frame;
    mlt_frame_push_service(frame,state);mlt_frame_push_service_int(frame,mlt_frame_get_position(frame));mlt_frame_push_get_image(frame,transformImage);return frame;
}
int overlayImage(mlt_frame frame,uint8_t **pixels,mlt_image_format *format,int *width,int *height,int) {
    const int position=mlt_frame_pop_service_int(frame);auto *state=static_cast<OverlayState*>(mlt_frame_pop_service(frame));
    *format=mlt_image_rgba;const int error=mlt_frame_get_image(frame,pixels,format,width,height,1);
    if(error||!*pixels||*format!=mlt_image_rgba)return error?error:1;
    QImage target(*pixels,*width,*height,*width*4,QImage::Format_RGBA8888);QMutexLocker lock(&state->mutex);
    for(auto &s:state->sources){if(position<s.start||position>=s.start+s.frames)continue;
        const int sourceFrame=s.overlay.sourceInFrame+position-s.start;QImage image=s.still;
        if(s.producer){s.producer->seek(sourceFrame);std::unique_ptr<Mlt::Frame> f(s.producer->get_frame());if(!f||!f->is_valid())continue;
            int w=*width,h=*height;auto fmt=mlt_image_rgba;uint8_t *data=nullptr;
            if(mlt_frame_get_image(f->get_frame(),&data,&fmt,&w,&h,0)||!data||fmt!=mlt_image_rgba)continue;
            image=QImage(data,w,h,w*4,QImage::Format_RGBA8888).copy(contentRect({w,h},s.size).toAlignedRect().intersected(QRect(0,0,w,h)));
        }
        paint(target,image,s.overlay.transform,sourceFrame+s.overlay.sourceFrameOffset);
    }
    return 0;
}
mlt_frame overlayProcess(mlt_filter filter,mlt_frame frame) {
    auto *state=static_cast<OverlayState*>(mlt_properties_get_data(MLT_FILTER_PROPERTIES(filter),overlayKey,nullptr));
    mlt_frame_push_service(frame,state);mlt_frame_push_service_int(frame,mlt_filter_get_position(filter,frame));mlt_frame_push_get_image(frame,overlayImage);return frame;
}
}
bool attachTransform(Mlt::Producer &source,Mlt::Tractor &timeline,const Clip &clip,const QSizeF &size) {
    auto filter=mlt_filter_new();if(!filter)return false;
    auto *state=new TransformState{clip.id,clip.transform,size,clip.sourceFrameOffset};
    mlt_properties_set_data(MLT_FILTER_PROPERTIES(filter),transformKey,state,0,[](void *p){delete static_cast<TransformState*>(p);},nullptr);filter->process=transformProcess;
    const bool ok=mlt_service_attach(source.get_service(),filter)==0;
    if(ok)liveState(timeline).clips.push_back(state);
    mlt_filter_close(filter);return ok;
}
bool attachOverlays(Mlt::Tractor &timeline,Mlt::Profile &profile,const Project &project,QString *error,bool allowOffline) {
    if(project.visualOverlays.isEmpty())return true;
    auto state=std::make_unique<OverlayState>();
    // Lane zero is lowest. Within a lane the later item is above the earlier one.
    for(int lane=0;lane<3;++lane)if(project.visualLaneVisible[lane])for(int i=0;i<project.visualOverlays.size();++i){const auto &o=project.visualOverlays[i];if(o.lane!=lane)continue;
        OverlaySource s;s.overlay=o;s.index=i;s.start=project.overlayStartFrame(i);s.frames=project.overlayFrames(i);
        if(s.frames<1)continue;
        QImageReader reader(o.path);reader.setAutoTransform(true);s.still=reader.read();
        if(s.still.isNull()){
            if(!QFileInfo(o.path).isFile()){
                if(!allowOffline){if(error)*error="Overlay file not found: "+o.path;return false;}
                s.still=QImage(320,180,QImage::Format_ARGB32_Premultiplied);s.still.fill(QColor("#26272d"));
                QPainter label(&s.still);label.setPen(Qt::white);label.drawText(s.still.rect(),Qt::AlignCenter,"Offline overlay");label.end();
                state->sources.push_back(std::move(s));continue;
            }
            const auto encoded=QFile::encodeName(o.path);s.producer=std::make_unique<Mlt::Producer>(profile,encoded.constData());
            const int stream=s.producer->get_int("video_index");
            if(!s.producer->is_valid()||stream<0||qint64(o.sourceInFrame)+s.frames>s.producer->get_length()){if(error)*error="Cannot read overlay range: "+o.path;return false;}
            s.producer->set("audio_index",-1);const auto prefix=QString("meta.media.%1.codec.").arg(stream).toLatin1();
            s.size={double(s.producer->get_int((prefix+"width").constData())),double(s.producer->get_int((prefix+"height").constData()))};
            if(std::abs(s.producer->get_int((prefix+"rotate").constData()))%180==90)s.size.transpose();
        }
        state->sources.push_back(std::move(s));
    }
    auto filter=mlt_filter_new();if(!filter)return false;
    liveState(timeline).overlays=state.get();
    mlt_properties_set_data(MLT_FILTER_PROPERTIES(filter),overlayKey,state.release(),0,[](void *p){delete static_cast<OverlayState*>(p);},nullptr);filter->process=overlayProcess;
    const bool ok=mlt_service_attach(timeline.get_service(),filter)==0;mlt_filter_close(filter);return ok;
}
void update(Mlt::Tractor &timeline,const Project &project) {
    auto &state=liveState(timeline);
    for(auto *clip:state.clips)for(const auto &next:project.clips)if(next.id==clip->id){clip->transform=next.transform;break;}
    if(state.overlays){
        QMutexLocker lock(&state.overlays->mutex);
        for(auto &source:state.overlays->sources){
            const auto &next=project.visualOverlays[source.index];
            source.overlay.transform=next.transform;
            source.overlay.sourceInFrame=next.sourceInFrame-source.overlay.sourceFrameOffset;
            source.start=project.overlayStartFrame(source.index);source.frames=project.overlayFrames(source.index);
        }
    }
}
}
