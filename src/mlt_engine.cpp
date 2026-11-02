#include "mlt_engine.h"
#include "text_renderer.h"
#include "visual_renderer.h"
#include "runtime_paths.h"
#include "encoding.h"
#include "performance.h"
#include "transition_presets.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QSize>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QtMath>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <thread>

namespace Media {

namespace { constexpr auto sourceFilesKey="isaac.source.files"; }
void setSourceFiles(Mlt::Producer &producer,const QStringList &paths){
    auto *files=new QStringList(paths);files->removeAll(QString{});files->removeDuplicates();
    mlt_properties_set_data(producer.get_properties(),sourceFilesKey,files,0,
        [](void *p){delete static_cast<QStringList*>(p);},nullptr);
}
QStringList sourceFiles(Mlt::Producer &producer){
    if(const auto *files=static_cast<QStringList*>(producer.get_data(sourceFilesKey)))return *files;
    const auto resource=QString::fromUtf8(producer.get("resource"));const QFileInfo file(resource);
    return !resource.isEmpty()&&(file.isFile()||file.isAbsolute())?QStringList{resource}:QStringList{};
}

void initializeMlt() {
    static std::once_flag once;
    std::call_once(once, [] {
        const auto runtime = bundledRuntimePaths(QCoreApplication::applicationDirPath());
        if (!runtime.mltRepository.isEmpty()) {
            qputenv("MLT_DATA", QFile::encodeName(runtime.mltData));
            const auto path = QFile::encodeName(runtime.executables) +
                QDir::listSeparator().toLatin1() + qgetenv("PATH");
            qputenv("PATH", path);
            // Relocatable MLT builds require the repository as an explicit argument.
            const auto repository = QFile::encodeName(runtime.mltRepository);
            Mlt::Factory::init(repository.constData());
        } else {
            Mlt::Factory::init();
        }
    });
}

namespace {

struct CachedMediaInfo {
    qint64 bytes = 0;
    qint64 modified = 0;
    MediaInfo info;
};

std::mutex mediaInfoMutex;
QHash<QString, CachedMediaInfo> mediaInfoCache;

struct PreparedSource {
    Clip clip;
    QString canonicalPath;
    qint64 bytes = 0, modified = 0;
    bool offline = false;
    std::shared_ptr<Mlt::Producer> producer;
};
struct PlaybackSources {
    mlt_profile owner;
    mlt_profile_s profile;
    QHash<QString, PreparedSource> clips;
};
constexpr auto playbackSourcesKey = "isaac.playback.sources";

bool sameProfile(const mlt_profile_s &a, const mlt_profile_s &b) {
    return a.width == b.width && a.height == b.height && a.frame_rate_num == b.frame_rate_num &&
        a.frame_rate_den == b.frame_rate_den && a.progressive == b.progressive &&
        a.sample_aspect_num == b.sample_aspect_num && a.sample_aspect_den == b.sample_aspect_den &&
        a.display_aspect_num == b.display_aspect_num && a.display_aspect_den == b.display_aspect_den &&
        a.colorspace == b.colorspace;
}

bool sameSourceSettings(const Clip &a, const Clip &b) {
    return a.path == b.path && a.inSeconds == b.inSeconds && a.outSeconds == b.outSeconds &&
        a.rotationDegrees == b.rotationDegrees && a.audio == b.audio && a.sourceFrameOffset == b.sourceFrameOffset;
}

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
            // Follow camera orientation; manual clip turns do not turn the canvas.
            const bool quarterTurn = qAbs(info->cameraRotationDegrees % 180) == 90;
            const int width = quarterTurn ? info->height : info->width;
            const int height = quarterTurn ? info->width : info->height;
            result = VideoSize{std::max(2, width - width % 2), std::max(2, height - height % 2)};
        }
    }
    if(!result && allowOffline && !project.clips.isEmpty())result=VideoSize{1920,1080};
    if (!result && error) *error = "Add at least one video clip.";
    return result;
}

std::optional<VideoSize> mp4OutputSize(const Project &project, bool optimizeForIphone, QString *error) {
    auto result=projectOutputSize(project,error);
    if(!result || !optimizeForIphone)return result;
    QSize size(result->width,result->height);
    const QSize bounds=size.width()>=size.height()?QSize(3840,2160):QSize(2160,3840);
    if(size.width()>bounds.width() || size.height()>bounds.height())
        size.scale(bounds,Qt::KeepAspectRatio);
    return VideoSize{std::max(2,size.width()/2*2),std::max(2,size.height()/2*2)};
}

std::unique_ptr<Mlt::Tractor> buildTimeline(const Project &project,
                                            Mlt::Profile &profile,
                                            QString *error, bool allowOffline, Mlt::Tractor *previousTimeline) {
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
    auto *previous = previousTimeline
        ? static_cast<PlaybackSources *>(previousTimeline->get_data(playbackSourcesKey)) : nullptr;
    if (previous && (previous->owner != profile.get_profile() || !sameProfile(previous->profile, *profile.get_profile()))) previous = nullptr;
    auto *prepared = new PlaybackSources{profile.get_profile(), *profile.get_profile(), {}};
    mlt_properties_set_data(timeline->get_properties(), playbackSourcesKey, prepared, 0,
        [](void *p) { delete static_cast<PlaybackSources *>(p); }, nullptr);
    for (const Clip &clip : project.clips) {
        const QFileInfo file(clip.path);
        const bool offline=allowOffline&&!file.isFile();
        std::shared_ptr<Mlt::Producer> source;
        if (previous) {
            const auto found = previous->clips.constFind(clip.id);
            if (found != previous->clips.cend() && sameSourceSettings(found->clip, clip) &&
                found->offline == offline && (offline || file.isFile()) &&
                found->canonicalPath == file.canonicalFilePath() &&
                found->bytes == file.size() && found->modified == file.lastModified().toMSecsSinceEpoch()) source = found->producer;
        }
        const bool reused = bool(source);
        if (!source) source = offline ? std::make_shared<Mlt::Producer>(profile,"color","#26272d") : openMedia(profile, clip.path, error);
        if (!source) return {};
        if(offline){source->set("length",qRound(clip.outSeconds*Project::fps)+1);source->set_in_and_out(0,qRound(clip.outSeconds*Project::fps));}
        if (!offline && source->get_int("video_index") < 0) {
            if (error) *error = QString("File has no video track: %1").arg(clip.path);
            return {};
        }
        if (!reused && clip.rotationDegrees != 0) {
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
        if(!reused && !applyAudio(*source,profile,clip.audio,in,out-in+1,error))return {};
        if (video.append(*source, in, out) != 0) {
            if (error) *error = QString("Could not add clip: %1").arg(clip.path);
            return {};
        }
        prepared->clips.insert(clip.id, {clip, file.canonicalFilePath(), file.size(), file.lastModified().toMSecsSinceEpoch(), offline, source});
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
        if (!Transitions::configure(dissolve, project.clips[boundary + 1].transitionType,
                                   {profile.width(), profile.height()}, error)) return {};
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
    QStringList files{project.voicePath,project.musicPath};
    for(const auto &clip:project.clips)files.append(clip.path);
    for(const auto &overlay:project.visualOverlays)files.append(overlay.path);
    setSourceFiles(*timeline,files);
    return timeline;
}

namespace {
struct ExportResources {
    int workers=1, buffer=1;
    quint64 queueBytes=0;
};
ExportResources exportResources(const Project &project, const VideoSize &size,
                                const Performance::Resources &resources) {
    bool composition=!project.renderedTexts().isEmpty();
    for(const auto &clip:project.clips)
        composition|=clip.transform!=VisualTransform{} || clip.rotationDegrees!=0;
    for(int i=0;i<project.clips.size();++i)
        composition|=project.transitionFramesBefore(i)>0;
    for(const auto &overlay:project.visualOverlays)
        composition|=overlay.lane>=0 && overlay.lane<project.visualLaneVisible.size() && project.visualLaneVisible[overlay.lane];
    // Reserve working RGBA/YUV copies as well as queued frames; codecs have their own allocations.
    const quint64 frameBytes=std::max(quint64(1),quint64(size.width)*quint64(size.height)*16);
    const auto budget=Performance::budgetFor(resources);
    const quint64 queueBudget=budget.exportQueueBytes;
    const int capacity=int(std::clamp(queueBudget/frameBytes,quint64(1),quint64(32)));
    ExportResources result;
    const int requested=composition?budget.renderThreads:1;
    result.workers=std::clamp(requested,1,std::min(8,std::max(1,capacity/2)));
    result.buffer=std::min(capacity,result.workers*2);
    result.queueBytes=quint64(result.buffer)*frameBytes;
    return result;
}
void configureMp4Consumer(Mlt::Consumer &consumer, const QString &encoder, const VideoSize &size, bool optimizeForIphone) {
    consumer.set("f","mp4");consumer.set("vcodec",encoder.toLatin1().constData());
    consumer.set("acodec","aac");consumer.set("pix_fmt","yuv420p");consumer.set("movflags","+faststart");
    qint64 maximumBitrate=150000000;
    if(optimizeForIphone) {
        const qint64 macroblocks=qint64((size.width+15)/16)*((size.height+15)/16);
        const bool hd=macroblocks<=8704 && macroblocks*Project::fps<=522240;
        consumer.set("vprofile","high");consumer.set("vlevel",hd?"4.2":"5.2");
        // Bound VBR peaks as well as the average: unconstrained NVENC can exceed device limits.
        maximumBitrate=hd?16000000:40000000;
        consumer.set("vmaxrate",static_cast<std::int64_t>(maximumBitrate));
        consumer.set("vbufsize",static_cast<std::int64_t>(maximumBitrate*2));
    }
    if(isHardwareH264Encoder(encoder)) {
        const qint64 bitrate=std::clamp(qint64(size.width)*size.height*Project::fps*75/1000,qint64(2000000),maximumBitrate);
        consumer.set("vb",static_cast<std::int64_t>(bitrate));consumer.set("color_range","tv");
        if(encoder==QLatin1String("h264_videotoolbox"))consumer.set("allow_sw",0);
        else {consumer.set("preset","p4");consumer.set("rc","vbr");consumer.set("cq",20);}
    } else {consumer.set("preset","veryfast");consumer.set("crf",20);}
}
bool completeMp4(Mlt::Profile &profile, const QString &path, int frames,
                  const VideoSize &size, bool hardware, bool optimizeForIphone) {
    auto check=openMedia(profile,path,nullptr);
    const int videoIndex=check?check->get_int("video_index"):-1;
    const QByteArray prefix=QString("meta.media.%1.codec.").arg(videoIndex).toLatin1();
    if(!check || videoIndex<0 || check->get_length()<frames ||
       check->get_int((prefix+"width").constData())!=size.width ||
       check->get_int((prefix+"height").constData())!=size.height)return false;
    if(!hardware)return true;
    QString inspector=QStandardPaths::findExecutable("ffprobe");
    if(inspector.isEmpty())inspector=QStandardPaths::findExecutable("ffprobe",{"/opt/homebrew/bin","/usr/local/bin"});
    if(inspector.isEmpty())return false;
    QProcess inspect;
    inspect.start(inspector,{"-v","error","-select_streams","v:0","-show_entries","stream=nb_frames,level,pix_fmt","-of","json",path});
    if(!inspect.waitForFinished(5000)) {
        inspect.kill();inspect.waitForFinished(1000);return false;
    }
    const auto streams=QJsonDocument::fromJson(inspect.readAllStandardOutput()).object()["streams"].toArray();
    if(streams.size()!=1)return false;
    const auto video=streams.first().toObject();bool valid=false;
    const auto count=video["nb_frames"].toString().toLongLong(&valid);
    return inspect.exitStatus()==QProcess::NormalExit && inspect.exitCode()==0 && valid && count==frames &&
        (!optimizeForIphone || (video["level"].toInt()>0 && video["level"].toInt()<=52)) &&
        video["pix_fmt"].toString()==QLatin1String("yuv420p");
}
}

bool exportMp4(const Project &project, const QString &path,
               std::function<void(int)> progress, QString *error, std::function<bool()> canceled,
                std::function<void(const QString &)> diagnostic, bool softwareOnly, bool optimizeForIphone) {
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
        for(const auto &asset:project.assets)if(QFileInfo(asset.path).canonicalFilePath()==canonicalDestination){if(error)*error="Output file cannot be a source media asset.";return false;}
        for(const auto &o:project.visualOverlays)if(QFileInfo(o.path).canonicalFilePath()==canonicalDestination){if(error)*error="Output file cannot be an overlay source.";return false;}
        if (QFileInfo(project.voicePath).canonicalFilePath() == canonicalDestination ||
            QFileInfo(project.musicPath).canonicalFilePath() == canonicalDestination) {
            if (error) *error = "Output file cannot be a source audio file.";
            return false;
        }
    }
    initializeMlt();
    const auto size = mp4OutputSize(project, optimizeForIphone, error);
    if (!size) return false;
    Mlt::Profile profile;
    configureOutputProfile(profile, size->width, size->height);
    QString temporaryPath;
    {
        QTemporaryFile temporary(target.absoluteDir().filePath(".isaac-XXXXXX.mp4"));
        if (!temporary.open()) {
            if (error) *error = QString("Cannot write output folder: %1").arg(temporary.errorString());
            return false;
        }
        temporaryPath = temporary.fileName();
        // close() retains a native handle that prevents renaming on Windows.
        temporary.setAutoRemove(false);
    }
    const auto removeTemporary = qScopeGuard([&] { QFile::remove(temporaryPath); });
    const QByteArray encodedPath = QFile::encodeName(temporaryPath);
    if (progress) progress(0);
    const int total = project.durationFrames();
    const auto resources=Performance::resources();
    const auto settings=exportResources(project,*size,resources);
    if(diagnostic)diagnostic(QString("Resources: cpus=%1 availableMiB=%2 totalMiB=%3 real_time=%4 buffer=%5 prefill=1 queueMiB=%6 codecThreads=auto")
        .arg(resources.logicalCpus).arg(resources.availableMemoryBytes/1024/1024).arg(resources.totalMemoryBytes/1024/1024)
        .arg(-settings.workers).arg(settings.buffer).arg(settings.queueBytes/1024/1024));
    const auto render = [&](const QString &encoder) {
        if(wasCanceled())return false;
        if(diagnostic)diagnostic("Encoder: "+encoder);
        auto timeline=buildTimeline(project,profile,error);
        if(!timeline || wasCanceled())return false;
        Mlt::Consumer consumer(profile,"avformat",encodedPath.constData());
        if(!consumer.is_valid()){if(error)*error="MP4 exporter is unavailable.";return false;}
        configureMp4Consumer(consumer,encoder,*size,optimizeForIphone);
        if(diagnostic)diagnostic(QString("Output: %1x%2 at %3 fps; mode=%4").arg(size->width).arg(size->height).arg(Project::fps)
            .arg(optimizeForIphone?"iPhone":"native"));
        consumer.set("real_time",-settings.workers);consumer.set("buffer",settings.buffer);consumer.set("prefill",1);
        if(consumer.connect(*timeline)!=0 || consumer.start()!=0) {
            if(error)*error="Could not start MP4 export.";return false;
        }
        const auto started=std::chrono::steady_clock::now();auto lastDiagnostic=started-std::chrono::seconds(1);
        while(!consumer.is_stopped()) {
            if(wasCanceled()){consumer.stop();return false;}
            const auto now=std::chrono::steady_clock::now();
            if(diagnostic && now-lastDiagnostic>=std::chrono::seconds(1)) {
                diagnostic(QString("Frame %1/%2").arg(consumer.position()).arg(total));lastDiagnostic=now;
            }
            if(progress && total>0)progress(std::clamp(consumer.position()*100/total,0,99));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        consumer.stop();if(wasCanceled())return false;
        const auto compositionError=VisualRendering::renderError(*timeline);
        if(!compositionError.isEmpty()){if(error)*error=compositionError;return false;}
        if(consumer.get_int("error") || !completeMp4(profile,temporaryPath,total,*size,isHardwareH264Encoder(encoder),optimizeForIphone)) {
            if(error)*error="MP4 export failed. Check the source files and free disk space.";return false;
        }
        if(diagnostic) {
            const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            diagnostic(QString("Encoded %1 frames in %2 s (%3 fps)").arg(total).arg(seconds,0,'f',2).arg(total/std::max(.001,seconds),0,'f',1));
        }
        return true;
    };
    const auto probeHardware=[&](const QString &encoder) {
        if(wasCanceled())return false;
        QString probePath;
        {
            QTemporaryFile probeFile(target.absoluteDir().filePath(".isaac-probe-XXXXXX.mp4"));
            if(!probeFile.open())return false;
            probePath=probeFile.fileName();probeFile.setAutoRemove(false);
        }
        const auto removeProbe=qScopeGuard([&]{QFile::remove(probePath);});
        if(diagnostic)diagnostic("Hardware probe: "+encoder);
        Mlt::Producer color(profile,"color","black");
        constexpr int probeFrames=4;color.set("length",probeFrames);color.set_in_and_out(0,probeFrames-1);
        const auto probeEncoded=QFile::encodeName(probePath);
        Mlt::Consumer consumer(profile,"avformat",probeEncoded.constData());
        if(!color.is_valid() || !consumer.is_valid())return false;
        configureMp4Consumer(consumer,encoder,*size,optimizeForIphone);
        consumer.set("an",1);consumer.set("audio_off",1);consumer.set("real_time",-1);consumer.set("buffer",1);consumer.set("prefill",1);
        if(consumer.connect(color)!=0 || consumer.start()!=0)return false;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!consumer.is_stopped() && std::chrono::steady_clock::now()<deadline) {
            if(wasCanceled()){consumer.stop();return false;}
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        const bool completed=consumer.is_stopped();consumer.stop();
        return completed && !wasCanceled() && !consumer.get_int("error") && completeMp4(profile,probePath,probeFrames,*size,true,optimizeForIphone);
    };
    bool rendered=false;
    const auto hardware=hardwareH264Encoder();
    // Driver faults must stay inside the worker so the editor can retry with software.
    const bool isolatedWorker=QCoreApplication::arguments().value(1)==QLatin1String("--export-worker");
    if(!softwareOnly && isolatedWorker && !hardware.isEmpty()) {
        if(probeHardware(hardware)) {
            if(diagnostic)diagnostic("Hardware probe succeeded: "+hardware);
            rendered=render(hardware);
            if(!rendered && !wasCanceled() && diagnostic)diagnostic("Hardware export failed: "+(error?*error:QString{})+" Retrying with software.");
        } else if(!wasCanceled() && diagnostic)diagnostic("Hardware encoder unavailable: "+hardware+" failed the MLT encode probe. Retrying with software.");
    }
    if(wasCanceled())return false;
    if(!rendered) {
        if(error)error->clear();
        if(!render(QStringLiteral("libx264")))return false;
    }
    if(wasCanceled())return false;
    std::error_code saveError;
    std::filesystem::rename(std::filesystem::path(temporaryPath.toStdU16String()),
                            std::filesystem::path(destination.toStdU16String()), saveError);
    if (saveError) {
        if (error) *error = QString("Could not save MP4: %1")
                                 .arg(QString::fromLocal8Bit(saveError.message().c_str()));
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
