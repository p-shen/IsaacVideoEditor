#include "runtime_paths.h"

#include <QDir>
#include <utility>

RuntimePaths bundledRuntimePaths(const QString &applicationDirectory)
{
    const QDir executableDirectory(applicationDirectory);
    const std::pair<QString, QString> layouts[] = {
        {"../PlugIns/mlt-7", "../Resources/mlt-7"},
        {"../lib/mlt-7", "../share/mlt-7"},
        {"lib/mlt-7", "share/mlt-7"},
    };
    for (const auto &layout : layouts) {
        const auto repository = QDir::cleanPath(executableDirectory.absoluteFilePath(layout.first));
        const auto data = QDir::cleanPath(executableDirectory.absoluteFilePath(layout.second));
        if (QDir(repository).exists() && QDir(data).exists())
            return {executableDirectory.absolutePath(), repository, data};
    }
    return {};
}
