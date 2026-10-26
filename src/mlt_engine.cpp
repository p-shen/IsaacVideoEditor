#include "mlt_engine.h"
#include "text_renderer.h"
#include "visual_renderer.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QtMath>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <numeric>
#include <thread>

namespace Media {

void initializeMlt() {
    static std::once_flag once;
    std::call_once(once, [] { Mlt::Factory::init(); });
}

namespace {

struct CachedMediaInfo {
    qint64 bytes = 0;
    qint64 modified = 0;
    MediaInfo info;
};

std::mutex mediaInfoMutex;
QHash<QString, CachedMediaInfo> mediaInfoCache;

std::unique_ptr<Mlt::Producer> openMedia(Mlt::Profile &profile,
                                         const QString &path,
                                         QString *error) {
    if (!QFileInfo(path).isFile()) {
        if (error) *error = QString("File not found: %1").arg(path);
        return {};
    }
    const QByteArray encodedPath = QFile::encodeName(path);
    auto producer = std::make_unique<Mlt::Producer>(profile, encodedPath.constData());
    if (!producer->is_valid() || producer->get_length() < 1) {
        if (error) *error = QString("Cannot read media file: %1").arg(path);
        return {};
    }
    return producer;
}

bool applyAudio(Mlt::Producer &source,Mlt::Profile &profile,const AudioControls &a,int start,int frames,QString *error) {
    Mlt::Filter volume(profile,"volume");
    if(!volume.is_valid()){if(error)*error="Audio volume filter is unavailable.";return false;}
    if(a.muted)source.set("set.test_audio",1);
    volume.set("level",a.muted?-100.0:a.gainDb);source.attach(volume);
    const int fadeIn=std::min(a.fadeInFrames,frames),fadeOut=std::min(a.fadeOutFrames,frames);
    for(int direction=0;direction<2;++direction){
        const int length=direction?fadeOut:fadeIn;if(length<=0)continue;
        Mlt::Filter fade(profile,"volume");if(!fade.is_valid())return false;
        fade.set("gain",direction?1.0:0.0);fade.set("end",direction?0.0:1.0);
        const int first=direction?start+frames-length:start;
        fade.set_in_and_out(first,first+length-1);source.attach(fade);
    }
    return true;
}

bool addAudioTrack(Mlt::Tractor &timeline, Mlt::Profile &profile,
                   const QString &path, int startFrame, int totalFrames,
                   int trackNumber, const AudioControls &controls, bool loop,
                   QString *error) {
    auto source = openMedia(profile, path, error);
    if (!source) return false;
    source->set("video_index", -1);
    const int sourceFrames = source->get_length();
    if (sourceFrames < 1) {
        if (error) *error = QString("Audio file has no playable duration: %1").arg(path);
        return false;
    }

    Mlt::Playlist track(profile);
    if (!track.is_valid()) {
        if (error) *error = "Could not create an audio track.";
        return false;
    }
    if (startFrame > 0) track.blank(startFrame - 1);

    int remaining = totalFrames - startFrame;
    while (remaining > 0) {
        const int count = std::min(sourceFrames, remaining);
        if (track.append(*source, 0, count - 1) != 0) {
            if (error) *error = QString("Could not add audio file: %1").arg(path);
            return false;
        }
        remaining -= count;
        if (!loop) break;
    }

    const int playable=std::max(0,std::min(totalFrames-startFrame,loop?totalFrames:sourceFrames));
    if(!applyAudio(track,profile,controls,startFrame,playable,error))return false;

    if (timeline.set_track(track, trackNumber) != 0) {
        if (error) *error = "Could not connect an audio track.";
        return false;
    }
    Mlt::Transition mix(profile, "mix");
    if (!mix.is_valid()) {
        if (error) *error = "Could not mix audio tracks.";
        return false;
    }
    mix.set("start", 1.0);
    mix.set("sum", 1);
    mix.set("always_active", 1);
    timeline.plant_transition(mix, 0, trackNumber);
    return true;
}

} // namespace

void configureOutputProfile(Mlt::Profile &profile, int width, int height) {
    width = std::max(2, width);
    height = std::max(2, height);
    profile.set_explicit(1);
    profile.set_width(width);
    profile.set_height(height);
    profile.set_progressive(1);
    profile.set_sample_aspect(1, 1);
    const int divisor = std::gcd(width, height);
    profile.set_display_aspect(width / divisor, height / divisor);
    profile.set_frame_rate(Project::fps, 1);
    profile.set_colorspace(709);
}

QStringList recentAudioFiles(const QString &directory, int limit) {
    if (directory.isEmpty() || limit <= 0) return {};
    const auto files = QDir(directory).entryInfoList(
        {"*.wav", "*.m4a", "*.mp3", "*.aac", "*.flac", "*.aiff"},
        QDir::Files | QDir::Readable, QDir::Time);
    QStringList recent;
    for (const auto &file : files) {
        if (file.size() == 0) continue;
        recent.append(file.absoluteFilePath());
        if (recent.size() >= limit) break;
    }
    return recent;
}

std::optional<MediaInfo> mediaInfo(const QString &path, QString *error) {
    if (error) error->clear();
    const QFileInfo file(path);
    if (!file.isFile()) {
        if (error) *error = QString("File not found: %1").arg(path);
        return std::nullopt;
    }
    const QString key = file.canonicalFilePath();
    const qint64 bytes = file.size();
    const qint64 modified = file.lastModified().toMSecsSinceEpoch();
    {
        const std::lock_guard<std::mutex> lock(mediaInfoMutex);
        const auto found = mediaInfoCache.constFind(key);
        if (found != mediaInfoCache.cend() && found->bytes == bytes && found->modified == modified)
            return found->info;
    }
    initializeMlt();
    Mlt::Profile profile;
    configureOutputProfile(profile);
    auto source = openMedia(profile, path, error);
    if (!source) return std::nullopt;
    MediaInfo info;
    info.durationSeconds = source->get_length() / double(Project::fps);
    info.hasAudio = source->get_int("audio_index") >= 0;
    const int stream = source->get_int("video_index");
    if (stream >= 0) {
        const QByteArray prefix = QString("meta.media.%1.codec.").arg(stream).toLatin1();
        info.width = source->get_int((prefix + "width").constData());
        info.height = source->get_int((prefix + "height").constData());
        info.cameraRotationDegrees = source->get_int((prefix + "rotate").constData());
        if (info.width <= 0 || info.height <= 0) {
            if (error) *error = QString("Cannot read video dimensions: %1").arg(path);
            return std::nullopt;
        }
    }
    const QFileInfo after(path);
    if (after.isFile() && after.size() == bytes && after.lastModified().toMSecsSinceEpoch() == modified) {
        const std::lock_guard<std::mutex> lock(mediaInfoMutex);
        mediaInfoCache.insert(key, {bytes, modified, info});
    }
    return info;
}

std::optional<VideoSize> projectOutputSize(const Project &project, QString *error, bool allowOffline) {
    if (error) error->clear();
    if(project.canvasWidth>0&&project.canvasHeight>0)return VideoSize{project.canvasWidth,project.canvasHeight};
    std::optional<VideoSize> result;
    qint64 smallestArea = 0;
    for (const auto &clip : project.clips) {
        if(allowOffline && !QFileInfo(clip.path).isFile())continue;
        const auto info = mediaInfo(clip.path, error);
        if (!info) return std::nullopt;
        if (info->width <= 0 || info->height <= 0) {
            if (error) *error = QString("File has no video track: %1").arg(clip.path);
            return std::nullopt;
        }
        const qint64 area = qint64(info->width) * info->height;
        if (!result || area < smallestArea) {
            smallestArea = area;
            // Manual/camera rotation changes content orientation, not the landscape canvas.
            const int width = std::max(info->width, info->height);
            const int height = std::min(info->width, info->height);
            result = VideoSize{std::max(2, width - width % 2), std::max(2, height - height % 2)};
        }
    }
    if(!result && allowOffline && !project.clips.isEmpty())result=VideoSize{1920,1080};
    if (!result && error) *error = "Add at least one video clip.";
    return result;
}

std::unique_ptr<Mlt::Tractor> buildTimeline(const Project &project,
                                            Mlt::Profile &profile,
                                            QString *error, bool allowOffline) {
    if (error) error->clear();
    initializeMlt();
    const QString problem = project.validate();
    if (!problem.isEmpty()) {
        if (error) *error = problem;
        return {};
    }
    if (project.clips.isEmpty()) {
        if (error) *error = "Add at least one video clip.";
        return {};
    }

    Mlt::Playlist video(profile);
    auto timeline = std::make_unique<Mlt::Tractor>(profile);
    if (!video.is_valid()) {
        if (error) *error = "Could not create the video timeline.";
        return {};
    }
    for (const Clip &clip : project.clips) {
        const bool offline=allowOffline&&!QFileInfo(clip.path).isFile();
        auto source = offline?std::make_unique<Mlt::Producer>(profile,"color","#26272d"):openMedia(profile, clip.path, error);
        if (!source) return {};
        if(offline){source->set("length",qRound(clip.outSeconds*Project::fps)+1);source->set_in_and_out(0,qRound(clip.outSeconds*Project::fps));}
        if (!offline && source->get_int("video_index") < 0) {
            if (error) *error = QString("File has no video track: %1").arg(clip.path);
            return {};
        }
        if (clip.rotationDegrees != 0) {
            const QByteArray metadata = QString("meta.media.%1.codec.rotate")
                .arg(source->get_int("video_index")).toLatin1();
            // The decoder override replaces camera orientation, so add the user's turn.
            const int rotation = (source->get_int(metadata.constData()) % 360 + clip.rotationDegrees + 360) % 360;
            source->set("rotate", rotation);
        }
        const auto prefix=QString("meta.media.%1.codec.").arg(source->get_int("video_index")).toLatin1();
        QSizeF sourceSize(source->get_int((prefix+"width").constData()),source->get_int((prefix+"height").constData()));
        if(std::abs(source->get_int((prefix+"rotate").constData())+clip.rotationDegrees)%180==90)sourceSize.transpose();
        if(!VisualRendering::attachTransform(*source,*timeline,clip,sourceSize)){if(error)*error="Could not create transform.";return {};}
        const int in = qRound(clip.inSeconds * Project::fps);
        const int out = qRound(clip.outSeconds * Project::fps) - 1;
        if (in < 0 || out >= source->get_length() || out < in) {
            if (error) *error = QString("Trim exceeds clip duration: %1").arg(clip.path);
            return {};
        }
        if(!applyAudio(*source,profile,clip.audio,in,out-in+1,error))return {};
        if (video.append(*source, in, out) != 0) {
            if (error) *error = QString("Could not add clip: %1").arg(clip.path);
            return {};
        }
    }

    for (int boundary = project.clips.size() - 2; boundary >= 0; --boundary) {
        const int overlap = project.transitionFramesBefore(boundary + 1);
        if (overlap == 0) continue;
        Mlt::Transition dissolve(profile, "luma");
        Mlt::Transition audioFade(profile, "mix");
        if (!dissolve.is_valid() || !audioFade.is_valid()) {
            if (error) *error = "Crossfade effect is unavailable.";
            return {};
        }
        audioFade.set("start", -1.0);
        if (video.mix(boundary, overlap, &dissolve) != 0 ||
            video.mix_add(boundary + 1, &audioFade) != 0) {
            if (error) *error = "Could not create a crossfade.";
            return {};
        }
    }

    if (!timeline->is_valid() || timeline->set_track(video, 0) != 0) {
        if (error) *error = "Could not create the timeline.";
        return {};
    }
    const int total = project.durationFrames();
    int nextAudioTrack = 1;
    if (!project.voicePath.isEmpty() && !(allowOffline&&!QFileInfo(project.voicePath).isFile())) {
        const int start = qRound(project.voiceStartSeconds * Project::fps);
        if (start < total) {
            if (!addAudioTrack(*timeline, profile, project.voicePath,
                               start, total, nextAudioTrack, project.voiceAudio, false, error)) {
                return {};
            }
            ++nextAudioTrack;
        }
    }
    if (!project.musicPath.isEmpty() && !(allowOffline&&!QFileInfo(project.musicPath).isFile()) &&
        !addAudioTrack(*timeline, profile, project.musicPath, 0, total, nextAudioTrack,
                       project.audioControls(2), project.musicLoopToEnd, error)) {
        return {};
    }
    Mlt::Filter meter(profile,"audiolevel");
    if(meter.is_valid()){meter.set("iec_scale",0);timeline->attach(meter);}
    timeline->refresh();
    timeline->set_in_and_out(0, total - 1);
    if(!VisualRendering::attachOverlays(*timeline,profile,project,error,allowOffline))return {};
    if(!TextRendering::attach(*timeline,project.renderedTexts())) {
        if(error)*error="Could not create the text compositor.";
        return {};
    }
    return timeline;
}

bool exportMp4(const Project &project, const QString &path,
               std::function<void(int)> progress, QString *error, std::function<bool()> canceled) {
    if (error) error->clear();
    const auto wasCanceled=[&]{if(canceled&&canceled()){if(error)*error="Export canceled.";return true;}return false;};
    if(wasCanceled())return false;
    if (path.isEmpty()) {
        if (error) *error = "Choose an output file.";
        return false;
    }
    const QFileInfo target(path);
    const QString destination = target.absoluteFilePath();
    const QString canonicalDestination = target.canonicalFilePath();
    if (!canonicalDestination.isEmpty()) {
        for (const Clip &clip : project.clips) {
            if (QFileInfo(clip.path).canonicalFilePath() == canonicalDestination) {
                if (error) *error = "Output file cannot be one of the source clips.";
                return false;
            }
        }
        for(const auto &o:project.visualOverlays)if(QFileInfo(o.path).canonicalFilePath()==canonicalDestination){if(error)*error="Output file cannot be an overlay source.";return false;}
        if (QFileInfo(project.voicePath).canonicalFilePath() == canonicalDestination ||
            QFileInfo(project.musicPath).canonicalFilePath() == canonicalDestination) {
            if (error) *error = "Output file cannot be a source audio file.";
            return false;
        }
    }
    initializeMlt();
    const auto size = projectOutputSize(project, error);
    if (!size) return false;
    Mlt::Profile profile;
    configureOutputProfile(profile, size->width, size->height);
    QTemporaryFile temporary(target.absoluteDir().filePath(".isaac-XXXXXX.mp4"));
    if (!temporary.open()) {
        if (error) *error = QString("Cannot write output folder: %1").arg(temporary.errorString());
        return false;
    }
    const QString temporaryPath = temporary.fileName();
    temporary.close();
    const QByteArray encodedPath = QFile::encodeName(temporaryPath);
    if (progress) progress(0);
    const int total = project.durationFrames();
    const auto render = [&](bool hardware) {
        if(wasCanceled())return false;
        auto timeline = buildTimeline(project, profile, error);
        if (!timeline) return false;
        if(wasCanceled())return false;
        Mlt::Consumer consumer(profile, "avformat", encodedPath.constData());
        if (!consumer.is_valid()) {
            if (error) *error = "MP4 exporter is unavailable.";
            return false;
        }
        consumer.set("f", "mp4");
        consumer.set("vcodec", hardware ? "h264_videotoolbox" : "libx264");
        consumer.set("acodec", "aac");
        consumer.set("pix_fmt", "yuv420p");
        if (hardware) {
            // VideoToolbox uses bitrate control instead of libx264's CRF setting.
            const qint64 bitrate = std::clamp(qint64(size->width) * size->height * Project::fps * 75 / 1000,
                                              qint64(2000000), qint64(150000000));
            consumer.set("vb", bitrate);
            consumer.set("allow_sw", 0);
            consumer.set("color_range", "tv");
        } else {
            consumer.set("preset", "veryfast");
            consumer.set("crf", 20);
        }
        consumer.set("movflags", "+faststart");
        consumer.set("real_time", -1);
        if (consumer.connect(*timeline) != 0 || consumer.start() != 0) {
            if (error) *error = "Could not start MP4 export.";
            return false;
        }
        while (!consumer.is_stopped()) {
            if(wasCanceled()){consumer.stop();return false;}
            if (progress && total > 0) progress(std::clamp(consumer.position() * 100 / total, 0, 99));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        consumer.stop();
        if(wasCanceled())return false;
        // MLT can finish without video when an encoder fails but audio succeeds.
        auto check = openMedia(profile, temporaryPath, nullptr);
        const int videoIndex = check ? check->get_int("video_index") : -1;
        const QByteArray prefix = QString("meta.media.%1.codec.").arg(videoIndex).toLatin1();
        bool completeVideo = !hardware;
        if (hardware) {
            QString inspector = QStandardPaths::findExecutable("ffprobe");
            if (inspector.isEmpty()) inspector = QStandardPaths::findExecutable("ffprobe", {"/opt/homebrew/bin", "/usr/local/bin"});
            if (!inspector.isEmpty()) {
                QProcess inspect;
                // MP4's sample count checks completeness without decoding the exported video.
                inspect.start(inspector, {"-v", "error", "-select_streams", "v:0", "-show_entries",
                    "stream=nb_frames", "-of", "csv=p=0", temporaryPath});
                if (inspect.waitForFinished(5000) && inspect.exitStatus() == QProcess::NormalExit && inspect.exitCode() == 0) {
                    bool valid = false;
                    completeVideo = inspect.readAllStandardOutput().trimmed().toLongLong(&valid) == total && valid;
                } else if (inspect.state() != QProcess::NotRunning) {
                    inspect.kill();
                    inspect.waitForFinished(1000);
                }
            }
        }
        if (consumer.get_int("error") || !check || videoIndex < 0 || check->get_length() < total ||
            !completeVideo ||
            check->get_int((prefix + "width").constData()) != size->width ||
            check->get_int((prefix + "height").constData()) != size->height) {
            if (error) *error = "MP4 export failed. Check the source files and free disk space.";
            return false;
        }
        return true;
    };
    bool rendered = false;
#ifdef Q_OS_MACOS
    QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) ffmpeg = QStandardPaths::findExecutable("ffmpeg", {"/opt/homebrew/bin", "/usr/local/bin"});
    if (!ffmpeg.isEmpty()) {
        QProcess probe;
        probe.start(ffmpeg, {"-nostdin", "-hide_banner", "-v", "error", "-f", "lavfi", "-i",
            QString("color=s=%1x%2:r=60").arg(size->width).arg(size->height), "-frames:v", "1", "-an",
            "-c:v", "h264_videotoolbox", "-allow_sw", "0", "-f", "null", "-"});
        if (probe.waitForFinished(5000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0)
            rendered = render(true);
        else if (probe.state() != QProcess::NotRunning) {
            probe.kill();
            probe.waitForFinished(1000);
        }
    }
#endif
    if(wasCanceled())return false;
    if (!rendered) {
        if (error) error->clear();
        if (!render(false)) return false;
    }
    if(wasCanceled())return false;
    const QByteArray encodedDestination = QFile::encodeName(destination);
    if (std::rename(encodedPath.constData(), encodedDestination.constData()) != 0) {
        if (error) *error = QString("Could not save MP4: %1")
                                 .arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    if (progress) progress(100);
    return true;
}

double mediaDurationSeconds(const QString &path, QString *error) {
    const auto info = mediaInfo(path, error);
    return info ? info->durationSeconds : 0;
}

} // namespace Media
