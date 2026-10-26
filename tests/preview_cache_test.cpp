#include "preview_cache.h"
#include "mlt_engine.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QString executable(const QString &name) {
    QString path = QStandardPaths::findExecutable(name);
    return path.isEmpty() ? QStandardPaths::findExecutable(name, {"/opt/homebrew/bin", "/usr/local/bin"}) : path;
}
QByteArray run(const QString &name, const QStringList &arguments) {
    QProcess process;
    process.start(executable(name), arguments);
    if (!process.waitForFinished(15000) || process.exitCode() != 0) return {};
    return process.readAllStandardOutput();
}
QJsonArray streams(const QString &path) {
    const auto json = run("ffprobe", {"-v", "error", "-count_frames", "-show_streams", "-of", "json", path});
    return QJsonDocument::fromJson(json).object().value("streams").toArray();
}
QString colors(const QByteArray &rgb, int width, int height) {
    QString result;
    for (const QPoint point : {QPoint(1, 1), QPoint(3, 1), QPoint(1, 3), QPoint(3, 3)}) {
        const int index = ((height * point.y() / 4) * width + width * point.x() / 4) * 3;
        if (index + 2 >= rgb.size()) return {};
        const auto red = uchar(rgb[index]), green = uchar(rgb[index + 1]), blue = uchar(rgb[index + 2]);
        result += red > 180 && green < 80 && blue < 80 ? 'R'
            : green > 180 && red < 80 && blue < 80 ? 'G'
            : blue > 180 && red < 80 && green < 80 ? 'B'
            : red > 180 && green > 180 && blue < 80 ? 'Y' : '?';
    }
    return result;
}
int frameId(Mlt::Tractor &timeline, int position) {
    timeline.seek(position);
    std::unique_ptr<Mlt::Frame> frame(timeline.get_frame());
    if (!frame || !frame->is_valid()) return -1;
    auto format = mlt_image_rgba;
    int width = 128, height = 72;
    std::uint8_t *pixels = nullptr;
    if (mlt_frame_get_image(frame->get_frame(), &pixels, &format, &width, &height, 0) != 0 ||
        !pixels || format != mlt_image_rgba || width != 128 || height != 72) return -1;
    int id = 0;
    for (int bit = 0; bit < 12; ++bit)
        if (pixels[(36 * width + bit * 8 + 4) * 4] > 127) id |= 1 << bit;
    return id;
}
}

class PreviewCacheTest : public QObject {
    Q_OBJECT
private slots:
    void generatesExactFramesAndReusesCache();
    void preservesCameraOrientation();
    void retainsAudioAndPadsFinalFrame();
    void missingSourceFailsOnce();
    void sourceChangeInvalidatesReadyCopy();
    void deletedCacheRegeneratesAndNotifies();
    void removedSourcesCancelWithoutFailure();
    void nonSquarePixelsKeepDisplayAspect();
    void nativeCadenceMatchesOriginalTimeline_data();
    void nativeCadenceMatchesOriginalTimeline();
    void discardedCorruptCopyNeverReused();
    void usedRangesKeepOriginalFrames_data();
    void usedRangesKeepOriginalFrames();
    void pausesAndPrioritizesQueuedWork();
    void replacedRangesCancelObsoleteWork();
    void seekingPreemptsBackgroundPreparation();
    void variableCadenceKeepsOriginalFrames();
    void priorityDoesNotRestartACoveringJob();
    void irregularTimestampsWithNominalRateKeepOrigin();
    void partialAudioMatchesSource();
};

void PreviewCacheTest::generatesExactFramesAndReusesCache() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QFINDTESTDATA("fixtures/rotation.mp4");
    PreviewCache cache(directory.path());
    QSignalSpy ready(&cache, &PreviewCache::ready);
    QSignalSpy failed(&cache, &PreviewCache::failed);
    cache.request(source, 60);
    QVERIFY(cache.isBusy());
    QVERIFY(cache.readyPath(source).isEmpty());
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!cache.isBusy());
    const QString output = cache.readyPath(source);
    QVERIFY(!output.isEmpty());
    const auto video = streams(output).first().toObject();
    QCOMPARE(video.value("width").toInt(), 160);
    QCOMPARE(video.value("height").toInt(), 90);
    QCOMPARE(video.value("r_frame_rate").toString(), "60/1");
    QCOMPARE(video.value("nb_read_frames").toString(), "60");
    QCOMPARE(video.value("duration").toString().toDouble(), 1.0);
    cache.request(source, 60);
    QTest::qWait(20);
    QCOMPARE(ready.count(), 1);
    QCOMPARE(cache.readyPath(source), output);

    PreviewCache fresh(directory.path());
    QSignalSpy reused(&fresh, &PreviewCache::ready);
    fresh.request(source, 60);
    QCOMPARE(reused.count(), 0);
    QVERIFY(!fresh.isBusy());
    QCOMPARE(fresh.readyPath(source), output);
    QTRY_COMPARE_WITH_TIMEOUT(reused.count(), 1, 1000);
    QCOMPARE(QDir(directory.path()).entryList({"*.mov"}, QDir::Files).size(), 1);
}

void PreviewCacheTest::preservesCameraOrientation() {
    QTemporaryDir directory;
    const QString source = QFINDTESTDATA("fixtures/rotation-metadata.mp4");
    PreviewCache cache(directory.path());
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const QString output = cache.readyPath(source);
    const auto video = streams(output).first().toObject();
    QCOMPARE(video.value("width").toInt(), 90);
    QCOMPARE(video.value("height").toInt(), 160);
    for (const auto &side : video.value("side_data_list").toArray())
        QCOMPARE(side.toObject().value("rotation").toInt(0), 0);
    const QByteArray pixels = run("ffmpeg", {"-v", "error", "-i", output, "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"});
    QCOMPARE(colors(pixels, 90, 160), "GYRB");
}

void PreviewCacheTest::retainsAudioAndPadsFinalFrame() {
    QTemporaryDir directory;
    const QString fixture = QFINDTESTDATA("fixtures/rotation.mp4");
    const QString source = directory.filePath("source with spaces.mov");
    QProcess encode;
    encode.start(executable("ffmpeg"), {"-v", "error", "-y", "-i", fixture, "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=1", "-map", "0:v:0", "-map", "1:a:0", "-c:v", "copy", "-c:a", "pcm_s16le", "-t", "1", source});
    QVERIFY(encode.waitForFinished(10000));
    QCOMPARE(encode.exitCode(), 0);
    PreviewCache cache(directory.filePath("cache"));
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.request(source, 121);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const auto media = streams(cache.readyPath(source));
    QCOMPARE(media.size(), 2);
    const auto video = media.at(0).toObject(), audio = media.at(1).toObject();
    QCOMPARE(video.value("nb_read_frames").toString(), "121");
    QVERIFY(qAbs(video.value("duration").toString().toDouble() - 121.0 / 60) < 0.00001);
    QCOMPARE(audio.value("codec_name").toString(), "pcm_s16le");
    QCOMPARE(audio.value("sample_rate").toString(), "48000");
    QCOMPARE(audio.value("channels").toInt(), 2);
    QVERIFY(qAbs(audio.value("duration").toString().toDouble() - 121.0 / 60) < 0.0001);
}

void PreviewCacheTest::missingSourceFailsOnce() {
    QTemporaryDir directory;
    PreviewCache cache(directory.path());
    QSignalSpy failed(&cache, &PreviewCache::failed);
    const QString missing = directory.filePath("missing.mp4");
    cache.request(missing, 60);
    cache.request(missing, 60);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
    QVERIFY(!cache.isBusy());
    QVERIFY(cache.readyPath(missing).isEmpty());
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files).size(), 0);
}

void PreviewCacheTest::sourceChangeInvalidatesReadyCopy() {
    QTemporaryDir directory;
    const QString source = directory.filePath("source.mp4");
    QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/rotation.mp4"), source));
    PreviewCache cache(directory.filePath("cache"));
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const QString first = cache.readyPath(source);
    QVERIFY(!first.isEmpty());
    QFile changed(source);
    QVERIFY(changed.open(QIODevice::Append));
    QVERIFY(changed.write("\0", 1) == 1);
    changed.close();
    QVERIFY(cache.readyPath(source).isEmpty());
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QVERIFY(cache.readyPath(source) != first);
}

void PreviewCacheTest::deletedCacheRegeneratesAndNotifies() {
    QTemporaryDir directory;
    const QString source = QFINDTESTDATA("fixtures/rotation.mp4");
    PreviewCache cache(directory.path());
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const QString output = cache.readyPath(source);
    QVERIFY(QFile::remove(output));
    QVERIFY(cache.readyPath(source).isEmpty());
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(cache.readyPath(source), output);
}

void PreviewCacheTest::removedSourcesCancelWithoutFailure() {
    QTemporaryDir directory;
    const QString source = QFINDTESTDATA("fixtures/rotation.mp4");
    const QString other = directory.filePath("other.mp4");
    QVERIFY(QFile::copy(source, other));
    PreviewCache cache(directory.filePath("cache"));
    QSignalSpy ready(&cache, &PreviewCache::ready);
    QSignalSpy failed(&cache, &PreviewCache::failed);
    cache.request(source, 60);
    cache.request(other, 60);
    cache.retainSources({});
    QTRY_VERIFY_WITH_TIMEOUT(!cache.isBusy(), 2000);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(failed.count(), 0);
    QVERIFY(cache.readyPath(source).isEmpty());
    QVERIFY(cache.readyPath(other).isEmpty());
    QCOMPARE(QDir(directory.filePath("cache")).entryList({"*.part.mov"}, QDir::Files).size(), 0);
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QCOMPARE(failed.count(), 0);
}

void PreviewCacheTest::nonSquarePixelsKeepDisplayAspect() {
    QTemporaryDir directory;
    const QString source = directory.filePath("wide-pixels.mp4");
    QProcess encode;
    encode.start(executable("ffmpeg"), {"-v", "error", "-y", "-i", QFINDTESTDATA("fixtures/rotation.mp4"),
        "-vf", "setsar=2/1", "-c:v", "libx264", "-an", source});
    QVERIFY(encode.waitForFinished(10000));
    QCOMPARE(encode.exitCode(), 0);
    PreviewCache cache(directory.filePath("cache"));
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const auto video = streams(cache.readyPath(source)).first().toObject();
    QCOMPARE(video.value("width").toInt(), 320);
    QCOMPARE(video.value("height").toInt(), 90);
    QCOMPARE(video.value("sample_aspect_ratio").toString(), "1:1");
}

void PreviewCacheTest::nativeCadenceMatchesOriginalTimeline_data() {
    QTest::addColumn<QString>("fixture");
    QTest::newRow("30fps") << QString("fixtures/proxy-timing-30.mp4");
    QTest::newRow("29.97fps") << QString("fixtures/proxy-timing-2997.mp4");
    QTest::newRow("59.94fps") << QString("fixtures/proxy-timing-5994.mp4");
}

void PreviewCacheTest::nativeCadenceMatchesOriginalTimeline() {
    QFETCH(QString, fixture);
    const QString source = QTest::qFindTestData(qPrintable(fixture), __FILE__, __LINE__);
    QVERIFY(!source.isEmpty());
    const auto info = Media::mediaInfo(source);
    QVERIFY(info);
    const int frames = qRound(info->durationSeconds * Project::fps);
    QTemporaryDir directory;
    PreviewCache cache(directory.path());
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.request(source, frames);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const QString output = cache.readyPath(source);
    QVERIFY(!output.isEmpty());
    const auto originalStream = streams(source).first().toObject();
    const auto proxyStream = streams(output).first().toObject();
    QCOMPARE(proxyStream.value("r_frame_rate").toString(), originalStream.value("r_frame_rate").toString());
    QVERIFY(proxyStream.value("nb_read_frames").toString().toInt() <= frames);
    if (originalStream.value("r_frame_rate").toString() == "30/1")
        QCOMPARE(proxyStream.value("nb_read_frames").toString().toInt() * 2, frames);
    Mlt::Profile profile;
    Media::configureOutputProfile(profile, 128, 72);
    Project originalProject, proxyProject;
    originalProject.clips = {{source, 0, frames / double(Project::fps)}};
    proxyProject.clips = {{output, 0, frames / double(Project::fps)}};
    QString error;
    auto original = Media::buildTimeline(originalProject, profile, &error);
    QVERIFY2(original, qPrintable(error));
    auto proxy = Media::buildTimeline(proxyProject, profile, &error);
    QVERIFY2(proxy, qPrintable(error));
    QCOMPARE(original->get_playtime(), frames);
    QCOMPARE(proxy->get_playtime(), frames);
    for (int frame = 0; frame < frames; ++frame) {
        const int expected = frameId(*original, frame);
        QVERIFY(expected >= 0);
        QCOMPARE(frameId(*proxy, frame), expected);
    }
}

void PreviewCacheTest::discardedCorruptCopyNeverReused() {
    QTemporaryDir directory;
    const QString source = QFINDTESTDATA("fixtures/rotation.mp4");
    QFile original(source);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = original.readAll();
    original.close();
    PreviewCache cache(directory.path());
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const QString output = cache.readyPath(source);
    QFile corrupted(output);
    QVERIFY(corrupted.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(corrupted.write("not a video") > 0);
    corrupted.close();

    PreviewCache fresh(directory.path());
    QSignalSpy reused(&fresh, &PreviewCache::ready);
    fresh.request(source, 60);
    fresh.discard(source);
    QVERIFY(!QFile::exists(output));
    QVERIFY(fresh.readyPath(source).isEmpty());
    // A failed deletion or an external replacement must not revive a discarded key.
    QVERIFY(corrupted.open(QIODevice::WriteOnly));
    QVERIFY(corrupted.write("still not a video") > 0);
    corrupted.close();
    fresh.request(source, 60);
    QTest::qWait(30);
    QVERIFY(!fresh.isBusy());
    QVERIFY(fresh.readyPath(source).isEmpty());
    QCOMPARE(reused.count(), 0);
    fresh.discard(source);
    QVERIFY(!QFile::exists(output));
    fresh.discard(source + ".unknown");
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), originalBytes);
}

void PreviewCacheTest::usedRangesKeepOriginalFrames_data() {
    QTest::addColumn<QString>("fixture");
    QTest::addColumn<int>("loops");
    QTest::addColumn<QString>("extension");
    QTest::newRow("30fps") << QString("fixtures/proxy-timing-30.mp4") << 63 << QString(".mp4");
    QTest::newRow("29.97fps") << QString("fixtures/proxy-timing-2997.mp4") << 3 << QString(".mp4");
    QTest::newRow("59.94fps") << QString("fixtures/proxy-timing-5994.mp4") << 3 << QString(".mp4");
    QTest::newRow("30fps-matroska") << QString("fixtures/proxy-timing-30.mp4") << 63 << QString(".mkv");
}

void PreviewCacheTest::usedRangesKeepOriginalFrames() {
    QFETCH(QString, fixture);
    QFETCH(int, loops);
    QFETCH(QString, extension);
    QTemporaryDir directory;
    const QString source = directory.filePath("long" + extension);
    const QString original = QTest::qFindTestData(qPrintable(fixture), __FILE__, __LINE__);
    run("ffmpeg", {"-v", "error", "-y", "-stream_loop", QString::number(loops), "-i", original, "-c", "copy", source});
    const int total = qRound(Media::mediaDurationSeconds(source) * 60);
    QVERIFY(total > 1440);
    PreviewCache cache(directory.filePath("cache"));
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.setRequests({{source, total, 1300, 1380}});
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const auto copy = cache.readyCopy(source, 1300, 1380);
    QVERIFY(!copy.path.isEmpty());
    QVERIFY(copy.startFrame > 0);
    QVERIFY(copy.startFrame <= 1300);
    QVERIFY(streams(copy.path).first().toObject().value("duration").toString().toDouble() < total / 120.0);
    Mlt::Profile profile;
    Media::configureOutputProfile(profile, 128, 72);
    Project originalProject, proxyProject;
    originalProject.clips = {{source, 1300 / 60.0, 1380 / 60.0}};
    proxyProject.clips = {{copy.path, (1300 - copy.startFrame) / 60.0, (1380 - copy.startFrame) / 60.0}};
    QString error;
    auto originalTimeline = Media::buildTimeline(originalProject, profile, &error);
    QVERIFY2(originalTimeline, qPrintable(error));
    auto proxyTimeline = Media::buildTimeline(proxyProject, profile, &error);
    QVERIFY2(proxyTimeline, qPrintable(error));
    QCOMPARE(proxyTimeline->get_playtime(), 80);
    for (int frame = 0; frame < 80; ++frame)
        QCOMPARE(frameId(*proxyTimeline, frame), frameId(*originalTimeline, frame));
    cache.setRequests({{source, total, 1310, 1350}});
    QCOMPARE(cache.readyCopy(source, 1310, 1350).path, copy.path);
    QVERIFY(!cache.isBusy());
    QCOMPARE(QDir(directory.filePath("cache")).entryList({"*.mov"}, QDir::Files).size(), 1);
}

void PreviewCacheTest::pausesAndPrioritizesQueuedWork() {
    QTemporaryDir directory;
    const QString a = directory.filePath("a.mp4"), b = directory.filePath("b.mp4");
    QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/rotation.mp4"), a));
    QVERIFY(QFile::copy(a, b));
    PreviewCache cache(directory.filePath("cache"));
    QSignalSpy ready(&cache, &PreviewCache::ready);
    QSignalSpy failed(&cache, &PreviewCache::failed);
    cache.setPaused(true);
    cache.setRequests({{a, 60, 0, 60}, {b, 60, 0, 60}});
    QTest::qWait(100);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(QDir(directory.filePath("cache")).entryList({"*.part.mov"}, QDir::Files).size(), 0);
    cache.prioritize(b, 0, 60);
    cache.setPaused(false);
    // Pausing an active process must not publish a partial or report an error.
    cache.setPaused(true);
    QTest::qWait(100);
    QCOMPARE(ready.count(), 0);
    cache.setPaused(false);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(ready.first().first().toString(), b);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!cache.isBusy());
}

void PreviewCacheTest::replacedRangesCancelObsoleteWork() {
    QTemporaryDir directory;
    const QString source = QFINDTESTDATA("fixtures/proxy-timing-2997.mp4");
    const int total = qRound(Media::mediaDurationSeconds(source) * 60);
    PreviewCache cache(directory.path());
    QSignalSpy ready(&cache, &PreviewCache::ready);
    QSignalSpy failed(&cache, &PreviewCache::failed);
    cache.setRequests({{source, total, 0, total}});
    cache.setRequests({{source, total, 60, 120}});
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    QVERIFY(cache.readyPath(source).isEmpty());
    const auto copy = cache.readyCopy(source, 60, 120);
    QVERIFY(!copy.path.isEmpty());
    QVERIFY(streams(copy.path).first().toObject().value("duration").toString().toDouble() < 4.0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(QDir(directory.path()).entryList({"*.part.mov"}, QDir::Files).size(), 0);
}

void PreviewCacheTest::seekingPreemptsBackgroundPreparation() {
    QTemporaryDir directory;
    const QString a = directory.filePath("a.mp4"), b = directory.filePath("b.mp4");
    QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/rotation.mp4"), a));
    QVERIFY(QFile::copy(a, b));
    PreviewCache cache(directory.filePath("cache"));
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.setRequests({{a, 60, 0, 60}, {b, 60, 0, 60}});
    cache.prioritize(b, 0, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(ready.first().first().toString(), b);
}

void PreviewCacheTest::variableCadenceKeepsOriginalFrames() {
    QTemporaryDir directory;
    const QString source = directory.filePath("variable.mp4");
    run("ffmpeg", {"-v", "error", "-y", "-stream_loop", "3", "-i", QFINDTESTDATA("fixtures/proxy-timing-5994.mp4"),
        "-vf", "select='not(mod(n,2))+not(mod(n,7))'", "-fps_mode", "passthrough", "-c:v", "libx264", "-crf", "0", source});
    const int total = qRound(Media::mediaDurationSeconds(source) * 60);
    QVERIFY(total > 1000);
    const auto video = streams(source).first().toObject();
    QVERIFY(video.value("avg_frame_rate").toString() != video.value("r_frame_rate").toString());
    PreviewCache cache(directory.filePath("cache"));
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.setRequests({{source, total, 300, 420}});
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const auto copy = cache.readyCopy(source, 300, 420);
    QVERIFY(!copy.path.isEmpty());
    QCOMPARE(copy.startFrame, 0);
    Mlt::Profile profile;
    Media::configureOutputProfile(profile, 128, 72);
    Project originalProject, proxyProject;
    originalProject.clips = {{source, 5, 7}};
    proxyProject.clips = {{copy.path, 5, 7}};
    auto original = Media::buildTimeline(originalProject, profile);
    auto proxy = Media::buildTimeline(proxyProject, profile);
    QVERIFY(original);
    QVERIFY(proxy);
    for (int frame = 0; frame < 120; ++frame)
        QCOMPARE(frameId(*proxy, frame), frameId(*original, frame));
}

void PreviewCacheTest::priorityDoesNotRestartACoveringJob() {
    QTemporaryDir directory;
    const QString source = QFINDTESTDATA("fixtures/proxy-timing-5994.mp4");
    const int total = qRound(Media::mediaDurationSeconds(source) * 60);
    PreviewCache cache(directory.path());
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.setRequests({{source, total, 60, 120}, {source, total, 100, 160}});
    cache.prioritize(source, 60, 120);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    const auto video = streams(ready.first().at(1).toString()).first().toObject();
    QVERIFY(video.value("duration").toString().toDouble() <= 3.01);
}

void PreviewCacheTest::irregularTimestampsWithNominalRateKeepOrigin() {
    QTemporaryDir directory;
    const QString source = directory.filePath("nominal-rate.mkv");
    run("ffmpeg", {"-v", "error", "-y", "-stream_loop", "63", "-i", QFINDTESTDATA("fixtures/proxy-timing-30.mp4"),
        "-vf", "settb=1/60000,setpts=N*2000+eq(mod(N\\,7)\\,1)*1000", "-enc_time_base", "1/60000",
        "-fps_mode", "passthrough", "-c:v", "libx264", "-crf", "0", "-bf", "0", source});
    const auto video = streams(source).first().toObject();
    QCOMPARE(video.value("r_frame_rate").toString(), QString("30/1"));
    QCOMPARE(video.value("avg_frame_rate").toString(), QString("30/1"));
    const int total = qRound(Media::mediaDurationSeconds(source) * 60);
    PreviewCache cache(directory.filePath("cache"));
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.setRequests({{source, total, 300, 420}});
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const auto copy = cache.readyCopy(source, 300, 420);
    QVERIFY(!copy.path.isEmpty());
    QCOMPARE(copy.startFrame, 0);
    Mlt::Profile profile;
    Media::configureOutputProfile(profile, 128, 72);
    Project originalProject, proxyProject;
    originalProject.clips = {{source, 5, 7}};
    proxyProject.clips = {{copy.path, 5, 7}};
    auto original = Media::buildTimeline(originalProject, profile);
    auto proxy = Media::buildTimeline(proxyProject, profile);
    QVERIFY(original);
    QVERIFY(proxy);
    for (int frame = 0; frame < 120; ++frame)
        QCOMPARE(frameId(*proxy, frame), frameId(*original, frame));
}

void PreviewCacheTest::partialAudioMatchesSource() {
    QTemporaryDir directory;
    const QString source = directory.filePath("long.mov");
    run("ffmpeg", {"-v", "error", "-y", "-stream_loop", "19", "-i", QFINDTESTDATA("fixtures/rotation.mp4"),
        "-f", "lavfi", "-i", "sine=frequency=443.3:sample_rate=48000:duration=20", "-map", "0:v:0", "-map", "1:a:0",
        "-c:v", "copy", "-c:a", "pcm_s16le", "-t", "20", source});
    PreviewCache cache(directory.filePath("cache"));
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.setRequests({{source, 1200, 600, 660}});
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const auto copy = cache.readyCopy(source, 600, 660);
    QVERIFY(copy.startFrame > 0);
    const auto audioAt = [&](const QString &path, double start) {
        return run("ffmpeg", {"-v", "error", "-ss", QString::number(start, 'f', 9), "-i", path,
            "-t", "1", "-map", "0:a:0", "-ar", "48000", "-ac", "2", "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"});
    };
    const auto original = audioAt(source, 10);
    const auto preview = audioAt(copy.path, (600 - copy.startFrame) / 60.0);
    QCOMPARE(original.size(), 192000);
    QCOMPARE(preview, original);
}

QTEST_GUILESS_MAIN(PreviewCacheTest)
#include "preview_cache_test.moc"
