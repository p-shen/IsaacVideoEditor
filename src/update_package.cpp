#include "update_package.h"

#include <QJsonArray>
#include <QObject>
#include <QRegularExpression>
#include <QSysInfo>

QString updatePlatform() {
#if defined(Q_OS_MACOS)
    return QSysInfo::buildCpuArchitecture() == "arm64" ? "macos-arm64" : QString();
#elif defined(Q_OS_WIN)
    return QSysInfo::buildCpuArchitecture() == "x86_64" ? "windows-x86_64" : QString();
#elif defined(Q_OS_LINUX)
    return QSysInfo::buildCpuArchitecture() == "x86_64" ? "linux-x86_64" : QString();
#else
    return {};
#endif
}

UpdatePackage releaseUpdatePackage(const QJsonObject &release, const QString &platform, QString *error) {
    if (platform.isEmpty()) {
        *error = QObject::tr("In-app updates are not available for this computer's operating system or processor.");
        return {};
    }
    const auto tag = release.value("tag_name").toString();
    const auto name = "IsaacVideoEditor-" + tag + "-" + platform +
                      (platform.startsWith("linux-") ? ".AppImage" : ".zip");
    for (const auto &value : release.value("assets").toArray()) {
        const auto asset = value.toObject();
        if (asset.value("name").toString() != name) continue;
        static const QRegularExpression digest("\\Asha256:([a-fA-F0-9]{64})\\z");
        const auto match = digest.match(asset.value("digest").toString());
        const auto size = asset.value("size").toInteger();
        if (asset.value("state").toString() != "uploaded" || !match.hasMatch() || size <= 0 || size > 2147483648LL) {
            *error = QObject::tr("The release package is missing a valid size or SHA-256 checksum. Please try again after the release finishes publishing.");
            return {};
        }
        const auto url = QUrl::fromEncoded("https://github.com/p-shen/IsaacVideoEditor/releases/download/" +
                                          QUrl::toPercentEncoding(tag) + "/" + QUrl::toPercentEncoding(name));
        return {name, url, match.captured(1).toLatin1().toLower(), size};
    }
    *error = QObject::tr("The update's package for this computer is not ready yet. Please check again after the release finishes building.");
    return {};
}

bool safeUpdateArchiveEntry(const QString &entry, const QString &root) {
    if (entry.isEmpty() || entry.contains('\\') || entry.contains(':') || entry.contains(QChar::Null) ||
        entry.contains('\r') || entry.contains('\n')) return false;
    const auto parts = entry.split('/');
    if (parts.first() != root && parts.first() != "__MACOSX") return false;
    for (const auto &part : parts) if (part == "." || part == "..") return false;
    return true;
}
