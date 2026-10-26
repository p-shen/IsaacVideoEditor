#include "mlt_engine.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
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
    void keepsTheCanvasLandscape();
    void roundsOddDimensionsForH264();
    void invalidatesChangedAndMissingSources();
    void rejectsEmptyAndNonVideoProjects();
    void exportUsesLowestResolution();
    void findsRecentAudioFiles();
    void prefersHardwareWhenAvailable();
    void failedExportPreservesDestination();
    void successfulExportReplacesDestination();
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

void MediaInfoTest::keepsTheCanvasLandscape() {
    Project project;
    project.clips = {{file("4k.mp4"), 0, 0.1}, {file("portrait.mp4"), 0, 0.1}};
    const auto size = Media::projectOutputSize(project);
    QVERIFY(size);
    QCOMPARE(size->width, 1280);
    QCOMPARE(size->height, 720);
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

void MediaInfoTest::prefersHardwareWhenAvailable() {
#ifndef Q_OS_MACOS
    QSKIP("The automatic hardware export currently targets macOS.");
#else
    QProcess probe;
    probe.start(ffmpeg_, {"-v", "error", "-f", "lavfi", "-i", "color=s=320x180:r=60:d=0.1",
        "-c:v", "h264_videotoolbox", "-allow_sw", "0", "-f", "null", "-"});
    QVERIFY(probe.waitForFinished(10000));
    if (probe.exitCode() != 0) QSKIP("VideoToolbox is unavailable in this execution environment.");
    Project project;
    project.clips = {{file("2k.mp4"), 0, 0.1}};
    const QString output = file("hardware.mp4");
    QString error;
    QVERIFY2(Media::exportMp4(project, output, {}, &error), qPrintable(error));
    QFile encoded(output);
    QVERIFY(encoded.open(QIODevice::ReadOnly));
    // libx264 identifies itself in its H.264 user-data SEI; VideoToolbox does not.
    QVERIFY2(!encoded.readAll().contains("x264 - core"), "Export selected libx264 despite available hardware.");
    // Simulate an inspector reporting an incomplete hardware video, then verify a real software retry.
    const QString tools = file("tools");
    QVERIFY(QDir().mkpath(tools));
    QFile inspect(tools + "/ffprobe");
    QVERIFY(inspect.open(QIODevice::WriteOnly));
    QVERIFY(inspect.write("#!/bin/sh\nprintf '1\\n'\n") > 0);
    inspect.close();
    QVERIFY(inspect.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    const QByteArray previousPath = qgetenv("PATH");
    const auto restorePath = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", QFile::encodeName(tools) + QDir::listSeparator().toLatin1() + previousPath);
    const QString retried = file("retried.mp4");
    QVERIFY2(Media::exportMp4(project, retried, {}, &error), qPrintable(error));
    QFile software(retried);
    QVERIFY(software.open(QIODevice::ReadOnly));
    QVERIFY2(software.readAll().contains("x264 - core"), "An incomplete hardware export was not retried with software.");
#endif
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

QTEST_GUILESS_MAIN(MediaInfoTest)
#include "media_info_test.moc"
