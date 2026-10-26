#pragma once

#include <QString>

struct RuntimePaths {
    QString executables;
    QString mltRepository;
    QString mltData;
};

// An empty result preserves dependency discovery for development builds.
RuntimePaths bundledRuntimePaths(const QString &applicationDirectory);
