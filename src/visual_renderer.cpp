#include "visual_renderer.h"
#include <QPainter>
#include <QImageReader>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QCache>
#include <QHash>
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
struct OverlayDecoder {QMutex mutex;QSizeF size;std::unique_ptr<Mlt::Producer> producer;};
struct StillAsset;
struct OverlaySource {VisualOverlay overlay;int index=0,start=0,frames=0;bool visible=true;QImage offline;std::shared_ptr<OverlayDecoder> decoder;std::shared_ptr<StillAsset> still;};
constexpr auto errorKey="isaac.visual.error";
struct RenderError {QMutex mutex;QString message;};
void recordError(const std::shared_ptr<RenderError> &error,const QString &message){QMutexLocker lock(&error->mutex);if(error->message.isEmpty())error->message=message;}
struct OverlayState {QMutex mutex;std::vector<OverlaySource> sources;std::shared_ptr<RenderError> error=std::make_shared<RenderError>();};
struct OverlayFrame {int position=0;std::vector<OverlaySource> sources;std::shared_ptr<RenderError> error;};
struct StillKey {
    QString path;qint64 size=0,modified=0;
    bool operator==(const StillKey &other) const {return path==other.path&&size==other.size&&modified==other.modified;}
};
struct StillAsset {QMutex mutex;const StillKey key;QImage pixels;explicit StillAsset(const StillKey &identity):key(identity){}};
size_t qHash(const StillKey &key,size_t seed=0){return qHashMulti(seed,key.path,key.size,key.modified);}
StillKey stillKey(const QString &path){const QFileInfo info(path);return {info.canonicalFilePath(),info.size(),info.lastModified().toMSecsSinceEpoch()};}
std::shared_ptr<StillAsset> activeStill(const StillKey &key) {
    struct Registry {QMutex mutex;QHash<StillKey,std::weak_ptr<StillAsset>> assets;};
    static Registry registry;QMutexLocker lock(&registry.mutex);
    if(auto asset=registry.assets.value(key).lock())return asset;
    for(auto it=registry.assets.begin();it!=registry.assets.end();)if(it.value().expired())it=registry.assets.erase(it);else ++it;
    auto asset=std::make_shared<StillAsset>(key);registry.assets.insert(key,asset);return asset;
}
QImage stillImage(const QString &path,StillAsset &active) {
    struct Cache {QMutex mutex;QCache<StillKey,QImage> images{32*1024*1024};};
    static Cache cache;
    const auto key=stillKey(path);if(key.path.isEmpty()||!(active.key==key))return {};
    QMutexLocker sourceLock(&active.mutex);
    if(!active.pixels.isNull())return active.pixels;
    {QMutexLocker lock(&cache.mutex);if(auto *image=cache.images.object(key)){active.pixels=*image;return active.pixels;}}
    QImageReader reader(path);reader.setAutoTransform(true);auto image=reader.read();
    if(image.isNull()||!(stillKey(path)==key))return {};
    // Active references retain shared full-size pixels; the LRU bounds reuse after they are released.
    active.pixels=image;
    if(image.sizeInBytes()>cache.images.maxCost())return active.pixels;
    QMutexLocker lock(&cache.mutex);
    if(auto *existing=cache.images.object(key)){active.pixels=*existing;return active.pixels;}
    cache.images.insert(key,new QImage(image),int(image.sizeInBytes()));return active.pixels;
}
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
    auto *snapshot=static_cast<OverlayFrame*>(mlt_frame_pop_service(frame));
    *format=mlt_image_rgba;const int error=mlt_frame_get_image(frame,pixels,format,width,height,1);
    if(error||!*pixels||*format!=mlt_image_rgba)return error?error:1;
    QImage target(*pixels,*width,*height,*width*4,QImage::Format_RGBA8888);
    for(const auto &s:snapshot->sources){
        const int sourceFrame=s.overlay.sourceInFrame+snapshot->position-s.start;QImage image=s.offline;
        if(s.decoder){
            // A producer's seek/decode is mutable; retain it through the frame and copy before unlocking.
            auto &decoder=*s.decoder;QMutexLocker lock(&decoder.mutex);
            decoder.producer->seek(sourceFrame);std::unique_ptr<Mlt::Frame> f(decoder.producer->get_frame());
            if(!f||!f->is_valid()){recordError(snapshot->error,"Cannot decode overlay video: "+s.overlay.path);continue;}
            int w=*width,h=*height;auto fmt=mlt_image_rgba;uint8_t *data=nullptr;
            if(mlt_frame_get_image(f->get_frame(),&data,&fmt,&w,&h,0)||!data||fmt!=mlt_image_rgba){recordError(snapshot->error,"Cannot decode overlay video: "+s.overlay.path);continue;}
            image=QImage(data,w,h,w*4,QImage::Format_RGBA8888).copy(contentRect({w,h},decoder.size).toAlignedRect().intersected(QRect(0,0,w,h)));
        }else if(image.isNull()){
            image=stillImage(s.overlay.path,*s.still);
            if(image.isNull()){recordError(snapshot->error,"Cannot read overlay image: "+s.overlay.path);return 1;}
        }
        paint(target,image,s.overlay.transform,sourceFrame+s.overlay.sourceFrameOffset);
    }
    return 0;
}
mlt_frame overlayProcess(mlt_filter filter,mlt_frame frame) {
    auto *state=static_cast<OverlayState*>(mlt_properties_get_data(MLT_FILTER_PROPERTIES(filter),overlayKey,nullptr));
    const int position=mlt_filter_get_position(filter,frame);
    OverlayFrame snapshot;snapshot.position=position;snapshot.error=state->error;
    {QMutexLocker lock(&state->mutex);for(auto &source:state->sources){
        if(!source.visible||position<source.start||position>=source.start+source.frames){source.still.reset();continue;}
        if(!source.decoder&&source.offline.isNull()){
            const auto key=stillKey(source.overlay.path);
            if(!source.still||!(source.still->key==key))source.still=activeStill(key);
        }
        snapshot.sources.push_back(source);
    }}
    if(snapshot.sources.empty())return frame;
    auto *data=new OverlayFrame(std::move(snapshot));
    mlt_properties_set_data(MLT_FRAME_PROPERTIES(frame),"isaac.visual.frame",data,0,[](void *p){delete static_cast<OverlayFrame*>(p);},nullptr);
    mlt_frame_push_service(frame,data);mlt_frame_push_get_image(frame,overlayImage);return frame;
}
}
QString renderError(Mlt::Producer &producer) {
    auto *state=static_cast<std::shared_ptr<RenderError>*>(producer.get_data(errorKey));
    if(!state)return {};
    QMutexLocker lock(&(*state)->mutex);return (*state)->message;
}
void discardSpeculativeError(Mlt::Producer &producer,const QString &expected) {
    auto *state=static_cast<std::shared_ptr<RenderError>*>(producer.get_data(errorKey));if(!state)return;
    QMutexLocker lock(&(*state)->mutex);if((*state)->message==expected)(*state)->message.clear();
}
bool attachTransform(Mlt::Producer &source,Mlt::Tractor &timeline,const Clip &clip,const QSizeF &size) {
    for(int i=0;i<source.filter_count();++i){
        std::unique_ptr<Mlt::Filter> existing(source.filter(i));
        auto *state=static_cast<TransformState*>(existing->get_data(transformKey));
        if(state){
            *state={clip.id,clip.transform,size,clip.sourceFrameOffset};
            liveState(timeline).clips.push_back(state);
            return true;
        }
    }
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
        QImageReader reader(o.path);
        if(!reader.canRead()){
            if(!QFileInfo(o.path).isFile()){
                if(!allowOffline){if(error)*error="Overlay file not found: "+o.path;return false;}
                s.offline=QImage(320,180,QImage::Format_ARGB32_Premultiplied);s.offline.fill(QColor("#26272d"));
                QPainter label(&s.offline);label.setPen(Qt::white);label.drawText(s.offline.rect(),Qt::AlignCenter,"Offline overlay");label.end();
                state->sources.push_back(std::move(s));continue;
            }
            s.decoder=std::make_shared<OverlayDecoder>();auto &decoder=*s.decoder;
            const auto encoded=QFile::encodeName(o.path);decoder.producer=std::make_unique<Mlt::Producer>(profile,encoded.constData());
            const int stream=decoder.producer->get_int("video_index");
            if(!decoder.producer->is_valid()||stream<0||qint64(o.sourceInFrame)+s.frames>decoder.producer->get_length()){if(error)*error="Cannot read overlay range: "+o.path;return false;}
            decoder.producer->set("audio_index",-1);const auto prefix=QString("meta.media.%1.codec.").arg(stream).toLatin1();
            decoder.size={double(decoder.producer->get_int((prefix+"width").constData())),double(decoder.producer->get_int((prefix+"height").constData()))};
            if(std::abs(decoder.producer->get_int((prefix+"rotate").constData()))%180==90)decoder.size.transpose();
        }
        state->sources.push_back(std::move(s));
    }
    auto filter=mlt_filter_new();if(!filter)return false;
    auto *data=state.get();
    mlt_properties_set_data(MLT_FILTER_PROPERTIES(filter),overlayKey,state.release(),0,[](void *p){delete static_cast<OverlayState*>(p);},nullptr);filter->process=overlayProcess;
    const bool ok=mlt_service_attach(timeline.get_service(),filter)==0;
    if(ok){
        liveState(timeline).overlays=data;
        mlt_properties_set_data(timeline.get_properties(),errorKey,new std::shared_ptr<RenderError>(data->error),0,[](void *p){delete static_cast<std::shared_ptr<RenderError>*>(p);},nullptr);
    }
    mlt_filter_close(filter);return ok;
}
void update(Mlt::Tractor &timeline,const Project &project) {
    auto &state=liveState(timeline);
    for(auto *clip:state.clips)for(const auto &next:project.clips)if(next.id==clip->id){clip->transform=next.transform;break;}
    if(state.overlays){
        QMutexLocker lock(&state.overlays->mutex);
        for(auto &source:state.overlays->sources){
            if(source.index>=project.visualOverlays.size()){source.visible=false;continue;}
            const auto &next=project.visualOverlays[source.index];
            source.visible=project.visualLaneVisible[next.lane];
            source.overlay.transform=next.transform;
            source.overlay.sourceInFrame=next.sourceInFrame-source.overlay.sourceFrameOffset;
            source.start=project.overlayStartFrame(source.index);source.frames=project.overlayFrames(source.index);
        }
    }
}
}
