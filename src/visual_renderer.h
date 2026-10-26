#pragma once
#include "project.h"
#include <QImage>
#include <QRectF>
#include <mlt++/Mlt.h>
namespace VisualRendering {
// All coordinates are normalized to the canvas; source frames survive trim/split/proxy changes.
QRectF contentRect(const QSize &canvas,const QSizeF &source);
void paint(QImage &canvas,const QImage &source,const VisualTransform &transform,int sourceFrame);
bool attachTransform(Mlt::Producer &source,Mlt::Tractor &timeline,const Clip &clip,const QSizeF &sourceSize);
bool attachOverlays(Mlt::Tractor &timeline,Mlt::Profile &profile,const Project &project,QString *error,bool allowOffline=false);
// Preview serializes this with playback and decoding, including transport changes.
void update(Mlt::Tractor &timeline,const Project &project);
}
