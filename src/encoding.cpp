#include "encoding.h"

namespace Media {
QString hardwareH264Encoder() {
#ifdef Q_OS_MACOS
    return QStringLiteral("h264_videotoolbox");
#elif defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return QStringLiteral("h264_nvenc");
#else
    return {};
#endif
}
bool isHardwareH264Encoder(const QString &encoder) {
    return encoder == QLatin1String("h264_videotoolbox") || encoder == QLatin1String("h264_nvenc");
}
QString h264EncoderLabel(const QString &encoder) {
    if (encoder == QLatin1String("h264_nvenc")) return QStringLiteral("NVIDIA hardware encoding");
    if (encoder == QLatin1String("h264_videotoolbox")) return QStringLiteral("Apple hardware encoding");
    return QStringLiteral("software encoding");
}
}
