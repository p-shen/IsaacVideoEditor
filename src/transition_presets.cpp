#include "transition_presets.h"
#include <Mlt.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>

namespace Transitions {
const QVector<Preset> &presets() {
    static const QVector<Preset> values{
        {"cut", "Cut"}, {"dissolve", "Dissolve"},
        {"wipe-left", "Wipe left"}, {"wipe-right", "Wipe right"},
        {"wipe-up", "Wipe up"}, {"wipe-down", "Wipe down"},
        {"diagonal-down-right", "Diagonal down-right"}, {"diagonal-up-left", "Diagonal up-left"},
        {"circle-open", "Circle open"}, {"circle-close", "Circle close"},
        {"barn-doors-horizontal", "Barn doors horizontal"}, {"barn-doors-vertical", "Barn doors vertical"},
        {"blinds-horizontal", "Blinds horizontal"}, {"blinds-vertical", "Blinds vertical"}
    };
    return values;
}
const Preset *find(const QString &id) {
    const auto &values = presets();
    const auto it = std::find_if(values.cbegin(), values.cend(), [&](const Preset &p) { return p.id == id; });
    return it == values.cend() ? nullptr : &*it;
}
QString name(const QString &id) {
    const auto *preset = find(id);
    return preset ? preset->name : QString{};
}

namespace {
QString maskPath(int effect, QSize size) {
    struct Cache {
        QMutex mutex;
        QTemporaryDir directory{QDir::tempPath() + "/isaac-transitions-XXXXXX"};
        QHash<QString, QString> paths;
    };
    static Cache cache;
    QMutexLocker lock(&cache.mutex);
    if (!cache.directory.isValid() || size.isEmpty()) return {};
    // Bound disk/memory use; MLT scales the map and softens its edges at render resolution.
    size.scale(512, 512, Qt::KeepAspectRatio);
    size.setWidth(std::max(2, size.width())); size.setHeight(std::max(2, size.height()));
    const auto key = presets()[effect].id + QString("-%1x%2").arg(size.width()).arg(size.height());
    if (const auto path = cache.paths.value(key); !path.isEmpty() && QFileInfo::exists(path)) return path;
    QByteArray pgm = "P5\n" + QByteArray::number(size.width()) + " " + QByteArray::number(size.height()) + "\n65535\n";
    pgm.reserve(pgm.size() + size.width() * size.height() * 2);
    for (int y = 0; y < size.height(); ++y) for (int x = 0; x < size.width(); ++x) {
        const double u = (x + .5) / size.width(), v = (y + .5) / size.height();
        double value = 0;
        switch (effect) {
        case 2: value = 1 - u; break;
        case 3: value = u; break;
        case 4: value = 1 - v; break;
        case 5: value = v; break;
        case 6: value = (u + v) / 2; break;
        case 7: value = 1 - (u + v) / 2; break;
        case 8: case 9:
            value = std::hypot((u - .5) * size.width(), (v - .5) * size.height()) /
                    std::hypot(size.width() / 2.0, size.height() / 2.0);
            if (effect == 9) value = 1 - value;
            break;
        case 10: value = std::abs(u - .5) * 2; break;
        case 11: value = std::abs(v - .5) * 2; break;
        case 12: value = v * 8 - std::floor(v * 8); break;
        case 13: value = u * 8 - std::floor(u * 8); break;
        }
        const auto gray = int(std::round(std::clamp(value, 0.0, 1.0) * 65535));
        pgm.append(char(gray >> 8)); pgm.append(char(gray & 255));
    }
    const auto path = cache.directory.filePath(key + ".pgm");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(pgm) != pgm.size() || !file.commit()) return {};
    cache.paths.insert(key, path);
    return path;
}
}

bool configure(Mlt::Transition &transition, const QString &id, const QSize &size, QString *error) {
    const auto *preset = find(id);
    if (!preset || id == "cut") {
        if (error) *error = "Unknown video transition.";
        return false;
    }
    if (id == "dissolve") return true;
    const auto path = maskPath(int(preset - presets().constData()), size);
    if (path.isEmpty()) {
        if (error) *error = "Could not prepare the " + preset->name + " transition.";
        return false;
    }
    transition.set("resource", QFile::encodeName(path).constData());
    transition.set("softness", .02);
    transition.set("progressive", 1);
    return true;
}
}
