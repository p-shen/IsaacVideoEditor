#pragma once

#include "project.h"

#include <Mlt.h>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

namespace Media {

// Must run before creating any MLT service, including preview consumers.
void initializeMlt();

// Preview tracks source identity independently of the producer's decoded-frame caches.
QStringList sourceFiles(Mlt::Producer &producer);
void setSourceFiles(Mlt::Producer &producer,const QStringList &paths);

struct MediaInfo {
    double durationSeconds = 0;
    int width = 0;
    int height = 0;
    bool hasAudio = false;
    int cameraRotationDegrees = 0;
};

struct VideoSize {
    int width = 0;
    int height = 0;
};

// Native source dimensions, independent of the timeline's rendering profile.
std::optional<MediaInfo> mediaInfo(const QString &path, QString *error = nullptr);
// Background preparation may retry an unchanged failed source while GUI lookups stay cached.
std::optional<MediaInfo> retryMediaInfo(const QString &path, QString *error = nullptr);

// Completed audio downloads, newest first; the caller chooses the directory.
QStringList recentAudioFiles(const QString &directory, int limit = 10);

// The smallest source raster, with camera orientation, determines the auto canvas.
std::optional<VideoSize> projectOutputSize(const Project &project, QString *error = nullptr, bool allowOffline = false);

// Native dimensions by default; iPhone mode fits within a 4K envelope without changing the canvas.
std::optional<VideoSize> mp4OutputSize(const Project &project, bool optimizeForIphone = false, QString *error = nullptr);

// Square-pixel, progressive 60 fps output at the requested canvas size.
void configureOutputProfile(Mlt::Profile &profile, int width = 1920, int height = 1080);

// The caller must keep profile alive while using the returned timeline.
// A stopped previous timeline with the same profile can lend unchanged video decoders.
std::unique_ptr<Mlt::Tractor> buildTimeline(const Project &project,
                                            Mlt::Profile &profile,
                                            QString *error = nullptr, bool allowOffline = false,
                                            Mlt::Tractor *previousTimeline = nullptr);

// The progress callback receives a percentage. Export runs on the calling thread.
bool exportMp4(const Project &project, const QString &path,
               std::function<void(int)> progress = {}, QString *error = nullptr,
               std::function<bool()> canceled = {},
               std::function<void(const QString &)> diagnostic = {}, bool softwareOnly = false,
               bool optimizeForIphone = false);

// Used when adding a clip or audio file to display its available duration.
double mediaDurationSeconds(const QString &path, QString *error = nullptr);

} // namespace Media
