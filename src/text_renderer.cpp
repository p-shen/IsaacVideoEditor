#include "text_renderer.h"
#include <QColor>
#include <QFont>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QTextLayout>
#include <algorithm>

namespace {
constexpr auto stateKey="isaac.text.state";
struct RenderState { QMutex mutex; QVector<TextOverlay> texts; };
struct Layout {
    QTextLayout text;
    QRectF rect;
    qreal padding=0,scale=1;
    Layout(const TextOverlay &t,const QSize &canvas) {
        if(canvas.isEmpty() || t.text.isEmpty())return;
        scale=canvas.height()/1080.0;
        const QSizeF logicalCanvas(canvas.width()/scale,1080);
        QFont font(t.fontFamily);font.setPixelSize(t.fontSize);font.setBold(t.bold);
        QString content=t.text;content.replace('\n',QChar::LineSeparator);
        text.setText(content);text.setFont(font);
        QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        option.setAlignment(t.style=="box"?Qt::AlignLeft:Qt::AlignHCenter);text.setTextOption(option);
        text.beginLayout();qreal y=0,w=0;
        while(true){auto line=text.createLine();if(!line.isValid())break;line.setLineWidth(logicalCanvas.width()*.86);line.setPosition({0,y});y+=line.height();w=std::max(w,line.naturalTextWidth());}
        text.endLayout();
        text.beginLayout();y=0;
        while(true){auto line=text.createLine();if(!line.isValid())break;line.setLineWidth(w+1);line.setPosition({0,y});y+=line.height();}
        text.endLayout();
        padding=18;
        const QSizeF size(std::min(logicalCanvas.width(),w+1+2*padding),std::min(logicalCanvas.height(),y+2*padding));
        const qreal x=std::clamp(t.x*logicalCanvas.width()-size.width()/2,0.0,logicalCanvas.width()-size.width());
        const qreal top=std::clamp(t.y*logicalCanvas.height()-size.height()/2,0.0,logicalCanvas.height()-size.height());
        rect={QPointF(x,top),size};
    }
};
int getImage(mlt_frame frame,uint8_t **pixels,mlt_image_format *format,int *width,int *height,int) {
    const int position=mlt_frame_pop_service_int(frame);
    auto *state=static_cast<RenderState*>(mlt_frame_pop_service(frame));
    *format=mlt_image_rgba;
    const int error=mlt_frame_get_image(frame,pixels,format,width,height,1);
    if(error || !*pixels || *format!=mlt_image_rgba)return error?error:1;
    QVector<TextOverlay> texts;{QMutexLocker lock(&state->mutex);texts=state->texts;}
    QImage image(*pixels,*width,*height,*width*4,QImage::Format_RGBA8888);
    TextRendering::paint(image,texts,position);
    return 0;
}
mlt_frame process(mlt_filter filter,mlt_frame frame) {
    auto *state=static_cast<RenderState*>(mlt_properties_get_data(MLT_FILTER_PROPERTIES(filter),stateKey,nullptr));
    const int position=mlt_filter_get_position(filter,frame);
    bool active=false;
    {QMutexLocker lock(&state->mutex);for(const auto &t:state->texts)active |= !t.text.isEmpty() && position>=t.startFrame && position<t.startFrame+t.durationFrames;}
    if(active){mlt_frame_push_service(frame,state);mlt_frame_push_service_int(frame,position);mlt_frame_push_get_image(frame,getImage);}
    return frame;
}
}
namespace TextRendering {
QRectF bounds(const TextOverlay &text,const QSize &canvas){const Layout layout(text,canvas);return {layout.rect.topLeft()*layout.scale,layout.rect.size()*layout.scale};}
void paint(QImage &image,const QVector<TextOverlay> &texts,int frame) {
    QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::TextAntialiasing);
    for(const auto &t:texts){
        if(frame<t.startFrame || frame>=t.startFrame+t.durationFrames || t.text.isEmpty())continue;
        Layout layout(t,image.size());if(layout.rect.isEmpty())continue;
        p.save();p.scale(layout.scale,layout.scale);p.setClipRect(layout.rect);
        if(t.fade&&t.durationFrames>2){const double ramp=std::min(27.0,(t.durationFrames-1)/2.0);p.setOpacity(std::clamp(std::min(frame-t.startFrame,t.startFrame+t.durationFrames-1-frame)/ramp,0.0,1.0));}
        if(t.style=="box"){p.setPen(Qt::NoPen);p.setBrush(QColor(10,24,23,220));p.drawRoundedRect(layout.rect,8,8);}
        const QPointF origin=layout.rect.topLeft()+QPointF(layout.padding,layout.padding);
        if(t.shadow){p.setPen(QColor(0,0,0,210));layout.text.draw(&p,origin+QPointF(2,3));}
        p.setPen(QColor(t.color));layout.text.draw(&p,origin);p.restore();
    }
}
bool attach(Mlt::Tractor &timeline,const QVector<TextOverlay> &texts) {
    auto filter=mlt_filter_new();if(!filter)return false;
    auto *state=new RenderState;state->texts=texts;
    mlt_properties_set_data(MLT_FILTER_PROPERTIES(filter),stateKey,state,0,[](void *p){delete static_cast<RenderState*>(p);},nullptr);
    filter->process=process;
    const bool attached=mlt_service_attach(timeline.get_service(),filter)==0;
    if(attached)mlt_properties_set_data(timeline.get_properties(),stateKey,state,0,nullptr,nullptr);
    mlt_filter_close(filter);
    return attached;
}
void update(Mlt::Tractor &timeline,const QVector<TextOverlay> &texts) {
    auto *state=static_cast<RenderState*>(mlt_properties_get_data(timeline.get_properties(),stateKey,nullptr));
    if(state){QMutexLocker lock(&state->mutex);state->texts=texts;}
}
}
