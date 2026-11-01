#pragma once
#include <QString>

namespace Media {
QString hardwareH264Encoder();
bool isHardwareH264Encoder(const QString &encoder);
QString h264EncoderLabel(const QString &encoder);
}
