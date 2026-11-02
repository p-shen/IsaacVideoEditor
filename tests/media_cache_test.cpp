#include "media_cache.h"
#include "mlt_engine.h"

#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

namespace {
QString executable(const QString &name) {
    const QString path = QStandardPaths::findExecutable(name);
    return path.isEmpty() ? QStandardPaths::findExecutable(name, {"/opt/homebrew/bin", "/usr/local/bin"}) : path;
}
bool encode(const QStringList &arguments) {
    QProcess process;
    process.start(executable("ffmpeg"), arguments);
    return process.waitForFinished(10000) && process.exitCode() == 0;
}
QByteArray corners(const QImage &image) {
    QByteArray result;
    for (const QPoint point : {QPoint(1, 1), QPoint(3, 1), QPoint(1, 3), QPoint(3, 3)}) {
        const QColor c = image.pixelColor(image.width() * point.x() / 4, image.height() * point.y() / 4);
        result += c.red() > 180 && c.green() < 80 && c.blue() < 80 ? 'R'
            : c.green() > 180 && c.red() < 80 && c.blue() < 80 ? 'G'
            : c.blue() > 180 && c.red() < 80 && c.green() < 80 ? 'B'
            : c.red() > 180 && c.green() > 180 && c.blue() < 80 ? 'Y' : '?';
    }
    return result;
}
int frameId(const QImage &image) {
    int result = 0;
    for (int bit = 0; bit < 12; ++bit)
        if (image.pixelColor((bit * 8 + 4) * image.width() / 128, image.height() / 2).red() > 127)
            result |= 1 << bit;
    return result;
}
}

class MediaCacheTest : public QObject {
    Q_OBJECT
private slots:
    void thumbnailsRespectOrientation_data();
    void thumbnailsRespectOrientation();
    void nearbyFilmstripFramesShareOneProcess();
    void batchedFilmstripMatchesExactFrames_data();
    void batchedFilmstripMatchesExactFrames();
    void batchedVfrWithOffsetMatchesExactFrames();
    void priorityPreemptsBlockedFilmstripBatch_data();
    void priorityPreemptsBlockedFilmstripBatch();
    void exactFrameKeys();
    void filmstripReusesNearbyCompletedFrames();
    void filmstripReusesNearbyPendingFrames();
    void trimComparisonMatchesTimeline_data();
    void trimComparisonMatchesTimeline();
    void trimComparisonKeepsFinalSourceFrame_data();
    void trimComparisonKeepsFinalSourceFrame();
    void anamorphicSourceIsBounded();
    void retryWhileRunningKeepsReplacement();
    void waveformDistinguishesSilenceAndMissingAudio();
    void interactiveDefersLaunches();
    void capablePolicyPreparesWhileInteractive();
    void priorityFramesCompleteDuringInteraction();
    void priorityFramesRunAheadOfBackground();
    void priorityKeepsLatestTwoQueuedFrames();
    void viewportRetiresQueuedThumbnails();
    void retainDropsAndCancels();
    void sourceIdentityInvalidates();
    void missingSourceAndRetry();
    void failedFfmpegSpawnDoesNotLoop();
    void queueIsBounded();
    void runsBoundedConcurrentJobs();
    void priorityThumbnailPreemptsBlockedWaveforms_data();
    void priorityThumbnailPreemptsBlockedWaveforms();
    void configuredByteBudgetsEvictOldEntries();
    void replacesSourcesWhileTwoJobsAreRunning();
};

void MediaCacheTest::thumbnailsRespectOrientation_data() {
    QTest::addColumn<QString>("fixture");
    QTest::addColumn<int>("rotation");
    QTest::addColumn<QByteArray>("expected");
    QTest::addColumn<bool>("portrait");
    const QList<QByteArray> plain{"RGBY", "BRYG", "YBGR", "GYRB"};
    const QList<QByteArray> camera{"GYRB", "RGBY", "BRYG", "YBGR"};
    for (int i = 0; i < 4; ++i) {
        QTest::newRow(qPrintable(QString("plain-%1").arg(i * 90))) << QString("fixtures/rotation.mp4") << i * 90 << plain[i] << bool(i % 2);
        QTest::newRow(qPrintable(QString("camera-%1").arg(i * 90))) << QString("fixtures/rotation-metadata.mp4") << i * 90 << camera[i] << bool(!(i % 2));
    }
}

void MediaCacheTest::thumbnailsRespectOrientation() {
    QFETCH(QString, fixture);
    QFETCH(int, rotation);
    QFETCH(QByteArray, expected);
    QFETCH(bool, portrait);
    const QString path = QTest::qFindTestData(qPrintable(fixture), __FILE__, __LINE__);
    MediaCache cache(nullptr, Performance::Budget{});
    QSignalSpy changed(&cache, &MediaCache::changed);
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(cache.thumbnail(path, 0.25, rotation).isNull());
    QVERIFY(elapsed.elapsed() < 50);
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 10000);
    const QImage image = cache.thumbnail(path, 0.25, rotation);
    QVERIFY(!image.isNull());
    QCOMPARE(corners(image), expected);
    QVERIFY(image.width() <= 160 && image.height() <= 90);
    const double aspect = double(image.width()) / image.height();
    QVERIFY(qAbs(aspect - (portrait ? 9.0 / 16 : 16.0 / 9)) < 0.04);
    QCOMPARE(cache.thumbnail(path, 0.25, rotation), image);
}

void MediaCacheTest::nearbyFilmstripFramesShareOneProcess() {
    const QString path = QFINDTESTDATA("fixtures/proxy-timing-30.mp4");
    MediaCache cache(nullptr, Performance::Budget{});
    QSignalSpy processes(cache.findChild<QProcess *>(), &QProcess::started);
    for (int frame : {0, 3, 6, 9, 12, 15}) cache.thumbnail(path, frame / 60.0, 0, false, 1);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 15.0 / 60, 0, false, 1).isNull(), 10000);
    QCOMPARE(processes.count(), 1);
    for (int frame : {0, 3, 6, 9, 12, 15})
        QCOMPARE(frameId(cache.thumbnail(path, frame / 60.0)), (frame + 1) / 2);
}

void MediaCacheTest::batchedFilmstripMatchesExactFrames_data() {
    QTest::addColumn<QString>("fixture");
    for (const QString &name : {"proxy-timing-30.mp4", "proxy-timing-2997.mp4", "proxy-timing-5994.mp4"})
        QTest::newRow(qPrintable(name)) << "fixtures/" + name;
}

void MediaCacheTest::batchedFilmstripMatchesExactFrames() {
    QFETCH(QString, fixture);
    const auto source = QTest::qFindTestData(qPrintable(fixture), __FILE__, __LINE__);
    const int frames = qRound(Media::mediaDurationSeconds(source) * 60);
    for (int start : {0, 1, 59, 121, 299}) {
        if (start + 11 >= frames) continue;
        MediaCache batched(nullptr, Performance::Budget{}), exact(nullptr, Performance::Budget{});
        for (int frame : {start, start + 3, start + 8, start + 11}) {
            batched.thumbnail(source, frame / 60.0, 0, false, 1);
            exact.thumbnail(source, frame / 60.0);
        }
        for (int frame : {start, start + 3, start + 8, start + 11}) {
            QTRY_VERIFY_WITH_TIMEOUT(!batched.thumbnail(source, frame / 60.0).isNull(), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(!exact.thumbnail(source, frame / 60.0).isNull(), 5000);
            QCOMPARE(frameId(batched.thumbnail(source, frame / 60.0)), frameId(exact.thumbnail(source, frame / 60.0)));
        }
    }
}

void MediaCacheTest::batchedVfrWithOffsetMatchesExactFrames() {
    QTemporaryDir directory;
    const auto source = directory.filePath("offset-variable.mkv");
    QVERIFY(encode({"-v", "error", "-copyts", "-i", QFINDTESTDATA("fixtures/proxy-timing-2997.mp4"),
        "-vf", "select='not(mod(n,2))+not(mod(n,7))',setpts=PTS+2/TB", "-fps_mode", "passthrough",
        "-c:v", "libx264", "-crf", "0", source}));
    QProcess timing;
    timing.start(executable("ffprobe"), {"-v", "error", "-select_streams", "v:0", "-show_entries", "stream=start_time",
        "-of", "default=noprint_wrappers=1:nokey=1", source});
    QVERIFY(timing.waitForFinished(5000));
    QCOMPARE(timing.exitCode(), 0);
    QVERIFY(timing.readAllStandardOutput().trimmed().toDouble() >= 1.9);
    MediaCache batched(nullptr, Performance::Budget{}), exact(nullptr, Performance::Budget{});
    for (int frame : {1, 4, 9, 12, 299, 302, 307, 310}) {
        batched.thumbnail(source, frame / 60.0, 0, false, 1);
        exact.thumbnail(source, frame / 60.0);
    }
    for (int frame : {1, 4, 9, 12, 299, 302, 307, 310}) {
        QTRY_VERIFY_WITH_TIMEOUT(!batched.thumbnail(source, frame / 60.0).isNull(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!exact.thumbnail(source, frame / 60.0).isNull(), 5000);
        QCOMPARE(frameId(batched.thumbnail(source, frame / 60.0)), frameId(exact.thumbnail(source, frame / 60.0)));
    }
}

void MediaCacheTest::priorityPreemptsBlockedFilmstripBatch_data() {
    QTest::addColumn<int>("priorityFrame");
    QTest::newRow("new-frame") << 9;
    QTest::newRow("already-in-batch") << 3;
}

void MediaCacheTest::priorityPreemptsBlockedFilmstripBatch() {
#ifdef Q_OS_WIN
    QSKIP("The blocked-process fixture uses a POSIX shell.");
#else
    QFETCH(int, priorityFrame);
    QTemporaryDir directory;
    const auto ffmpeg = executable("ffmpeg");
    QFile script(directory.filePath("ffmpeg"));
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/bin/sh\nfor arg in \"$@\"; do case \"$arg\" in *select=*) exec /bin/sleep 30;; esac; done\nexec \"" + ffmpeg.toUtf8() + "\" \"$@\"\n");
    script.close();
    QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto previous = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previous); });
    qputenv("PATH", directory.path().toUtf8());
    MediaCache cache(nullptr, Performance::Budget{});
    const auto source = QFINDTESTDATA("fixtures/proxy-timing-30.mp4");
    for (int frame : {0, 3, 6}) cache.thumbnail(source, frame / 60.0, 0, false, 1);
    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(cache.findChild<QProcess *>()->state() != QProcess::NotRunning, 1000);
    cache.setInteractive(true);
    cache.thumbnail(source, priorityFrame / 60.0, 0, true);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(source, priorityFrame / 60.0, 0, true).isNull(), 2000);
    cache.retainSources({});
#endif
}

void MediaCacheTest::exactFrameKeys() {
    const QString path = QFINDTESTDATA("fixtures/proxy-timing-30.mp4");
    MediaCache cache(nullptr, Performance::Budget{});
    QVERIFY(cache.thumbnail(path, 0).isNull());
    QVERIFY(cache.thumbnail(path, 1.0 / 60).isNull());
    QVERIFY(cache.thumbnail(path, 2.0 / 60).isNull());
    QVERIFY(cache.thumbnail(path, 4.0 / 60).isNull());
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 4.0 / 60).isNull(), 10000);
    QCOMPARE(frameId(cache.thumbnail(path, 0)), 0);
    QCOMPARE(frameId(cache.thumbnail(path, 1.0 / 60)), 1);
    QCOMPARE(frameId(cache.thumbnail(path, 2.0 / 60)), 1);
    QCOMPARE(frameId(cache.thumbnail(path, 4.0 / 60)), 2);
}

void MediaCacheTest::trimComparisonMatchesTimeline_data() {
    QTest::addColumn<QString>("fixture");
    QTest::addColumn<int>("finalNativeFrame");
    QTest::newRow("30fps") << QString("fixtures/proxy-timing-30.mp4") << 11;
    QTest::newRow("29.97fps") << QString("fixtures/proxy-timing-2997.mp4") << 299;
    QTest::newRow("59.94fps") << QString("fixtures/proxy-timing-5994.mp4") << 599;
}

void MediaCacheTest::trimComparisonMatchesTimeline() {
    QFETCH(QString, fixture);
    const QString path = QTest::qFindTestData(qPrintable(fixture), __FILE__, __LINE__);
    Media::initializeMlt();
    Mlt::Profile profile;
    Media::configureOutputProfile(profile, 128, 72);
    MediaCache cache(nullptr, Performance::Budget{});
    const int frames = qRound(Media::mediaDurationSeconds(path) * 60);
    // MLT can round a cold seek at EOF past the last native frame; test that preview separately.
    for (int start : {0, 1, 2, 3, 7, 17, 21, 22, 30, 59, 60, 63, 121, 299, 300, 599}) {
        if (start >= frames - 1) continue;
        Project p;
        p.clips = {{path, start / 60.0, frames / 60.0}};
        QString error;
        auto timeline = Media::buildTimeline(p, profile, &error);
        QVERIFY2(timeline, qPrintable(error));
        std::unique_ptr<Mlt::Frame> frame(timeline->get_frame());
        auto format = mlt_image_rgba;
        int width = 128, height = 72;
        uint8_t *pixels = nullptr;
        QCOMPARE(mlt_frame_get_image(frame->get_frame(), &pixels, &format, &width, &height, 0), 0);
        QVERIFY(pixels);
        const QImage expected(pixels, width, height, width * 4, QImage::Format_RGBA8888);
        QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, start / 60.0, 0, true).isNull(), 3000);
        const int actual = frameId(cache.thumbnail(path, start / 60.0));
        QVERIFY2(actual == frameId(expected), qPrintable(QString("At source frame %1: trim comparison %2, timeline %3")
            .arg(start).arg(actual).arg(frameId(expected))));
    }
}

void MediaCacheTest::trimComparisonKeepsFinalSourceFrame_data() {
    trimComparisonMatchesTimeline_data();
}

void MediaCacheTest::trimComparisonKeepsFinalSourceFrame() {
    QFETCH(QString, fixture);
    QFETCH(int, finalNativeFrame);
    const QString path = QTest::qFindTestData(qPrintable(fixture), __FILE__, __LINE__);
    const int finalFrame = qRound(Media::mediaDurationSeconds(path) * 60) - 1;
    MediaCache cache(nullptr, Performance::Budget{});
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, finalFrame / 60.0, 0, true).isNull(), 3000);
    QCOMPARE(frameId(cache.thumbnail(path, finalFrame / 60.0)), finalNativeFrame);
}

void MediaCacheTest::anamorphicSourceIsBounded() {
    QTemporaryDir directory;
    const QString source = directory.filePath("wide-pixels.mp4");
    QVERIFY(encode({"-v", "error", "-i", QFINDTESTDATA("fixtures/rotation.mp4"), "-vf", "setsar=2/1", "-an", source}));
    MediaCache cache(nullptr, Performance::Budget{});
    cache.thumbnail(source, 0);
    cache.thumbnail(source, 0, 90);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(source, 0, 90).isNull(), 10000);
    const QImage landscape = cache.thumbnail(source, 0);
    const QImage portrait = cache.thumbnail(source, 0, 90);
    QVERIFY(landscape.width() <= 160 && landscape.height() <= 90);
    QVERIFY(portrait.width() <= 160 && portrait.height() <= 90);
    QVERIFY(qAbs(double(landscape.width()) / landscape.height() - 32.0 / 9) < 0.04);
    QVERIFY(qAbs(double(portrait.width()) / portrait.height() - 9.0 / 32) < 0.04);
}

void MediaCacheTest::retryWhileRunningKeepsReplacement() {
    const QString path = QFINDTESTDATA("fixtures/rotation.mp4");
    MediaCache cache(nullptr, Performance::Budget{});
    QSignalSpy changed(&cache, &MediaCache::changed);
    cache.thumbnail(path, 0);
    QCoreApplication::processEvents();
    QVERIFY(cache.findChild<QProcess *>()->state() != QProcess::NotRunning);
    cache.retry(path);
    cache.thumbnail(path, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 0).isNull(), 10000);
    QTest::qWait(50);
    QCOMPARE(changed.count(), 2);
}

void MediaCacheTest::waveformDistinguishesSilenceAndMissingAudio() {
    QTemporaryDir directory;
    const QString tone = directory.filePath("quarter amplitude.wav");
    const QString silence = directory.filePath("silence.wav");
    const QString negative = directory.filePath("negative-full-scale.wav");
    QVERIFY(encode({"-v", "error", "-f", "lavfi", "-i", "aevalsrc=0.25:s=2000:d=0.12", "-c:a", "pcm_s16le", tone}));
    QVERIFY(encode({"-v", "error", "-f", "lavfi", "-i", "anullsrc=r=2000:cl=mono", "-t", "0.10", "-c:a", "pcm_s16le", silence}));
    QVERIFY(encode({"-v", "error", "-f", "lavfi", "-i", "aevalsrc=-1:s=2000:d=0.05", "-c:a", "pcm_s16le", negative}));
    const QString noAudio = QFINDTESTDATA("fixtures/rotation.mp4");
    MediaCache cache(nullptr, Performance::Budget{});
    QCOMPARE(cache.waveformState(tone), QString());
    QVERIFY(cache.waveform(tone).isEmpty());
    QCOMPARE(cache.waveformState(tone), QString("loading"));
    QVERIFY(cache.waveform(silence).isEmpty());
    QVERIFY(cache.waveform(noAudio).isEmpty());
    QVERIFY(cache.waveform(negative).isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(tone), QString("ready"), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(silence), QString("ready"), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(noAudio), QString("unavailable"), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(negative), QString("ready"), 10000);
    QCOMPARE(cache.waveform(negative), QVector<float>({1}));
    const auto peaks = cache.waveform(tone);
    QCOMPARE(peaks.size(), 3);
    for (const float peak : peaks) QVERIFY(qAbs(peak - 0.25f) < 0.001f);
    QCOMPARE(cache.waveform(silence), QVector<float>({0, 0}));
    QVERIFY(cache.waveform(noAudio).isEmpty());
}

void MediaCacheTest::interactiveDefersLaunches() {
    const QString path = QFINDTESTDATA("fixtures/rotation.mp4");
    MediaCache cache(nullptr, Performance::Budget{});
    QSignalSpy changed(&cache, &MediaCache::changed);
    cache.setInteractive(true);
    cache.thumbnail(path, 0);
    cache.waveform(path);
    QTest::qWait(150);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(cache.waveformState(path), QString("loading"));
    cache.setInteractive(false);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 0).isNull(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(path), QString("unavailable"), 10000);
}

void MediaCacheTest::capablePolicyPreparesWhileInteractive() {
    QTemporaryDir directory;
    const auto audio = directory.filePath("tone.wav");
    QVERIFY(encode({"-v", "error", "-f", "lavfi", "-i", "aevalsrc=0.25:s=2000:d=0.10", "-c:a", "pcm_s16le", audio}));
    Performance::Budget budget;
    budget.prepareDuringExport = true;
    budget.mediaJobs = 2;
    budget.backgroundThreads = 4;
    MediaCache cache(nullptr, budget);
    cache.setInteractive(true);
    const auto video = QFINDTESTDATA("fixtures/rotation.mp4");
    cache.thumbnail(video, 0);
    cache.waveform(audio);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(video, 0).isNull() && cache.waveformState(audio) == "ready", 2000);
    QCOMPARE(cache.waveform(audio), QVector<float>({0.25f, 0.25f}));
}

void MediaCacheTest::priorityFramesCompleteDuringInteraction() {
    const QString path = QFINDTESTDATA("fixtures/proxy-timing-30.mp4");
    MediaCache cache(nullptr, Performance::Budget{});
    QSignalSpy changed(&cache, &MediaCache::changed);
    cache.setInteractive(true);
    cache.thumbnail(path, 0);
    // A comparison request must promote an already queued background key.
    cache.thumbnail(path, 0, 0, true);
    cache.thumbnail(path, 2.0 / 60, 0, true);
    cache.beginViewport();
    cache.thumbnail(path, 10.0 / 60);
    cache.endViewport();
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 2.0 / 60, 0, true).isNull(), 2000);
    QCOMPARE(frameId(cache.thumbnail(path, 0, 0, true)), 0);
    QCOMPARE(frameId(cache.thumbnail(path, 2.0 / 60, 0, true)), 1);
    QCOMPARE(changed.count(), 2);
    QVERIFY(cache.thumbnail(path, 10.0 / 60).isNull());
    QTest::qWait(50);
    QCOMPARE(changed.count(), 2);
    cache.setInteractive(false);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 10.0 / 60).isNull(), 2000);
}

void MediaCacheTest::priorityFramesRunAheadOfBackground() {
    const QString path = QFINDTESTDATA("fixtures/proxy-timing-30.mp4");
    MediaCache cache(nullptr, Performance::Budget{});
    bool sawCompletion = false;
    bool firstWasPriority = false;
    connect(&cache, &MediaCache::changed, &cache, [&] {
        if (sawCompletion) return;
        sawCompletion = true;
        firstWasPriority = !cache.thumbnail(path, 2.0 / 60).isNull() && cache.thumbnail(path, 0).isNull();
    });
    cache.thumbnail(path, 0);
    cache.thumbnail(path, 2.0 / 60, 0, true);
    QTRY_VERIFY_WITH_TIMEOUT(sawCompletion, 2000);
    QVERIFY(firstWasPriority);
    cache.retainSources({});
}

void MediaCacheTest::priorityKeepsLatestTwoQueuedFrames() {
    const QString path = QFINDTESTDATA("fixtures/proxy-timing-30.mp4");
    MediaCache cache(nullptr, Performance::Budget{});
    QSignalSpy changed(&cache, &MediaCache::changed);
    cache.setInteractive(true);
    cache.thumbnail(path, 0, 0, true);
    QCoreApplication::processEvents();
    QVERIFY(cache.findChild<QProcess *>()->state() != QProcess::NotRunning);
    for (int frame = 2; frame <= 10; frame += 2) cache.thumbnail(path, frame / 60.0, 0, true);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 10.0 / 60, 0, true).isNull(), 2000);
    QCOMPARE(changed.count(), 3);
    QVERIFY(!cache.thumbnail(path, 0).isNull());
    QVERIFY(!cache.thumbnail(path, 8.0 / 60).isNull());
    for (const int obsolete : {2, 4, 6}) QVERIFY(cache.thumbnail(path, obsolete / 60.0).isNull());
}

void MediaCacheTest::viewportRetiresQueuedThumbnails() {
    const QString path = QFINDTESTDATA("fixtures/proxy-timing-30.mp4");
    MediaCache cache(nullptr, Performance::Budget{});
    cache.setInteractive(true);
    cache.waveform(path);
    cache.beginViewport();
    cache.thumbnail(path, 0);
    cache.endViewport();
    cache.beginViewport();
    cache.thumbnail(path, 2.0 / 60);
    cache.endViewport();
    cache.setInteractive(false);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 2.0 / 60).isNull(), 10000);
    cache.setInteractive(true);
    QVERIFY(cache.thumbnail(path, 0).isNull());
    QCOMPARE(cache.waveformState(path), QString("unavailable"));
}

void MediaCacheTest::retainDropsAndCancels() {
    const QString path = QFINDTESTDATA("fixtures/rotation.mp4");
    MediaCache cache(nullptr, Performance::Budget{});
    cache.thumbnail(path, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 0).isNull(), 10000);
    cache.retainSources({});
    cache.setInteractive(true);
    QVERIFY(cache.thumbnail(path, 0).isNull());
    cache.waveform(path);
    cache.retainSources({});
    QCOMPARE(cache.waveformState(path), QString());
    cache.setInteractive(false);
    QTest::qWait(100);
    QCOMPARE(cache.waveformState(path), QString());
    cache.thumbnail(path, 0);
    QCoreApplication::processEvents();
    cache.retainSources({});
    QTest::qWait(100);
    cache.setInteractive(true);
    QVERIFY(cache.thumbnail(path, 0).isNull());
}

void MediaCacheTest::sourceIdentityInvalidates() {
    QTemporaryDir directory;
    const QString path = directory.filePath("source.mp4");
    QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/rotation.mp4"), path));
    MediaCache cache(nullptr, Performance::Budget{});
    cache.thumbnail(path, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 0).isNull(), 10000);
    QFile file(path);
    QVERIFY(file.open(QIODevice::Append));
    QCOMPARE(file.write("\0", 1), qint64(1));
    file.close();
    cache.setInteractive(true);
    QVERIFY(cache.thumbnail(path, 0).isNull());
    cache.setInteractive(false);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 0).isNull(), 10000);
}

void MediaCacheTest::missingSourceAndRetry() {
    QTemporaryDir directory;
    const QString path = directory.filePath("missing.mp4");
    MediaCache cache(nullptr, Performance::Budget{});
    QSignalSpy changed(&cache, &MediaCache::changed);
    auto *process = cache.findChild<QProcess *>();
    const QString errors = directory.filePath("ffmpeg-errors");
    process->setStandardErrorFile(errors);
    const auto failureDetails = [&] {
        QFile log(errors), source(path);
        log.open(QIODevice::ReadOnly);
        source.open(QIODevice::ReadOnly);
        return QString("FFmpeg exit %1, state %2, source bytes %3, header %4, errors: %5")
            .arg(process->exitCode()).arg(int(process->state())).arg(source.size())
            .arg(QString::fromLatin1(source.read(32).toHex()), QString::fromUtf8(log.readAll()));
    };
    cache.waveform(path);
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(path), QString("unavailable"), 1000);
    const int failures = changed.count();
    for (int i = 0; i < 5; ++i) cache.waveform(path);
    QTest::qWait(100);
    QCOMPARE(changed.count(), failures);
    cache.retry(path);
    QCOMPARE(cache.waveformState(path), QString());
    cache.waveform(path);
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(path), QString("unavailable"), 1000);
    QVERIFY(changed.count() > failures);
    QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/rotation.mp4"), path));
    QVERIFY(cache.thumbnail(path, 0).isNull());
    QTRY_VERIFY2_WITH_TIMEOUT(!cache.thumbnail(path, 0).isNull(), qPrintable(failureDetails()), 10000);
}

void MediaCacheTest::failedFfmpegSpawnDoesNotLoop() {
    QTemporaryDir directory;
    const QString fake = directory.filePath("ffmpeg");
    QFile file(fake);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("#!/a/missing/interpreter\n");
    file.close();
    QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const QByteArray previousPath = qgetenv("PATH");
    const auto restorePath = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });
    qputenv("PATH", directory.path().toUtf8());
    MediaCache cache(nullptr, Performance::Budget{});
    QSignalSpy errors(cache.findChild<QProcess *>(), &QProcess::errorOccurred);
    const QString source = QFINDTESTDATA("fixtures/rotation.mp4");
    QSignalSpy changed(&cache, &MediaCache::changed);
    cache.waveform(source);
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(source), QString("unavailable"), 1000);
    const int failures = changed.count();
    for (int i = 0; i < 5; ++i) cache.waveform(source);
    QTest::qWait(100);
    QCOMPARE(changed.count(), failures);
    for (int i = 0; i < 5; ++i) cache.thumbnail(source, i / 60.0);
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= failures + 5, 1000);
    QCOMPARE(errors.count(), 1);
    qputenv("PATH", previousPath);
    cache.retry(source);
    cache.thumbnail(source, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(source, 0).isNull(), 10000);
}

void MediaCacheTest::queueIsBounded() {
    const QString path = QFINDTESTDATA("fixtures/rotation.mp4");
    MediaCache cache(nullptr, Performance::Budget{});
    cache.setInteractive(true);
    for (int i = 0; i < 130; ++i) cache.thumbnail(path, (i % 60) / 60.0, (i / 60) * 90);
    cache.setInteractive(false);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 9.0 / 60, 180).isNull(), 30000);
    cache.setInteractive(true);
    // Completing the final request also drains all older retained work.
    QVERIFY(cache.thumbnail(path, 0).isNull());
    QVERIFY(cache.thumbnail(path, 1.0 / 60).isNull());
    QVERIFY(!cache.thumbnail(path, 2.0 / 60).isNull());
}

void MediaCacheTest::runsBoundedConcurrentJobs() {
#ifdef Q_OS_WIN
    QSKIP("The blocked-process fixture uses a POSIX shell.");
#else
    QTemporaryDir directory;
    QFile script(directory.filePath("ffmpeg"));
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/bin/sh\nexec /bin/sleep 30\n");
    script.close();
    QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto previousPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", directory.path().toUtf8());
    Performance::Budget budget;
    budget.mediaJobs = 2;
    budget.backgroundThreads = 2;
    MediaCache cache(nullptr, budget);
    QSignalSpy changed(&cache, &MediaCache::changed);
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    for (int frame = 0; frame < 3; ++frame) cache.thumbnail(source, frame / 60.0);
    const auto running = [&] {
        int count = 0;
        for (auto *process : cache.findChildren<QProcess *>()) count += process->state() != QProcess::NotRunning;
        return count;
    };
    QTRY_COMPARE_WITH_TIMEOUT(running(), 2, 1000);
    for (auto *process : cache.findChildren<QProcess *>()) {
        if (process->state() == QProcess::NotRunning) continue;
        const auto arguments = process->arguments();
        QCOMPARE(arguments.count("-threads"), 2);
        QCOMPARE(arguments[arguments.indexOf("-threads") + 1], QString("2"));
        QCOMPARE(arguments[arguments.lastIndexOf("-threads") + 1], QString("2"));
        QCOMPARE(arguments[arguments.indexOf("-filter_threads") + 1], QString("2"));
    }
    cache.retainSources({});
    QTRY_COMPARE_WITH_TIMEOUT(running(), 0, 1000);
    QCOMPARE(changed.count(), 0);
    qputenv("PATH", previousPath);
    cache.thumbnail(source, 0);
    cache.thumbnail(source, 1.0 / 60);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(source, 0).isNull() && !cache.thumbnail(source, 1.0 / 60).isNull(), 5000);
#endif
}

void MediaCacheTest::priorityThumbnailPreemptsBlockedWaveforms_data() {
    QTest::addColumn<int>("jobs");
    QTest::newRow("conservative") << 1;
    QTest::newRow("workstation") << 2;
}

void MediaCacheTest::priorityThumbnailPreemptsBlockedWaveforms() {
#ifdef Q_OS_WIN
    QSKIP("The blocked-process fixture uses a POSIX shell.");
#else
    QFETCH(int, jobs);
    QTemporaryDir directory;
    const auto realFfmpeg = executable("ffmpeg");
    QFile script(directory.filePath("ffmpeg"));
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/bin/sh\nfor arg in \"$@\"; do if [ \"$arg\" = s16le ]; then exec /bin/sleep 30; fi; done\nexec \"" + realFfmpeg.toUtf8() + "\" \"$@\"\n");
    script.close();
    QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto previousPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", directory.path().toUtf8());
    Performance::Budget budget;
    budget.mediaJobs = jobs;
    MediaCache cache(nullptr, budget);
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    QStringList sources;
    for (int i = 0; i < jobs; ++i) {
        const auto copy = directory.filePath(QString("waveform%1.mp4").arg(i));
        QVERIFY(QFile::copy(source, copy));
        sources.append(copy);
        cache.waveform(copy);
    }
    QCoreApplication::processEvents();
    int active = 0;
    for (auto *process : cache.findChildren<QProcess *>()) active += process->state() != QProcess::NotRunning;
    QCOMPARE(active, jobs);
    cache.setInteractive(true);
    cache.thumbnail(source, 0, 0, true);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(source, 0, 0, true).isNull(), 2000);
    for (const auto &copy : sources) QCOMPARE(cache.waveformState(copy), QString("loading"));
    cache.retainSources({});
#endif
}

void MediaCacheTest::configuredByteBudgetsEvictOldEntries() {
    QTemporaryDir directory;
    const auto first = directory.filePath("first.wav"), second = directory.filePath("second.wav");
    QVERIFY(encode({"-v", "error", "-f", "lavfi", "-i", "aevalsrc=0.25:s=2000:d=0.10", "-c:a", "pcm_s16le", first}));
    QVERIFY(encode({"-v", "error", "-f", "lavfi", "-i", "aevalsrc=0.5:s=2000:d=0.10", "-c:a", "pcm_s16le", second}));
    Performance::Budget budget;
    budget.thumbnailBytes = 60000; // One 160 by 90 RGBA thumbnail.
    budget.waveformBytes = 8; // Two float peaks, enough for one of these waveforms.
    MediaCache cache(nullptr, budget);
    cache.waveform(first);
    cache.waveform(second);
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(second), QString("ready"), 5000);
    QCOMPARE(cache.waveform(second), QVector<float>({0.5f, 0.5f}));
    const auto source = QFINDTESTDATA("fixtures/proxy-timing-30.mp4");
    cache.thumbnail(source, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(source, 0).isNull(), 5000);
    cache.thumbnail(source, 2.0 / 60);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(source, 2.0 / 60).isNull(), 5000);
    cache.setInteractive(true);
    QVERIFY(cache.thumbnail(source, 0).isNull());
    QVERIFY(!cache.thumbnail(source, 2.0 / 60).isNull());
    QVERIFY(cache.waveform(first).isEmpty());
    QCOMPARE(cache.waveformState(first), QString("loading"));
    QCOMPARE(cache.waveform(second), QVector<float>({0.5f, 0.5f}));
}

void MediaCacheTest::replacesSourcesWhileTwoJobsAreRunning() {
#ifdef Q_OS_WIN
    QSKIP("The blocked-process fixture uses a POSIX shell.");
#else
    QTemporaryDir directory;
    QFile script(directory.filePath("ffmpeg"));
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/bin/sh\nexec /bin/sleep 30\n");
    script.close();
    QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto previousPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", directory.path().toUtf8());
    const auto source = directory.filePath("changing.mp4");
    QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/rotation.mp4"), source));
    Performance::Budget budget;
    budget.mediaJobs = 2;
    MediaCache cache(nullptr, budget);
    QSignalSpy changed(&cache, &MediaCache::changed);
    cache.thumbnail(source, 0);
    cache.thumbnail(source, 1.0 / 60);
    QCoreApplication::processEvents();
    int active = 0;
    for (auto *process : cache.findChildren<QProcess *>()) active += process->state() != QProcess::NotRunning;
    QCOMPARE(active, 2);
    QVERIFY(QFile::remove(source));
    QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/proxy-timing-30.mp4"), source));
    qputenv("PATH", previousPath);
    cache.thumbnail(source, 0);
    cache.thumbnail(source, 2.0 / 60);
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 2, 5000);
    QCOMPARE(frameId(cache.thumbnail(source, 0)), 0);
    QCOMPARE(frameId(cache.thumbnail(source, 2.0 / 60)), 1);
    cache.setInteractive(true);
    QVERIFY(cache.thumbnail(source, 1.0 / 60).isNull());
#endif
}

QTEST_GUILESS_MAIN(MediaCacheTest)
void MediaCacheTest::filmstripReusesNearbyCompletedFrames() {
    const auto path=QFINDTESTDATA("fixtures/seek.mp4");
    MediaCache cache(nullptr, Performance::Budget{});cache.thumbnail(path,.5);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path,.5).isNull(),10000);
    const auto original=cache.thumbnail(path,.5);
    cache.setInteractive(true);
    QCOMPARE(cache.thumbnail(path,.6,0,false,12),original);
    QVERIFY(cache.thumbnail(path,.6).isNull());
    QVERIFY(cache.thumbnail(path,.8,0,false,1).isNull());
    QVERIFY(cache.thumbnail(path,.6,90,false,12).isNull());
}

void MediaCacheTest::filmstripReusesNearbyPendingFrames() {
    const auto path=QFINDTESTDATA("fixtures/seek.mp4");
    MediaCache cache(nullptr, Performance::Budget{});cache.setInteractive(true);
    cache.beginViewport();cache.thumbnail(path,.5);cache.endViewport();
    cache.beginViewport();cache.thumbnail(path,.6,0,false,12);cache.endViewport();
    cache.setInteractive(false);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path,.6,0,false,12).isNull(),10000);
    cache.setInteractive(true);
    QVERIFY(!cache.thumbnail(path,.5).isNull());
    QVERIFY(cache.thumbnail(path,.6).isNull());
}

#include "media_cache_test.moc"
