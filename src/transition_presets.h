#pragma once
#include <QString>
#include <QVector>
#include <QSize>

namespace Mlt { class Transition; }
namespace Transitions {
struct Preset { QString id, name; };
const QVector<Preset> &presets();
const Preset *find(const QString &id);
QString name(const QString &id);
bool configure(Mlt::Transition &transition, const QString &id, const QSize &size, QString *error);
}
