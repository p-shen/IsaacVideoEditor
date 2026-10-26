#pragma once
#include "project.h"
#include <QByteArray>
namespace Subtitles {
std::optional<QVector<TextOverlay>> parseSrt(const QByteArray &bytes,QString *error=nullptr);
QByteArray writeSrt(const Project &project);
}
