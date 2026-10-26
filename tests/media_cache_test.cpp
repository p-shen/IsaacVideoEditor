#include "media_cache.h"

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
    void exactFrameKeys();
    void anamorphicSourceIsBounded();
    void retryWhileRunningKeepsReplacement();
    void waveformDistinguishesSilenceAndMissingAudio();
    void interactiveDefersLaunches();
    void priorityFramesCompleteDuringInteraction();
    void priorityFramesRunAheadOfBackground();
    void priorityKeepsLatestTwoQueuedFrames();
    void viewportRetiresQueuedThumbnails();
    void retainDropsAndCancels();
    void sourceIdentityInvalidates();
    void missingSourceAndRetry();
    void failedFfmpegSpawnDoesNotLoop();
    void queueIsBounded();
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
    MediaCache cache;
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

void MediaCacheTest::exactFrameKeys() {
    const QString path = QFINDTESTDATA("fixtures/proxy-timing-30.mp4");
    MediaCache cache;
    QVERIFY(cache.thumbnail(path, 0).isNull());
    QVERIFY(cache.thumbnail(path, 1.0 / 60).isNull());
    QVERIFY(cache.thumbnail(path, 2.0 / 60).isNull());
    QVERIFY(cache.thumbnail(path, 4.0 / 60).isNull());
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 4.0 / 60).isNull(), 10000);
    QCOMPARE(frameId(cache.thumbnail(path, 0)), 0);
    QCOMPARE(frameId(cache.thumbnail(path, 1.0 / 60)), 0);
    QCOMPARE(frameId(cache.thumbnail(path, 2.0 / 60)), 1);
    QCOMPARE(frameId(cache.thumbnail(path, 4.0 / 60)), 2);
}

void MediaCacheTest::anamorphicSourceIsBounded() {
    QTemporaryDir directory;
    const QString source = directory.filePath("wide-pixels.mp4");
    QVERIFY(encode({"-v", "error", "-i", QFINDTESTDATA("fixtures/rotation.mp4"), "-vf", "setsar=2/1", "-an", source}));
    MediaCache cache;
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
    MediaCache cache;
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
    MediaCache cache;
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
    MediaCache cache;
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

void MediaCacheTest::priorityFramesCompleteDuringInteraction() {
    const QString path = QFINDTESTDATA("fixtures/proxy-timing-30.mp4");
    MediaCache cache;
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
    MediaCache cache;
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
    MediaCache cache;
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
    MediaCache cache;
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
    MediaCache cache;
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
    MediaCache cache;
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
    MediaCache cache;
    QSignalSpy changed(&cache, &MediaCache::changed);
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
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path, 0).isNull(), 10000);
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
    MediaCache cache;
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
    MediaCache cache;
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

QTEST_GUILESS_MAIN(MediaCacheTest)
#include "media_cache_test.moc"
