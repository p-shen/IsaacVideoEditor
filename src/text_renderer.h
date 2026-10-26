#pragma once
#include "project.h"
#include <QImage>
#include <QRectF>
#include <mlt++/Mlt.h>

namespace TextRendering {
QRectF bounds(const TextOverlay &text, const QSize &canvas);
void paint(QImage &image, const QVector<TextOverlay> &texts, int frame);
bool attach(Mlt::Tractor &timeline, const QVector<TextOverlay> &texts);
void update(Mlt::Tractor &timeline, const QVector<TextOverlay> &texts);
}
