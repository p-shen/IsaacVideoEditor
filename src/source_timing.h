#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace SourceTiming {
inline QString fingerprint(const QString &path) {
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    if (!info.isFile() || canonical.isEmpty()) return {};
    const QByteArray identity = canonical.toUtf8() + '\0' + QByteArray::number(info.size()) + '\0'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + QByteArrayLiteral("\0verified-source-timing-v1");
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

// A sampled cadence cannot prove VFR safety; only a complete packet sequence permits a shifted origin.
inline int verifiedCadencePeriod(const QByteArray &json) {
    const auto root = QJsonDocument::fromJson(json).object();
    const auto streams = root.value("streams").toArray();
    const auto packets = root.value("packets").toArray();
    if (streams.size() != 1 || packets.size() < 2) return 0;
    const auto rational = [](const QString &value) {
        const auto parts = value.split('/');
        return parts.size() == 2 ? qMakePair(qint64(parts[0].toInt()), qint64(parts[1].toInt()))
                                 : qMakePair(qint64(0), qint64(0));
    };
    const auto stream = streams.first().toObject();
    const auto rate = rational(stream.value("r_frame_rate").toString());
    const auto average = rational(stream.value("avg_frame_rate").toString());
    const auto clock = rational(stream.value("time_base").toString());
    if (rate.first <= 0 || rate.second <= 0 || average.first <= 0 || average.second <= 0 ||
        clock.first <= 0 || clock.second <= 0 || rate.first * average.second != average.first * rate.second) return 0;
    QVector<qint64> timestamps;
    timestamps.reserve(packets.size());
    for (const auto &value : packets) {
        const auto packet = value.toObject();
        if (!packet.value("pts").isDouble()) return 0;
        timestamps.append(packet.value("pts").toInteger());
    }
    std::sort(timestamps.begin(), timestamps.end());
    if (timestamps.first() != 0) return 0;
    const long double unitsPerTick = static_cast<long double>(rate.first) * clock.first;
    for (int frame = 0; frame < timestamps.size(); ++frame) {
        const long double actual = static_cast<long double>(timestamps[frame]) * unitsPerTick;
        const long double expected = static_cast<long double>(frame) * rate.second * clock.second;
        if (std::abs(actual - expected) > unitsPerTick * 0.51L) return 0;
    }
    const qint64 cadence = 60 * rate.second / std::gcd(rate.first, 60 * rate.second);
    const qint64 tick = 60 * clock.first / std::gcd(clock.second, 60 * clock.first);
    if (cadence > 3600 || tick > 3600) return 0;
    const qint64 aligned = std::lcm(cadence, tick);
    return aligned <= 3600 ? int(aligned) : 0;
}

inline bool verifiedCfr60(const QByteArray &json) {
    if (verifiedCadencePeriod(json) != 1) return false;
    const auto streams = QJsonDocument::fromJson(json).object().value("streams").toArray();
    const auto rate = streams.first().toObject().value("r_frame_rate").toString().split('/');
    return rate.size() == 2 && rate[1].toLongLong() > 0 && rate[0].toLongLong() == 60 * rate[1].toLongLong();
}
}
