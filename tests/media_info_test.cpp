#include "mlt_engine.h"
#include "visual_renderer.h"
#include "performance.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class MediaInfoTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void readsNativeDimensionsAndAudioDuration();
    void choosesLowestNativeResolution();
    void keepsTheLowestSourceOrientation();
    void honorsCameraOrientation_data();
    void honorsCameraOrientation();
    void roundsOddDimensionsForH264();
    void invalidatesChangedAndMissingSources();
    void failedProbeIsRememberedForTheSameSourceIdentity();
    void rejectsEmptyAndNonVideoProjects();
    void exportUsesLowestResolution();
    void exportKeepsH264WithinPhoneCompatibleBounds_data();
    void exportKeepsH264WithinPhoneCompatibleBounds();
    void nativeExportRetainsOversizedCanvas();
    void exportResourcesAreBoundedAndKeepEveryFrame_data();
    void exportResourcesAreBoundedAndKeepEveryFrame();
    void findsRecentAudioFiles();
    void directExportKeepsHardwareOutsideTheEditorProcess();
    void failedExportPreservesDestination();
    void unusedSourceAssetCannotBeExportDestination();
    void corruptStillOverlayCannotPublishIncompleteComposition();
    void successfulExportReplacesDestination();
    void playbackBuildReusesUnchangedSources();
    void playbackReuseInvalidatesChangedSettings();
private:
    bool makeMedia(const QString &path, const QString &source, const QStringList &encoding);
    QString file(const QString &name) const { return directory_.filePath(name); }
    QTemporaryDir directory_;
    QString ffmpeg_;
};

bool MediaInfoTest::makeMedia(const QString &path, const QString &source, const QStringList &encoding) {
    QProcess process;
    QStringList arguments{"-v", "error", "-f", "lavfi", "-i", source};
    arguments += encoding;
    arguments << "-y" << path;
    process.start(ffmpeg_, arguments);
    if (!process.waitForFinished(30000)) return false;
    if (process.exitCode() != 0) qWarning().noquote() << process.readAllStandardError();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void MediaInfoTest::initTestCase() {
    QVERIFY(directory_.isValid());
    ffmpeg_ = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg_.isEmpty()) ffmpeg_ = QStandardPaths::findExecutable("ffmpeg", {"/opt/homebrew/bin", "/usr/local/bin"});
    if (ffmpeg_.isEmpty()) QSKIP("FFmpeg is needed to generate native-resolution test media.");
    const QStringList encoding{"-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p", "-an"};
    for (const auto &entry : {qMakePair("4k.mp4", "3840x2160"), qMakePair("2k.mp4", "2560x1440"),
                              qMakePair("2.7k.mp4", "2704x1520"), qMakePair("hd.mp4", "1920x1080"),
                              qMakePair("portrait.mp4", "720x1280")}) {
        QVERIFY(makeMedia(file(entry.first), QString("color=c=red:s=%1:r=60:d=0.1").arg(entry.second), encoding));
    }
    QVERIFY(makeMedia(file("odd.mkv"), "testsrc=s=321x181:r=60:d=0.1",
                      {"-c:v", "ffv1", "-pix_fmt", "bgr0", "-an"}));
    QVERIFY(makeMedia(file("tone.wav"), "sine=frequency=440:sample_rate=48000:duration=0.1",
                      {"-c:a", "pcm_s16le"}));
}

void MediaInfoTest::readsNativeDimensionsAndAudioDuration() {
    QString error;
    const auto info = Media::mediaInfo(file("2k.mp4"), &error);
    QVERIFY2(info, qPrintable(error));
    QCOMPARE(info->width, 2560);
    QCOMPARE(info->height, 1440);
    QCOMPARE(info->durationSeconds, 0.1);
    QCOMPARE(Media::mediaDurationSeconds(file("2k.mp4")), info->durationSeconds);
    const auto audio = Media::mediaInfo(file("tone.wav"), &error);
    QVERIFY2(audio, qPrintable(error));
    QCOMPARE(audio->width, 0);
    QCOMPARE(audio->height, 0);
    QVERIFY(audio->durationSeconds > 0);
}

void MediaInfoTest::choosesLowestNativeResolution() {
    Project project;
    project.clips = {{file("4k.mp4"), 0, 0.1}, {file("4k.mp4"), 0, 0.1}};
    auto size = Media::projectOutputSize(project);
    QVERIFY(size);
    QCOMPARE(size->width, 3840);
    QCOMPARE(size->height, 2160);
    project.clips.append({file("2.7k.mp4"), 0, 0.1});
    size = Media::projectOutputSize(project);
    QVERIFY(size);
    QCOMPARE(size->width, 2704);
    QCOMPARE(size->height, 1520);
    project.clips.append({file("2k.mp4"), 0, 0.1});
    project.clips.last().rotationDegrees = 90;
    size = Media::projectOutputSize(project);
    QVERIFY(size);
    QCOMPARE(size->width, 2560);
    QCOMPARE(size->height, 1440);
    project.clips.append({file("hd.mp4"), 0, 0.1});
    size = Media::projectOutputSize(project);
    QVERIFY(size);
    QCOMPARE(size->width, 1920);
    QCOMPARE(size->height, 1080);
}

void MediaInfoTest::roundsOddDimensionsForH264() {
    const auto info = Media::mediaInfo(file("odd.mkv"));
    QVERIFY(info);
    QCOMPARE(info->width, 321);
    QCOMPARE(info->height, 181);
    Project project;
    project.clips = {{file("odd.mkv"), 0, 0.1}};
    const auto size = Media::projectOutputSize(project);
    QVERIFY(size);
    QCOMPARE(size->width, 320);
    QCOMPARE(size->height, 180);
    Mlt::Profile profile;
    Media::configureOutputProfile(profile, size->width, size->height);
    QCOMPARE(profile.width(), 320);
    QCOMPARE(profile.height(), 180);
    QCOMPARE(profile.dar(), 16.0 / 9.0);
}

void MediaInfoTest::keepsTheLowestSourceOrientation() {
    Project project;
    project.clips = {{file("4k.mp4"), 0, 0.1}, {file("portrait.mp4"), 0, 0.1}};
    const auto size = Media::projectOutputSize(project);
    QVERIFY(size);
    QCOMPARE(size->width, 720);
    QCOMPARE(size->height, 1280);
}

void MediaInfoTest::honorsCameraOrientation_data() {
    QTest::addColumn<int>("rotation");
    QTest::addColumn<QSize>("expected");
    QTest::newRow("landscape") << 0 << QSize(3840, 2160);
    QTest::newRow("portrait-clockwise") << 90 << QSize(2160, 3840);
    QTest::newRow("upside-down") << 180 << QSize(3840, 2160);
    QTest::newRow("portrait-counterclockwise") << 270 << QSize(2160, 3840);
    QTest::newRow("negative-quarter-turn") << -90 << QSize(2160, 3840);
}

void MediaInfoTest::honorsCameraOrientation() {
    QFETCH(int, rotation);
    QFETCH(QSize, expected);
    const auto path = file(QString("rotated-%1.mp4").arg(rotation));
    QProcess remux;
    remux.start(ffmpeg_, {"-v", "error", "-display_rotation", QString::number(rotation),
                         "-i", file("4k.mp4"), "-c", "copy", path});
    QVERIFY(remux.waitForFinished(30000));
    QCOMPARE(remux.exitCode(), 0);
    Project project;
    project.clips = {{path, 0, 0.1}};
    auto size = Media::projectOutputSize(project);
    QVERIFY(size);
    QCOMPARE(QSize(size->width, size->height), expected);
    // Editing a clip's rotation should not turn the project's canvas.
    project.clips[0].rotationDegrees = 90;
    size = Media::projectOutputSize(project);
    QVERIFY(size);
    QCOMPARE(QSize(size->width, size->height), expected);
    project.clips.append({file("hd.mp4"), 0, 0.1});
    size = Media::projectOutputSize(project);
    QVERIFY(size);
    QCOMPARE(QSize(size->width, size->height), QSize(1920, 1080));
    project.canvasWidth = 1080;
    project.canvasHeight = 1920;
    size = Media::projectOutputSize(project);
    QVERIFY(size);
    QCOMPARE(QSize(size->width, size->height), QSize(1080, 1920));
}

void MediaInfoTest::invalidatesChangedAndMissingSources() {
    const QString changing = file("changing.mp4");
    QVERIFY(QFile::copy(file("2k.mp4"), changing));
    auto info = Media::mediaInfo(changing);
    QVERIFY(info);
    QCOMPARE(info->width, 2560);
    QVERIFY(QFile::remove(changing));
    QVERIFY(QFile::copy(file("4k.mp4"), changing));
    info = Media::mediaInfo(changing);
    QVERIFY(info);
    QCOMPARE(info->width, 3840);
    QVERIFY(QFile::remove(changing));
    QString error;
    QVERIFY(!Media::mediaInfo(changing, &error));
    QVERIFY(!error.isEmpty());
}

void MediaInfoTest::failedProbeIsRememberedForTheSameSourceIdentity() {
    QFile valid(file("hd.mp4"));QVERIFY(valid.open(QIODevice::ReadOnly));const auto bytes=valid.readAll();
    const auto path=file("retry-metadata.mp4");const auto modified=QDateTime::fromMSecsSinceEpoch(1700000000000LL);
    QFile source(path);QVERIFY(source.open(QIODevice::WriteOnly));QCOMPARE(source.write(QByteArray(bytes.size(),'\0')),bytes.size());
    QVERIFY(source.flush());QVERIFY(source.setFileTime(modified,QFileDevice::FileModificationTime));source.close();QString firstError;
    QVERIFY(!Media::mediaInfo(path,&firstError));QVERIFY(!firstError.isEmpty());
    QVERIFY(source.open(QIODevice::WriteOnly));QCOMPARE(source.write(bytes),bytes.size());
    QVERIFY(source.flush());QVERIFY(source.setFileTime(modified,QFileDevice::FileModificationTime));source.close();QString repeatedError;
    QVERIFY(!Media::mediaInfo(path,&repeatedError));QCOMPARE(repeatedError,firstError);
    const auto repaired=Media::retryMediaInfo(path,&repeatedError);QVERIFY(repaired);QVERIFY(repeatedError.isEmpty());QCOMPARE(repaired->width,1920);
}

void MediaInfoTest::rejectsEmptyAndNonVideoProjects() {
    QString error;
    QVERIFY(!Media::projectOutputSize({}, &error));
    QVERIFY(!error.isEmpty());
    Project project;
    project.clips = {{file("tone.wav"), 0, 0.1}};
    QVERIFY(!Media::projectOutputSize(project, &error));
    QVERIFY(!error.isEmpty());
}

void MediaInfoTest::exportUsesLowestResolution() {
    Project project;
    project.clips = {{file("4k.mp4"), 0, 0.1}, {file("2k.mp4"), 0, 0.1}};
    const QString output = file("mixed.mp4");
    QString error;
    QVERIFY2(Media::exportMp4(project, output, {}, &error), qPrintable(error));
    QString ffprobe = QStandardPaths::findExecutable("ffprobe");
    if (ffprobe.isEmpty()) ffprobe = QStandardPaths::findExecutable("ffprobe", {"/opt/homebrew/bin", "/usr/local/bin"});
    QVERIFY(!ffprobe.isEmpty());
    QProcess process;
    process.start(ffprobe, {"-v", "error", "-select_streams", "v:0", "-show_entries",
                           "stream=width,height,r_frame_rate,nb_frames", "-of", "json", output});
    QVERIFY(process.waitForFinished(30000));
    QCOMPARE(process.exitCode(), 0);
    const auto streams = QJsonDocument::fromJson(process.readAllStandardOutput()).object().value("streams").toArray();
    QCOMPARE(streams.size(), 1);
    const auto stream = streams.first().toObject();
    QCOMPARE(stream.value("width").toInt(), 2560);
    QCOMPARE(stream.value("height").toInt(), 1440);
    QCOMPARE(stream.value("r_frame_rate").toString(), QString("60/1"));
    QCOMPARE(stream.value("nb_frames").toString(), QString("12"));
}

void MediaInfoTest::exportKeepsH264WithinPhoneCompatibleBounds_data() {
    QTest::addColumn<QSize>("canvas");
    QTest::addColumn<QSize>("expected");
    QTest::addColumn<int>("level");
    QTest::newRow("reported-portrait") << QSize(2880,3840) << QSize(2160,2880) << 52;
    QTest::newRow("large-landscape") << QSize(4800,2700) << QSize(3840,2160) << 52;
    QTest::newRow("standard-portrait-4k") << QSize(2160,3840) << QSize(2160,3840) << 52;
    QTest::newRow("square") << QSize(4320,4320) << QSize(2160,2160) << 52;
    QTest::newRow("small-unchanged") << QSize(320,180) << QSize(320,180) << 42;
}

void MediaInfoTest::nativeExportRetainsOversizedCanvas() {
    Project project;project.clips={{file("hd.mp4"),0,.1}};
    project.canvasWidth=2880;project.canvasHeight=3840;
    const auto output=file("native.mp4");QString error;
    QVERIFY2(Media::exportMp4(project,output,{},&error,{}, {},true),qPrintable(error));
    const auto info=Media::mediaInfo(output);QVERIFY(info);
    QCOMPARE(info->width,2880);QCOMPARE(info->height,3840);
}

void MediaInfoTest::exportKeepsH264WithinPhoneCompatibleBounds() {
    QFETCH(QSize,canvas);QFETCH(QSize,expected);QFETCH(int,level);
    Project project;project.clips={{file("hd.mp4"),0,.1}};
    project.canvasWidth=canvas.width();project.canvasHeight=canvas.height();
    const auto output=file("phone-compatible.mp4");QString error;
    QVERIFY2(Media::exportMp4(project,output,{},&error,{}, {},true,true),qPrintable(error));
    const auto canvasAfter=Media::projectOutputSize(project);
    QVERIFY(canvasAfter);QCOMPARE(QSize(canvasAfter->width,canvasAfter->height),canvas);
    auto inspector=QStandardPaths::findExecutable("ffprobe");
    if(inspector.isEmpty())inspector=QStandardPaths::findExecutable("ffprobe",{"/opt/homebrew/bin","/usr/local/bin"});
    QVERIFY(!inspector.isEmpty());QProcess inspect;
    inspect.start(inspector,{"-v","error","-select_streams","v:0","-show_entries",
        "stream=width,height,r_frame_rate,nb_frames,profile,level,pix_fmt","-of","json",output});
    QVERIFY(inspect.waitForFinished(15000));QCOMPARE(inspect.exitCode(),0);
    const auto streams=QJsonDocument::fromJson(inspect.readAllStandardOutput()).object()["streams"].toArray();
    QCOMPARE(streams.size(),1);const auto stream=streams.first().toObject();
    QCOMPARE(QSize(stream["width"].toInt(),stream["height"].toInt()),expected);
    QCOMPARE(stream["profile"].toString(),QString("High"));
    QVERIFY(stream["level"].toInt()>0);QVERIFY(stream["level"].toInt()<=level);
    QCOMPARE(stream["pix_fmt"].toString(),QString("yuv420p"));
    QCOMPARE(stream["r_frame_rate"].toString(),QString("60/1"));
    QCOMPARE(stream["nb_frames"].toString(),QString("6"));
}

void MediaInfoTest::exportResourcesAreBoundedAndKeepEveryFrame_data() {
    QTest::addColumn<bool>("composition");QTest::newRow("plain")<<false;QTest::newRow("composed")<<true;
}
void MediaInfoTest::exportResourcesAreBoundedAndKeepEveryFrame() {
    QFETCH(bool,composition);
    Project project;project.clips={{file("hd.mp4"),0,.1}};if(composition)project.clips[0].transform.scale.value=.8;
    QStringList diagnostics;QString error;const auto output=file("bounded.mp4");
    QVERIFY2(Media::exportMp4(project,output,{},&error,{},[&](const QString &line){diagnostics.append(line);},true),qPrintable(error));
    const auto resource=diagnostics.filter(QRegularExpression("^Resources: "));
    QCOMPARE(resource.size(),1);
    const auto match=QRegularExpression("real_time=(-[0-9]+) buffer=([0-9]+) prefill=([0-9]+)").match(resource.first());
    QVERIFY2(match.hasMatch(),qPrintable(resource.first()));
    QVERIFY(match.captured(1).toInt()<0);QVERIFY(match.captured(1).toInt()>=-8);
    const auto available=Performance::resources();
    if(!composition)QCOMPARE(match.captured(1).toInt(),-1);
    else if(available.logicalCpus>=4 && available.availableMemoryBytes>=quint64(8)*1024*1024*1024)
        QVERIFY2(match.captured(1).toInt()<-1,"A composed 1080p export should use spare render workers on a capable machine");
    qInfo().noquote()<<resource.first();
    QVERIFY(match.captured(2).toInt()>0);QVERIFY(match.captured(2).toInt()<=32);
    QVERIFY(match.captured(3).toInt()>0);QVERIFY(match.captured(3).toInt()<=match.captured(2).toInt());
    QProcess inspect;inspect.start(QStandardPaths::findExecutable("ffprobe"),{"-v","error","-select_streams","v:0","-count_frames","-show_entries","stream=nb_read_frames","-of","csv=p=0",output});
    QVERIFY(inspect.waitForFinished(10000));QCOMPARE(inspect.exitCode(),0);QCOMPARE(inspect.readAllStandardOutput().trimmed(),QByteArray("6"));
}

void MediaInfoTest::findsRecentAudioFiles() {
    QTemporaryDir downloads;
    QVERIFY(downloads.isValid());
    const auto addFile = [&](const QString &name, int age, const QByteArray &contents = "audio") {
        QFile entry(downloads.filePath(name));
        if (!entry.open(QIODevice::WriteOnly) || entry.write(contents) != contents.size() || !entry.flush()) return false;
        return entry.setFileTime(QDateTime::fromSecsSinceEpoch(1700000000 - age), QFileDevice::FileModificationTime);
    };
    QVERIFY(addFile("old music.mp3", 30));
    QVERIFY(addFile("newer.aiff", 20));
    QVERIFY(addFile("Newest & Bright.WAV", 10));
    QVERIFY(addFile("video.mp4", 0));
    QVERIFY(addFile("unfinished.mp3.part", 0));
    QVERIFY(addFile("empty.mp3", 0, {}));
    QVERIFY(QDir(downloads.path()).mkdir("folder.mp3"));

    const QStringList expected{downloads.filePath("Newest & Bright.WAV"),
                               downloads.filePath("newer.aiff"), downloads.filePath("old music.mp3")};
    QCOMPARE(Media::recentAudioFiles(downloads.path()), expected);
    QCOMPARE(Media::recentAudioFiles(downloads.path(), 2), expected.mid(0, 2));
    QVERIFY(Media::recentAudioFiles(downloads.path(), 0).isEmpty());
    QVERIFY(Media::recentAudioFiles(downloads.filePath("missing")).isEmpty());
    QVERIFY(Media::recentAudioFiles(QString()).isEmpty());

    QVERIFY(addFile("just downloaded.m4a", 0));
    QCOMPARE(Media::recentAudioFiles(downloads.path(), 1), QStringList{downloads.filePath("just downloaded.m4a")});
}

void MediaInfoTest::directExportKeepsHardwareOutsideTheEditorProcess() {
    Project project;project.clips={{file("2k.mp4"),0,.1}};
    QString error;QStringList diagnostics;const auto output=file("in-process.mp4");
    QVERIFY2(Media::exportMp4(project,output,{},&error,{},[&](const QString &line){diagnostics.append(line);}),qPrintable(error));
    QFile encoded(output);QVERIFY(encoded.open(QIODevice::ReadOnly));QVERIFY(encoded.readAll().contains("x264 - core"));
    QVERIFY(diagnostics.filter(QRegularExpression("^Hardware probe: ")).isEmpty());
    QCOMPARE(diagnostics.filter(QRegularExpression("^Encoder: ")),QStringList{"Encoder: libx264"});
    const auto metricsLines=diagnostics.filter(QRegularExpression("^Export metrics: "));QCOMPARE(metricsLines.size(),1);
    const auto metrics=QJsonDocument::fromJson(metricsLines.first().mid(16).toUtf8()).object();
    QCOMPARE(metrics["pipeline"].toString(),QString("mlt"));QCOMPARE(metrics["encoder"].toString(),QString("libx264"));
    QCOMPARE(metrics["frames"].toInt(),6);QCOMPARE(metrics["mode"].toString(),QString("native"));QVERIFY(metrics["totalMs"].toDouble()>=0);
    QVERIFY(metrics.contains("residentBytes"));QVERIFY(metrics.contains("peakResidentBytes"));
    QCOMPARE(diagnostics.filter(QRegularExpression("^Export stage: ")).size(),3);
}

void MediaInfoTest::successfulExportReplacesDestination() {
    const QString output = file(QString::fromUtf8("replace ü 中文.mp4"));
    const QByteArray original("keep existing video until export succeeds");
    QFile destination(output);
    QVERIFY(destination.open(QIODevice::WriteOnly));
    QCOMPARE(destination.write(original), original.size());
    destination.close();
    Project project;
    project.clips = {{file("hd.mp4"), 0, 0.1}};
    bool sawProgress = false;
    bool preservedDuringRender = true;
    QString error;
    QVERIFY2(Media::exportMp4(project, output, [&](int percent) {
        if (percent == 100) return;
        sawProgress = true;
        QFile existing(output);
        preservedDuringRender &= existing.open(QIODevice::ReadOnly) && existing.readAll() == original;
    }, &error), qPrintable(error));
    QVERIFY(sawProgress);
    QVERIFY(preservedDuringRender);
    QVERIFY(destination.open(QIODevice::ReadOnly));
    QCOMPARE(destination.read(8).mid(4), QByteArray("ftyp"));
    destination.close();
    // Windows refuses this move if the exporter or validator retains a file handle.
    QVERIFY(QFile::rename(output, file("renamed export.mp4")));
    QVERIFY(QDir(directory_.path()).entryList({".isaac-*.mp4"}, QDir::Files | QDir::Hidden).isEmpty());
}

void MediaInfoTest::unusedSourceAssetCannotBeExportDestination() {
    const auto asset=file("protected-asset.mp4");QVERIFY(QFile::copy(file("hd.mp4"),asset));QFile source(asset);QVERIFY(source.open(QIODevice::ReadOnly));const auto original=source.readAll();source.close();
    Project project;project.clips={{file("hd.mp4"),0,.1}};project.assets.append({asset,"video",6,0,0,6});
    QString error;QVERIFY(!Media::exportMp4(project,asset,{},&error,{}, {},true));QVERIFY(!error.isEmpty());
    QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),original);
}

void MediaInfoTest::corruptStillOverlayCannotPublishIncompleteComposition() {
    const auto still=file("corrupt-still.png");QImage image(8,8,QImage::Format_RGB32);image.fill(Qt::yellow);QVERIFY(image.save(still));
    QFile source(still);QVERIFY(source.open(QIODevice::ReadOnly));const auto header=source.read(33);source.close();QVERIFY(source.open(QIODevice::WriteOnly));QCOMPARE(source.write(header),header.size());source.close();
    QImageReader reader(still);QVERIFY(reader.canRead());QVERIFY(reader.read().isNull());
    const auto output=file("composition-must-stay.mp4");QFile destination(output);QVERIFY(destination.open(QIODevice::WriteOnly));destination.write("KEEP");destination.close();
    Project project;project.clips={{file("hd.mp4"),0,.1}};VisualOverlay overlay;overlay.path=still;overlay.durationFrames=6;project.visualOverlays={overlay};
    QString error;QVERIFY(!Media::exportMp4(project,output,{},&error,{}, {},true));QVERIFY2(error.contains("overlay",Qt::CaseInsensitive),qPrintable(error));
    QVERIFY(destination.open(QIODevice::ReadOnly));QCOMPARE(destination.readAll(),QByteArray("KEEP"));
}

void MediaInfoTest::failedExportPreservesDestination() {
    const QString output = file("existing.mp4");
    QFile destination(output);
    QVERIFY(destination.open(QIODevice::WriteOnly));
    QCOMPARE(destination.write("keep existing video"), 19);
    destination.close();
    Project project;
    project.clips = {{file("hd.mp4"), 0, 2}}; // The source is only 0.1 seconds.
    QString error;
    QVERIFY(!Media::exportMp4(project, output, {}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(destination.open(QIODevice::ReadOnly));
    QCOMPARE(destination.readAll(), QByteArray("keep existing video"));
    QVERIFY(QDir(directory_.path()).entryList({".isaac-*.mp4"}, QDir::Files | QDir::Hidden).isEmpty());
}

namespace {
mlt_producer clipSource(Mlt::Tractor &timeline, int row) {
    std::unique_ptr<Mlt::Producer> track(timeline.track(0));
    Mlt::Playlist video(*track);
    std::unique_ptr<Mlt::Producer> cut(video.get_clip(row));
    return cut->get_parent();
}
int centerRed(Mlt::Tractor &timeline, int position) {
    timeline.seek(position);
    std::unique_ptr<Mlt::Frame> frame(timeline.get_frame());
    int width=160,height=90;auto format=mlt_image_rgba;uint8_t *pixels=nullptr;
    if(!frame||mlt_frame_get_image(frame->get_frame(),&pixels,&format,&width,&height,0)||!pixels)return -1;
    return pixels[(height/2*width+width/2)*4];
}
}

void MediaInfoTest::playbackBuildReusesUnchangedSources() {
    Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);
    Project project;project.clips={{file("hd.mp4"),0,.1},{file("2k.mp4"),0,.1}};
    QString error;auto original=Media::buildTimeline(project,profile,&error);QVERIFY2(original,qPrintable(error));
    const auto first=clipSource(*original,0),second=clipSource(*original,1);
    project.clips.append({file("portrait.mp4"),0,.1});
    auto appended=Media::buildTimeline(project,profile,&error,false,original.get());QVERIFY2(appended,qPrintable(error));
    QCOMPARE(clipSource(*appended,0),first);QCOMPARE(clipSource(*appended,1),second);
    const auto third=clipSource(*appended,2);
    // A ready proxy replaces one source while the other decoders remain warm.
    project.clips[1].path=file("4k.mp4");
    auto replaced=Media::buildTimeline(project,profile,&error,false,appended.get());QVERIFY2(replaced,qPrintable(error));
    QCOMPARE(clipSource(*replaced,0),first);QVERIFY(clipSource(*replaced,1)!=second);QCOMPARE(clipSource(*replaced,2),third);
    original.reset();appended.reset();
    QVERIFY(centerRed(*replaced,0)>180);
    project.clips[0].transform.opacity.value=0;
    VisualRendering::update(*replaced,project);
    QVERIFY(centerRed(*replaced,1)<20);
}

void MediaInfoTest::playbackReuseInvalidatesChangedSettings() {
    const auto path=file("reuse.mp4");QVERIFY(QFile::copy(file("hd.mp4"),path));
    Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);
    Project project;project.clips={{path,0,.1}};
    auto original=Media::buildTimeline(project,profile);QVERIFY(original);
    const auto source=clipSource(*original,0);
    for(int change=0;change<4;++change){
        auto edited=project;
        if(change==0)edited.clips[0].audio.gainDb=-6;
        if(change==1)edited.clips[0].rotationDegrees=90;
        if(change==2)edited.clips[0].inSeconds=1./60;
        if(change==3)Media::configureOutputProfile(profile,320,180);
        auto next=Media::buildTimeline(edited,profile,nullptr,false,original.get());QVERIFY(next);
        QVERIFY(clipSource(*next,0)!=source);
    }
    Media::configureOutputProfile(profile,160,90);
    QFile changed(path);QVERIFY(changed.open(QIODevice::Append));QCOMPARE(changed.write("padding"),7);changed.close();
    auto replaced=Media::buildTimeline(project,profile,nullptr,false,original.get());QVERIFY(replaced);
    QVERIFY(clipSource(*replaced,0)!=source);
    QVERIFY(QFile::remove(path));
    auto offline=Media::buildTimeline(project,profile,nullptr,true,original.get());QVERIFY(offline);
    QVERIFY(clipSource(*offline,0)!=source);

#ifdef Q_OS_UNIX
    const auto link=file("linked.mp4"),other=file("same-metadata.mp4");
    QVERIFY(QFile::copy(file("hd.mp4"),other));
    QFile copy(other);QVERIFY(copy.open(QIODevice::ReadWrite));
    QVERIFY(copy.setFileTime(QFileInfo(file("hd.mp4")).lastModified(),QFileDevice::FileModificationTime));copy.close();
    QVERIFY(QFile::link(file("hd.mp4"),link));
    project.clips[0].path=link;
    auto linked=Media::buildTimeline(project,profile);QVERIFY(linked);
    QVERIFY(QFile::remove(link));QVERIFY(QFile::link(other,link));
    auto relinked=Media::buildTimeline(project,profile,nullptr,false,linked.get());QVERIFY(relinked);
    QVERIFY(clipSource(*relinked,0)!=clipSource(*linked,0));
#endif
}

QTEST_GUILESS_MAIN(MediaInfoTest)
#include "media_info_test.moc"
