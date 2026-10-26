#include "preview_item.h"
#include "editor_controller.h"
#include "media_cache.h"
#include "text_renderer.h"
#include "mlt_engine.h"
#include <QImageReader>
#include <cmath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPalette>
#include <QtMath>
namespace {
void drawFit(QPainter *p,const QImage &image,const QRectF &bounds) {
    p->fillRect(bounds,QColor("#101114"));
    if(image.isNull()) return;
    QSizeF size=image.size(); size.scale(bounds.size(),Qt::KeepAspectRatio);
    QRectF target(QPointF((bounds.width()-size.width())/2,(bounds.height()-size.height())/2),size);
    p->setRenderHint(QPainter::SmoothPixmapTransform); p->drawImage(target,image);
}
}
VideoPreview::VideoPreview(QQuickItem *parent):QQuickPaintedItem(parent) { setOpaquePainting(true);setAcceptedMouseButtons(Qt::LeftButton);setActiveFocusOnTab(true); }
void VideoPreview::setController(EditorController *c) {
    if(controller_==c) return;
    if(controller_) disconnect(controller_,nullptr,this,nullptr);
    controller_=c;
    if(c){
        connect(c,&EditorController::compositionChanged,this,[this]{update();});
        connect(c,&EditorController::frameChanged,this,[this]{update();});
        connect(c,&EditorController::selectionChanged,this,[this]{update();});
        connect(c,&EditorController::positionChanged,this,[this]{update();});
    }
    emit controllerChanged(); update();
}
QRectF VideoPreview::imageRect() const {
    if(!controller_||controller_->frame().isNull())return {};
    QSizeF size=controller_->frame().size();size.scale(boundingRect().size(),Qt::KeepAspectRatio);
    return {QPointF((width()-size.width())/2,(height()-size.height())/2),size};
}
QRectF VideoPreview::textRect(int index) const {return timedRect(index,false);}
QRectF VideoPreview::captionRect(int index) const {return timedRect(index,true);}
QRectF VideoPreview::timedRect(int index,bool caption) const {
    if(!controller_||controller_->sourceMode()||imageRect().isEmpty()||(caption&&!controller_->burnCaptions()))return {};
    const auto &project=controller_->project();const auto &list=caption?project.captions:project.textOverlays;if(index<0||index>=list.size())return {};
    const auto &t=list[index];const int start=project.textStartFrame(index,caption);
    if(controller_->position()<start||controller_->position()>=start+project.textFrames(index,caption))return {};
    const auto canvas=controller_->frame().size();const auto target=imageRect();const auto bounds=TextRendering::bounds(t,canvas);
    return {target.left()+bounds.x()*target.width()/canvas.width(),target.top()+bounds.y()*target.height()/canvas.height(),bounds.width()*target.width()/canvas.width(),bounds.height()*target.height()/canvas.height()};
}
QPolygonF VideoPreview::transformPolygon() const {
    if(!controller_||controller_->sourceMode()||!controller_->transformMode())return {};
    const auto t=controller_->currentTransform();if(t.isEmpty()||!t.value("active").toBool())return {};
    const auto canvas=imageRect();if(canvas.isEmpty())return {};
    QString path;int rotation=0;
    if(controller_->selectedOverlay()>=0)path=controller_->project().visualOverlays[controller_->selectedOverlay()].path;
    else if(controller_->selectedClip()>=0){const auto &c=controller_->project().clips[controller_->selectedClip()];path=c.path;rotation=c.rotationDegrees;}
    QImageReader reader(path);QSizeF size=reader.size();int cameraRotation=0;
    if(size.isEmpty()){const auto info=Media::mediaInfo(path);if(info){size={double(info->width),double(info->height)};cameraRotation=info->cameraRotationDegrees;}}
    else if(reader.transformation().testFlag(QImageIOHandler::TransformationRotate90))cameraRotation=90;
    if(size.isEmpty())size=canvas.size();if((rotation+cameraRotation)%180)size.transpose();
    if(t.value("framing").toString()=="crop")size={size.width()*(1-t.value("cropLeft").toDouble()-t.value("cropRight").toDouble()),size.height()*(1-t.value("cropTop").toDouble()-t.value("cropBottom").toDouble())};
    size.scale(canvas.size(),t.value("framing").toString()=="fill"?Qt::KeepAspectRatioByExpanding:Qt::KeepAspectRatio);size*=t.value("scale").toDouble();
    QTransform m;m.translate(canvas.left()+t.value("x").toDouble()*canvas.width(),canvas.top()+t.value("y").toDouble()*canvas.height());m.rotate(t.value("rotation").toDouble()-rotation);
    return m.map(QPolygonF(QRectF(-size.width()/2,-size.height()/2,size.width(),size.height())));
}
QPointF VideoPreview::rotationHandle() const {
    const auto polygon=transformPolygon();if(polygon.size()<4)return {};
    const QPointF top=(polygon[0]+polygon[1])/2,center=(polygon[0]+polygon[2])/2;const auto vector=top-center;const double distance=std::hypot(vector.x(),vector.y());return distance>0?top+vector*(24/distance):top;
}
void VideoPreview::paint(QPainter *p) {
    drawFit(p,controller_?controller_->frame():QImage(),boundingRect());
    if(controller_&&!controller_->playing()&&controller_->transformMode()){
        const auto polygon=transformPolygon();if(polygon.isEmpty())return;
        const auto accent=QGuiApplication::palette().highlight().color();p->setPen(QPen(accent,2));p->setBrush(Qt::NoBrush);p->drawPolygon(polygon);
        p->drawLine((polygon[0]+polygon[1])/2,rotationHandle());p->setBrush(accent);p->drawEllipse(rotationHandle(),5,5);
        for(int i=0;i<4;++i)p->drawRect(QRectF(polygon[i]-QPointF(4,4),QSizeF(8,8)));
        if(transformDrag_){const auto r=imageRect();p->setPen(QPen(accent,1,Qt::DashLine));p->drawLine(QPointF(r.center().x(),r.top()),QPointF(r.center().x(),r.bottom()));p->drawLine(QPointF(r.left(),r.center().y()),QPointF(r.right(),r.center().y()));
            const auto t=controller_->currentTransform();p->setPen(Qt::white);p->drawText(QRectF(8,8,width()-16,45),Qt::AlignLeft,QString("X %1  Y %2  Scale %3  Rotation %4°").arg(t["x"].toDouble(),0,'f',3).arg(t["y"].toDouble(),0,'f',3).arg(t["scale"].toDouble(),0,'f',3).arg(t["rotation"].toDouble(),0,'f',1));}
        return;
    }
    if(controller_&&!controller_->playing()){
        const auto accent=QGuiApplication::palette().highlight().color();p->setBrush(Qt::NoBrush);
        if(!controller_->sourceMode()&&controller_->textGuides()&&(controller_->selectedText()>=0||controller_->selectedCaption()>=0)){
            const auto canvas=imageRect();p->setPen(QPen(accent,1,Qt::DashLine));p->drawRect(canvas.adjusted(canvas.width()*.1,canvas.height()*.1,-canvas.width()*.1,-canvas.height()*.1));
        }
        const auto rect=controller_->selectedCaption()>=0?captionRect(controller_->selectedCaption()):textRect(controller_->selectedText());if(rect.isEmpty())return;
        p->setPen(QPen(accent,2));p->drawRect(rect);
        p->setBrush(accent);for(const auto &point:{rect.topLeft(),rect.topRight(),rect.bottomLeft(),rect.bottomRight()})p->drawRect(QRectF(point-QPointF(3,3),QSizeF(6,6)));
    }
}
void VideoPreview::mousePressEvent(QMouseEvent *e){
    if(!controller_||e->button()!=Qt::LeftButton){e->ignore();return;}
    if(!controller_->sourceMode()&&controller_->transformMode()){
        const auto polygon=transformPolygon();if(polygon.isEmpty()){e->ignore();return;}
        transformDrag_=0;
        if(QLineF(e->position(),rotationHandle()).length()<13)transformDrag_=3;
        else for(int i=0;i<4;++i)if(QLineF(e->position(),polygon[i]).length()<13)transformDrag_=2;
        if(!transformDrag_&&polygon.containsPoint(e->position(),Qt::OddEvenFill))transformDrag_=1;
        if(transformDrag_){forceActiveFocus(Qt::MouseFocusReason);transformPress_=e->position();transformCenter_=(polygon[0]+polygon[2])/2;transformOrigin_=controller_->currentTransform();controller_->beginVisualGesture();setKeepMouseGrab(true);e->accept();return;}
        e->ignore();return;
    }
    for(bool caption:{true,false}){const int count=caption?controller_->project().captions.size():controller_->project().textOverlays.size();for(int i=count-1;i>=0;--i){const auto rect=timedRect(i,caption);if(!rect.isEmpty()&&rect.contains(e->position())){
        forceActiveFocus(Qt::MouseFocusReason);if(caption){controller_->beginCaptionEdit(i);emit captionSelected();}else{controller_->beginTextDrag(i);emit textSelected();}
        const auto image=imageRect();textPress_=e->position();textOrigin_={(rect.center().x()-image.left())/image.width(),(rect.center().y()-image.top())/image.height()};
        textDragging_=true;setKeepMouseGrab(true);e->accept();return;
    }}}e->ignore();
}
void VideoPreview::mouseMoveEvent(QMouseEvent *e){
    if(transformDrag_&&controller_){const auto r=imageRect();if(r.isEmpty())return;
        if(transformDrag_==1){double x=transformOrigin_["x"].toDouble()+(e->position().x()-transformPress_.x())/r.width(),y=transformOrigin_["y"].toDouble()+(e->position().y()-transformPress_.y())/r.height();if(std::abs(x-.5)<.008)x=.5;if(std::abs(y-.5)<.008)y=.5;controller_->setTransform({{"x",std::clamp(x,-10.,10.)},{"y",std::clamp(y,-10.,10.)}});}
        else if(transformDrag_==2){const double base=QLineF(transformCenter_,transformPress_).length();if(base>0)controller_->setTransform({{"scale",std::clamp(transformOrigin_["scale"].toDouble()*QLineF(transformCenter_,e->position()).length()/base,.01,100.)}});}
        else {const auto a=transformPress_-transformCenter_,b=e->position()-transformCenter_;controller_->setTransform({{"rotation",transformOrigin_["rotation"].toDouble()+(std::atan2(b.y(),b.x())-std::atan2(a.y(),a.x()))*180/M_PI}});}
        e->accept();return;
    }
    if(!textDragging_||!controller_){e->ignore();return;}const auto rect=imageRect();if(rect.isEmpty())return;
    controller_->dragTextTo(textOrigin_.x()+(e->position().x()-textPress_.x())/rect.width(),textOrigin_.y()+(e->position().y()-textPress_.y())/rect.height());e->accept();
}
void VideoPreview::mouseReleaseEvent(QMouseEvent *e){
    if(transformDrag_&&controller_){mouseMoveEvent(e);transformDrag_=0;setKeepMouseGrab(false);controller_->endVisualGesture(false);update();e->accept();return;}
    if(textDragging_&&controller_){mouseMoveEvent(e);textDragging_=false;setKeepMouseGrab(false);controller_->endTextDrag(false);}e->accept();
}
void VideoPreview::mouseUngrabEvent(){if(transformDrag_&&controller_){transformDrag_=0;setKeepMouseGrab(false);controller_->endVisualGesture(true);update();}if(textDragging_&&controller_){textDragging_=false;setKeepMouseGrab(false);controller_->endTextDrag(true);}}
void VideoPreview::keyPressEvent(QKeyEvent *e){
    if(e->key()==Qt::Key_Escape&&(textDragging_||transformDrag_)){mouseUngrabEvent();e->accept();return;}
    if(controller_&&!controller_->sourceMode()&&(controller_->selectedText()>=0||controller_->selectedCaption()>=0)&&e->key()>=Qt::Key_Left&&e->key()<=Qt::Key_Down){
        const auto rect=imageRect();if(rect.isEmpty())return;const bool caption=controller_->selectedCaption()>=0;const auto t=caption?controller_->project().captions[controller_->selectedCaption()]:controller_->project().textOverlays[controller_->selectedText()];const double step=e->modifiers().testFlag(Qt::ShiftModifier)?10:1;
        if(caption)controller_->beginCaptionEdit(controller_->selectedCaption());else controller_->beginTextDrag();controller_->dragTextTo(t.x+(e->key()==Qt::Key_Left?-step:e->key()==Qt::Key_Right?step:0)/rect.width(),t.y+(e->key()==Qt::Key_Up?-step:e->key()==Qt::Key_Down?step:0)/rect.height());controller_->endTextDrag(false);e->accept();
    }else QQuickPaintedItem::keyPressEvent(e);
}
CachedFrame::CachedFrame(QQuickItem *parent):QQuickPaintedItem(parent) { setOpaquePainting(true); }
void CachedFrame::setCache(MediaCache *c) {
    if(cache_==c) return; if(cache_) disconnect(cache_,nullptr,this,nullptr);
    cache_=c; if(c) connect(c,&MediaCache::changed,this,&CachedFrame::refresh); emit changed(); refresh();
}
void CachedFrame::setSource(const QString &s) { if(source_==s) return; source_=s; emit changed(); refresh(); }
void CachedFrame::setSeconds(double s) { if(qFuzzyCompare(seconds_+1,s+1)) return; seconds_=s; emit changed(); refresh(); }
void CachedFrame::setPriority(bool p) { if(priority_==p) return; priority_=p; emit changed(); refresh(); }
void CachedFrame::setSourceRotation(int r) { if(rotation_==r) return; rotation_=r; emit changed(); refresh(); }
void CachedFrame::refresh() { const bool wasReady=ready(); image_=cache_&&!source_.isEmpty()?cache_->thumbnail(source_,seconds_,rotation_,priority_):QImage(); if(wasReady!=ready()) emit readyChanged(); update(); }
void CachedFrame::paint(QPainter *p) { drawFit(p,image_,boundingRect()); }
