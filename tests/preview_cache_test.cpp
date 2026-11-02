#include "preview_cache.h"
#include "mlt_engine.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QScopeGuard>
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
    void storageQuotaPreservesActiveAndUnrelatedFiles();
    void quotaEvictsLeastRecentlyUsedOwnedCopies();
    void copiedCacheOnlyPrunesOwnedArtifacts();
    void concurrentInstancesShareOneEncoding();
    void quotaProtectsInflightCopies();
    void copyLeaseProtectsDetachedDecoder();
    void invalidCopyWaitsForDecoderLease_data();
    void invalidCopyWaitsForDecoderLease();
    void missingInvalidCopyRegenerates();
    void sharedCacheProtectsOtherInstances();
    void sharedCacheProtectsOtherProcesses();
    void cacheLeaseChild();
    void persistentTimingChecksAreReused();
    void cacheDirectoryAndQualityChangesRetireOldWork();
    void generatesExactFramesAndReusesCache();
    void preservesCameraOrientation();
    void retainsAudioAndPadsFinalFrame();
    void unwritableCacheFailsOnce();
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
    void runsBoundedConcurrentJobsAndCancelsAll();
    void hardwareFailureRetriesSoftwareOnce();
    void configuredDimensionsHaveSeparateCopies();
    void sourceReplacementRetiresBothWorkers();
    void hardwareCrashAndBadSourcePreserveReadyCopy();
    void pausingFullPoolKeepsQueueBounded();
    void concurrentRangeProbesKeepIndependentTiming();
    void retiredRequestsCannotDeliverQueuedReady_data();
    void retiredRequestsCannotDeliverQueuedReady();
};

void PreviewCacheTest::storageQuotaPreservesActiveAndUnrelatedFiles() {
    QTemporaryDir directory;
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    QFile unrelated(directory.filePath("family-video.mov"));
    QVERIFY(unrelated.open(QIODevice::WriteOnly));
    QCOMPARE(unrelated.write("original footage"), qint64(16));
    unrelated.close();
    QString output;
    {
        PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
        QVERIFY(cache.setProperty("storageLimit", qint64(1)));
        cache.request(source, 60);
        QTRY_VERIFY_WITH_TIMEOUT(!cache.readyPath(source).isEmpty(), 5000);
        output = cache.readyPath(source);
        QVERIFY(QFile::exists(output)); // The active working set may exceed the quota.
        cache.retainSources({});
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(output), 1000);
    }
    QVERIFY(QFile::exists(unrelated.fileName()));
}

void PreviewCacheTest::quotaEvictsLeastRecentlyUsedOwnedCopies() {
    QTemporaryDir directory;
    const auto fixture = QFINDTESTDATA("fixtures/rotation.mp4");
    const auto secondSource = directory.filePath("second.mp4");
    QVERIFY(QFile::copy(fixture, secondSource));
    QString older, newer;
    {
        PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
        cache.request(fixture, 60);
        cache.request(secondSource, 60);
        QTRY_VERIFY_WITH_TIMEOUT(!cache.readyPath(fixture).isEmpty() && !cache.readyPath(secondSource).isEmpty(), 5000);
        older = cache.readyPath(fixture); newer = cache.readyPath(secondSource);
    }
    QFile oldRecord(older + ".isaac-cache.json");
    QVERIFY(oldRecord.open(QIODevice::ReadOnly));
    QVERIFY(oldRecord.setFileTime(QDateTime::currentDateTimeUtc().addDays(-1), QFileDevice::FileModificationTime));
    oldRecord.close();
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
    cache.setStorageLimit(QFileInfo(newer).size() + QFileInfo(newer + ".isaac-cache.json").size());
    QVERIFY(!QFile::exists(older));
    QVERIFY(QFile::exists(newer));
}

void PreviewCacheTest::copiedCacheOnlyPrunesOwnedArtifacts() {
    QTemporaryDir directory;
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    const auto originalDirectory = directory.filePath("original");
    QString artifact;
    {
        PreviewCache cache(originalDirectory, nullptr, Performance::Budget{});
        cache.request(source, 60);
        QTRY_VERIFY_WITH_TIMEOUT(!cache.readyPath(source).isEmpty(), 5000);
        artifact = QFileInfo(cache.readyPath(source)).fileName();
    }
    const auto copied = directory.filePath("copied");
    QVERIFY(QDir().mkpath(copied));
    for (const auto &file : QDir(originalDirectory).entryList(QDir::Files))
        QVERIFY(QFile::copy(QDir(originalDirectory).filePath(file), QDir(copied).filePath(file)));
    const auto unrelated = QDir(copied).filePath(QString(64, 'a') + ".mov");
    QFile file(unrelated);
    QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("user media") > 0); file.close();
    const auto forged = unrelated + ".isaac-cache.json";
    file.setFileName(forged);
    QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("{\"owner\":\"someone else\"}") > 0); file.close();
    PreviewCache cache(copied, nullptr, Performance::Budget{});
    cache.setStorageLimit(0);
    QVERIFY(!QFile::exists(QDir(copied).filePath(artifact)));
    QVERIFY(QFile::exists(unrelated));
    QVERIFY(QFile::exists(forged));
    QVERIFY(QFile::exists(QDir(originalDirectory).filePath(artifact)));
}

void PreviewCacheTest::concurrentInstancesShareOneEncoding() {
    QTemporaryDir directory;
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    PreviewCache first(directory.path(), nullptr, Performance::Budget{}), second(directory.path(), nullptr, Performance::Budget{});
    QSignalSpy firstReady(&first, &PreviewCache::ready), secondReady(&second, &PreviewCache::ready);
    QSignalSpy firstStarted(first.findChild<QProcess *>(), &QProcess::started), secondStarted(second.findChild<QProcess *>(), &QProcess::started);
    first.request(source, 60); second.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(firstReady.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(secondReady.count(), 1, 5000);
    QVERIFY(firstStarted.count() >= 1 && firstStarted.count() <= 2); // A failed hardware attempt may retry in software.
    QCOMPARE(secondStarted.count(), 0);
    QCOMPARE(first.readyPath(source), second.readyPath(source));
}

void PreviewCacheTest::quotaProtectsInflightCopies() {
#ifdef Q_OS_WIN
    QSKIP("The blocked-process fixture uses a POSIX shell.");
#else
    QTemporaryDir directory;
    QFile script(directory.filePath("ffmpeg"));
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/bin/sh\nfor output in \"$@\"; do :; done\nprintf partial > \"$output\"\nexec /bin/sleep 30\n");
    script.close();
    QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto previous = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previous); });
    qputenv("PATH", directory.path().toUtf8());
    const auto cacheDirectory = directory.filePath("cache");
    PreviewCache first(cacheDirectory, nullptr, Performance::Budget{});
    first.request(QFINDTESTDATA("fixtures/rotation.mp4"), 60);
    QString partial;
    const auto hasPartial = [&] {
        const auto files = QDir(cacheDirectory).entryList({"*.part.mov"}, QDir::Files);
        if (files.isEmpty()) return false;
        partial = QDir(cacheDirectory).filePath(files.first());
        return QFileInfo(partial).size() > 0;
    };
    QTRY_VERIFY_WITH_TIMEOUT(hasPartial(), 1000);
    PreviewCache prune(cacheDirectory, nullptr, Performance::Budget{});
    prune.setStorageLimit(0);
    QVERIFY(QFile::exists(partial));
    first.retainSources({});
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(partial), 1000);
#endif
}

void PreviewCacheTest::copyLeaseProtectsDetachedDecoder() {
    QTemporaryDir directory;
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    PreviewCache::Copy copy;
    {
        PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
        cache.request(source, 60);
        QTRY_VERIFY_WITH_TIMEOUT(!cache.readyPath(source).isEmpty(), 5000);
        copy = cache.readyCopy(source, 0, 60);
        QVERIFY(copy.lease);
        cache.discard(source);
        QVERIFY(QFile::exists(copy.path));
    }
    PreviewCache prune(directory.path(), nullptr, Performance::Budget{});
    prune.setStorageLimit(0);
    QVERIFY(QFile::exists(copy.path));
    copy.lease.reset();
    prune.pruneStorage();
    QVERIFY(!QFile::exists(copy.path));
}

void PreviewCacheTest::invalidCopyWaitsForDecoderLease_data() {
    QTest::addColumn<QString>("refresh");
    QTest::newRow("directory-round-trip") << QString("directory");
    QTest::newRow("quality-round-trip") << QString("quality");
    QTest::newRow("other-instance-refresh") << QString("other-instance");
}

void PreviewCacheTest::invalidCopyWaitsForDecoderLease() {
    QFETCH(QString, refresh);
    QTemporaryDir directory;
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    const auto cacheDirectory = directory.filePath("cache");
    PreviewCache first(cacheDirectory, nullptr, Performance::Budget{});
    first.request(source, 60);
    QTRY_VERIFY_WITH_TIMEOUT(!first.readyPath(source).isEmpty(), 5000);
    std::unique_ptr<PreviewCache> second;
    PreviewCache *consumer = &first;
    if (refresh == "other-instance") {
        second = std::make_unique<PreviewCache>(cacheDirectory, nullptr, Performance::Budget{});
        second->request(source, 60);
        QTRY_VERIFY_WITH_TIMEOUT(!second->readyPath(source).isEmpty(), 1000);
        consumer = second.get();
    }
    auto copy = consumer->readyCopy(source, 0, 60);
    QVERIFY(copy.lease);
    first.discard(source);
    QVERIFY(QFile::exists(copy.path));
    consumer->setPaused(true);
    if (refresh == "directory") {
        QVERIFY(consumer->setCacheDirectory(directory.filePath("other")));
        QVERIFY(consumer->setCacheDirectory(cacheDirectory));
    } else if (refresh == "quality") {
        const auto quality = consumer->qualityLimit();
        consumer->setQualityLimit({128, 72});
        consumer->setQualityLimit(quality);
    } else consumer->setRequests({{source, 60, 0, 60}});
    QSignalSpy started(consumer->findChild<QProcess *>(), &QProcess::started);
    consumer->setPaused(false);
    QTest::qWait(250); // Exercise multiple shared-cache retry attempts while the decoder still holds its pin.
    QCOMPARE(started.count(), 0);
    QVERIFY(consumer->readyCopy(source, 0, 60).path.isEmpty());
    QVERIFY(QFile::exists(copy.path));
    QFile record(copy.path + ".isaac-cache.json");
    QVERIFY(record.open(QIODevice::ReadOnly));
    QVERIFY(QJsonDocument::fromJson(record.readAll()).object().value("invalid").toBool());
    record.close();
    copy.lease.reset();
    QTRY_VERIFY_WITH_TIMEOUT(!consumer->readyCopy(source, 0, 60).path.isEmpty(), 5000);
    QCOMPARE(consumer->readyCopy(source, 0, 60).path, copy.path);
    QVERIFY(started.count() > 0);
}

void PreviewCacheTest::missingInvalidCopyRegenerates() {
    QTemporaryDir directory;
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    QString output;
    {
        PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
        cache.request(source, 60);
        QTRY_VERIFY_WITH_TIMEOUT(!cache.readyPath(source).isEmpty(), 5000);
        auto copy = cache.readyCopy(source, 0, 60);
        output = copy.path;
        cache.discard(source);
        QVERIFY(QFile::exists(output + ".isaac-cache.json"));
        QVERIFY(QFile::remove(output)); // Simulate losing the media after recording its invalidation.
        copy.lease.reset();
    }
    PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
    QSignalSpy failed(&cache, &PreviewCache::failed);
    cache.request(source, 60);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.readyPath(source).isEmpty(), 5000);
    QCOMPARE(cache.readyPath(source), output);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!cache.isBusy());
}

void PreviewCacheTest::sharedCacheProtectsOtherInstances() {
    QTemporaryDir directory;
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    PreviewCache first(directory.path(), nullptr, Performance::Budget{});
    first.request(source, 60);
    QTRY_VERIFY_WITH_TIMEOUT(!first.readyPath(source).isEmpty(), 5000);
    const auto output = first.readyPath(source);
    PreviewCache second(directory.path(), nullptr, Performance::Budget{});
    QVERIFY(second.setProperty("storageLimit", qint64(1)));
    QVERIFY(QFile::exists(output));
    first.retainSources({});
    QVERIFY(second.setProperty("storageLimit", qint64(1)));
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(output), 1000);
}

void PreviewCacheTest::cacheLeaseChild() {
    const auto source = qEnvironmentVariable("ISAAC_TEST_LEASE_SOURCE");
    if (source.isEmpty()) QSKIP("Only used by the cross-process lease regression.");
    PreviewCache cache(qEnvironmentVariable("ISAAC_TEST_LEASE_DIRECTORY"), nullptr, Performance::Budget{});
    cache.request(source, 60);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.readyPath(source).isEmpty(), 5000);
    fprintf(stdout, "LEASE_READY\n");
    fflush(stdout);
    QTest::qWait(30000);
}

void PreviewCacheTest::sharedCacheProtectsOtherProcesses() {
    QTemporaryDir directory;
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    QString output;
    {
        PreviewCache prepare(directory.path(), nullptr, Performance::Budget{});
        prepare.request(source, 60);
        QTRY_VERIFY_WITH_TIMEOUT(!prepare.readyPath(source).isEmpty(), 5000);
        output = prepare.readyPath(source);
    }
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("ISAAC_TEST_LEASE_SOURCE", source);
    environment.insert("ISAAC_TEST_LEASE_DIRECTORY", directory.path());
    process.setProcessEnvironment(environment);
    process.start(QCoreApplication::applicationFilePath(), {"cacheLeaseChild"});
    QByteArray messages;
    const auto childReady = [&] { messages += process.readAllStandardOutput(); return messages.contains("LEASE_READY"); };
    QTRY_VERIFY_WITH_TIMEOUT(childReady(), 6000);
    const auto cleanup = qScopeGuard([&] { process.kill(); process.waitForFinished(1000); });
    PreviewCache prune(directory.path(), nullptr, Performance::Budget{});
    QVERIFY(prune.setProperty("storageLimit", qint64(1)));
    QVERIFY(QFile::exists(output));
    process.kill();
    QVERIFY(process.waitForFinished(1000));
    QVERIFY(prune.setProperty("storageLimit", qint64(1)));
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(output), 1000);
}

void PreviewCacheTest::persistentTimingChecksAreReused() {
#ifdef Q_OS_WIN
    QSKIP("The counting-process fixture uses a POSIX shell.");
#else
    QTemporaryDir directory;
    const auto source = directory.filePath("long.mp4");
    run("ffmpeg", {"-v", "error", "-y", "-stream_loop", "63", "-i", QFINDTESTDATA("fixtures/proxy-timing-30.mp4"), "-c", "copy", source});
    const int frames = qRound(Media::mediaDurationSeconds(source) * 60);
    QVERIFY(frames > 1200);
    const auto probe = executable("ffprobe");
    const auto log = directory.filePath("probes");
    QFile script(directory.filePath("ffprobe"));
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/bin/sh\necho probe >> \"" + log.toUtf8() + "\"\nexec \"" + probe.toUtf8() + "\" \"$@\"\n");
    script.close();
    QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto previous = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previous); });
    qputenv("PATH", directory.path().toUtf8());
    for (int range : {300, 900}) {
        Performance::Budget budget;
        budget.previewWidth = range == 300 ? 160 : 128;
        PreviewCache cache(directory.filePath("cache"), nullptr, budget);
        cache.setRequests({{source, frames, range, range + 60}});
        QTRY_VERIFY_WITH_TIMEOUT(!cache.readyCopy(source, range, range + 60).path.isEmpty(), 10000);
        QVERIFY(cache.readyCopy(source, range, range + 60).startFrame > 0);
    }
    QFile attempts(log);
    QVERIFY(attempts.open(QIODevice::ReadOnly));
    QCOMPARE(attempts.readAll().count("probe"), 1);
#endif
}

void PreviewCacheTest::cacheDirectoryAndQualityChangesRetireOldWork() {
    QTemporaryDir directory;
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    PreviewCache cache(directory.filePath("first"), nullptr, Performance::Budget{});
    cache.request(source, 60);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.readyPath(source).isEmpty(), 5000);
    const auto output = cache.readyPath(source);
    cache.setProperty("storageLimit", qint64(1));
    QVERIFY(QFile::exists(output));
    cache.setQualityLimit({128, 72});
    QTRY_VERIFY_WITH_TIMEOUT(!cache.readyPath(source).isEmpty(), 5000);
    const auto smaller = cache.readyPath(source);
    QVERIFY(smaller != output);
    QCOMPARE(streams(smaller).first().toObject().value("width").toInt(), 128);
    QString error;
    const auto second = directory.filePath("second");
    QVERIFY2(cache.setCacheDirectory(second, &error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!cache.readyPath(source).isEmpty(), 5000);
    QVERIFY(cache.readyPath(source).startsWith(second + '/'));
    QCOMPARE(streams(cache.readyPath(source)).first().toObject().value("width").toInt(), 128);
    const auto invalid = directory.filePath("file");
    QFile file(invalid);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QVERIFY(!cache.setCacheDirectory(invalid, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(cache.cacheDirectory(), second);
}

void PreviewCacheTest::generatesExactFramesAndReusesCache() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QFINDTESTDATA("fixtures/rotation.mp4");
    PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
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

    PreviewCache fresh(directory.path(), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
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

void PreviewCacheTest::unwritableCacheFailsOnce() {
    QTemporaryDir directory;
    const auto path = directory.filePath("not-a-folder");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
    PreviewCache cache(path, nullptr, Performance::Budget{});
    QSignalSpy failed(&cache, &PreviewCache::failed);
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
    QVERIFY(!cache.isBusy());
    QVERIFY(failed.first().at(1).toString().contains("folder"));
    cache.request(source, 60);
    QTest::qWait(150);
    QCOMPARE(failed.count(), 1);
    QVERIFY(!cache.isBusy());
}

void PreviewCacheTest::missingSourceFailsOnce() {
    QTemporaryDir directory;
    PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
    const QString output = cache.readyPath(source);
    QFile corrupted(output);
    QVERIFY(corrupted.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(corrupted.write("not a video") > 0);
    corrupted.close();

    PreviewCache fresh(directory.path(), nullptr, Performance::Budget{});
    QSignalSpy reused(&fresh, &PreviewCache::ready);
    fresh.request(source, 60);
    fresh.discard(source);
    QVERIFY(QFile::exists(output)); // Another instance still retains its working copy.
    cache.retainSources({});
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
    QVERIFY(QFile::exists(output)); // An external replacement without our ownership record is never deleted.
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
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
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
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
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

void PreviewCacheTest::runsBoundedConcurrentJobsAndCancelsAll() {
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
    const auto fixture = QFINDTESTDATA("fixtures/rotation.mp4");
    QStringList sources;
    for (int i = 0; i < 3; ++i) {
        const auto source = directory.filePath(QString("source%1.mp4").arg(i));
        QVERIFY(QFile::copy(fixture, source));
        sources.append(source);
    }
    Performance::Budget budget;
    budget.proxyJobs = 2;
    budget.backgroundThreads = 2;
    PreviewCache cache(directory.filePath("cache"), nullptr, budget);
    QSignalSpy ready(&cache, &PreviewCache::ready), failed(&cache, &PreviewCache::failed);
    cache.setRequests({{sources[0], 60, 0, 60}, {sources[1], 60, 0, 60}, {sources[2], 60, 0, 60}});
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
    cache.setRequests({});
    QTRY_VERIFY_WITH_TIMEOUT(!cache.isBusy(), 1000);
    QCOMPARE(running(), 0);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(QDir(directory.filePath("cache")).entryList({"*.part.mov"}, QDir::Files).size(), 0);
    qputenv("PATH", previousPath);
    cache.setRequests({{sources[0], 60, 0, 60}, {sources[1], 60, 0, 60}});
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 5000);
    QVERIFY(!cache.readyPath(sources[0]).isEmpty());
    QVERIFY(!cache.readyPath(sources[1]).isEmpty());
#endif
}

void PreviewCacheTest::hardwareFailureRetriesSoftwareOnce() {
#ifdef Q_OS_WIN
    QSKIP("The failing-encoder fixture uses a POSIX shell.");
#else
    QTemporaryDir directory;
    const auto realFfmpeg = executable("ffmpeg");
    const auto log = directory.filePath("attempts");
    QFile script(directory.filePath("ffmpeg"));
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/bin/sh\nfor arg in \"$@\"; do\ncase \"$arg\" in h264_*) echo hardware >> \"" + log.toUtf8() + "\"; echo 'hardware initialization failed' >&2; exit 1;; esac\ndone\necho software >> \"" + log.toUtf8() + "\"\nexec \"" + realFfmpeg.toUtf8() + "\" \"$@\"\n");
    script.close();
    QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto previousPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", directory.path().toUtf8());
    const auto fixture = QFINDTESTDATA("fixtures/rotation.mp4");
    const auto second = directory.filePath("second.mp4");
    QVERIFY(QFile::copy(fixture, second));
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
    QSignalSpy ready(&cache, &PreviewCache::ready), failed(&cache, &PreviewCache::failed);
    cache.request(fixture, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
    const auto originalProxy = cache.readyPath(fixture);
    cache.request(second, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 5000);
    QFile attempts(log);
    QVERIFY(attempts.open(QIODevice::ReadOnly));
    const auto bytes = attempts.readAll();
    QCOMPARE(bytes.count("hardware"), 1);
    QCOMPARE(bytes.count("software"), 2);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(cache.readyPath(fixture), originalProxy);
    QCOMPARE(streams(originalProxy).first().toObject().value("nb_read_frames").toString(), QString("60"));
    QCOMPARE(QDir(directory.filePath("cache")).entryList({"*.part.mov"}, QDir::Files).size(), 0);
#endif
}

void PreviewCacheTest::configuredDimensionsHaveSeparateCopies() {
    QTemporaryDir directory;
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    Performance::Budget budget;
    budget.previewWidth = 128;
    budget.previewHeight = 72;
    PreviewCache small(directory.path(), nullptr, budget);
    small.request(source, 60);
    QTRY_VERIFY_WITH_TIMEOUT(!small.readyPath(source).isEmpty(), 5000);
    const auto first = small.readyPath(source);
    QCOMPARE(streams(first).first().toObject().value("width").toInt(), 128);
    QCOMPARE(streams(first).first().toObject().value("height").toInt(), 72);
    budget.previewWidth = 160;
    budget.previewHeight = 90;
    PreviewCache large(directory.path(), nullptr, budget);
    large.request(source, 60);
    QTRY_VERIFY_WITH_TIMEOUT(!large.readyPath(source).isEmpty(), 5000);
    QVERIFY(large.readyPath(source) != first);
    QCOMPARE(streams(large.readyPath(source)).first().toObject().value("width").toInt(), 160);
    QCOMPARE(streams(large.readyPath(source)).first().toObject().value("height").toInt(), 90);
}

void PreviewCacheTest::sourceReplacementRetiresBothWorkers() {
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
    const auto a = directory.filePath("a.mp4"), b = directory.filePath("b.mp4");
    QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/rotation.mp4"), a));
    QVERIFY(QFile::copy(a, b));
    Performance::Budget budget;
    budget.proxyJobs = 2;
    PreviewCache cache(directory.filePath("cache"), nullptr, budget);
    QSignalSpy ready(&cache, &PreviewCache::ready), failed(&cache, &PreviewCache::failed);
    cache.setRequests({{a, 60, 0, 60}, {b, 60, 0, 60}});
    QVERIFY(QFile::remove(a));
    QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/proxy-timing-30.mp4"), a));
    qputenv("PATH", previousPath);
    cache.setRequests({{a, 60, 0, 60}});
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.isBusy(), 1000);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(ready.first().first().toString(), a);
    QVERIFY(cache.readyPath(b).isEmpty());
    const auto video = streams(cache.readyPath(a)).first().toObject();
    QCOMPARE(video.value("width").toInt(), 128);
    QCOMPARE(video.value("r_frame_rate").toString(), QString("30/1"));
    QCOMPARE(QDir(directory.filePath("cache")).entryList({"*.part.mov"}, QDir::Files).size(), 0);
#endif
}

void PreviewCacheTest::hardwareCrashAndBadSourcePreserveReadyCopy() {
#ifdef Q_OS_WIN
    QSKIP("The failing-encoder fixture uses a POSIX shell.");
#else
    QTemporaryDir directory;
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    PreviewCache prepare(directory.filePath("cache"), nullptr, Performance::Budget{});
    prepare.request(source, 60);
    QTRY_VERIFY_WITH_TIMEOUT(!prepare.readyPath(source).isEmpty(), 5000);
    PreviewCache cache(directory.filePath("cache"), nullptr, Performance::Budget{});
    QSignalSpy ready(&cache, &PreviewCache::ready), failed(&cache, &PreviewCache::failed);
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
    const auto output = cache.readyPath(source);
    QFile original(output);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const auto bytes = original.readAll();
    original.close();
    const auto log = directory.filePath("attempts");
    QFile script(directory.filePath("ffmpeg"));
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/bin/sh\nfor arg in \"$@\"; do case \"$arg\" in h264_*) hardware=yes;; esac; output=\"$arg\"; done\necho attempt >> \"" + log.toUtf8() + "\"\nprintf incomplete > \"$output\"\nif [ \"$hardware\" = yes ]; then kill -KILL $$; fi\nexit 1\n");
    script.close();
    QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto previousPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", directory.path().toUtf8());
    const auto bad = directory.filePath("bad.mp4");
    QFile invalid(bad);
    QVERIFY(invalid.open(QIODevice::WriteOnly));
    QVERIFY(invalid.write("invalid video") > 0);
    invalid.close();
    cache.request(bad, 60);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.isBusy(), 1000);
    QFile attempts(log);
    QVERIFY(attempts.open(QIODevice::ReadOnly));
    const int tries = attempts.readAll().count("attempt");
    QCOMPARE(tries, 2);
    cache.request(bad, 60);
    QTest::qWait(50);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(ready.count(), 1);
    QCOMPARE(cache.readyPath(source), output);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), bytes);
    QVERIFY(cache.readyPath(bad).isEmpty());
    QCOMPARE(QDir(directory.filePath("cache")).entryList({"*.part.mov"}, QDir::Files).size(), 0);
#endif
}

void PreviewCacheTest::pausingFullPoolKeepsQueueBounded() {
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
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    Performance::Budget budget;
    budget.proxyJobs = 2;
    PreviewCache cache(directory.filePath("cache"), nullptr, budget);
    QSignalSpy failed(&cache, &PreviewCache::failed);
    for (int frames = 60; frames < 190; ++frames) cache.request(source, frames);
    cache.setPaused(true);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("#!/bin/sh\nexit 1\n");
    script.close();
    cache.setPaused(false);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.isBusy(), 10000);
    QCOMPARE(failed.count(), 128);
    QCOMPARE(QDir(directory.filePath("cache")).entryList({"*.part.mov"}, QDir::Files).size(), 0);
#endif
}

void PreviewCacheTest::concurrentRangeProbesKeepIndependentTiming() {
    QTemporaryDir directory;
    const auto a = directory.filePath("30fps.mp4"), b = directory.filePath("2997fps.mp4");
    run("ffmpeg", {"-v", "error", "-y", "-stream_loop", "63", "-i", QFINDTESTDATA("fixtures/proxy-timing-30.mp4"), "-c", "copy", a});
    run("ffmpeg", {"-v", "error", "-y", "-stream_loop", "3", "-i", QFINDTESTDATA("fixtures/proxy-timing-2997.mp4"), "-c", "copy", b});
    const int aFrames = qRound(Media::mediaDurationSeconds(a) * 60), bFrames = qRound(Media::mediaDurationSeconds(b) * 60);
    QVERIFY(aFrames > 1440 && bFrames > 1440);
    Performance::Budget budget;
    budget.proxyJobs = 2;
    PreviewCache cache(directory.filePath("cache"), nullptr, budget);
    QSignalSpy ready(&cache, &PreviewCache::ready), failed(&cache, &PreviewCache::failed);
    cache.setRequests({{a, aFrames, 1300, 1380}, {b, bFrames, 1300, 1380}});
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QCOMPARE(failed.count(), 0);
    Mlt::Profile profile;
    Media::configureOutputProfile(profile, 128, 72);
    for (const auto &source : {a, b}) {
        const auto copy = cache.readyCopy(source, 1300, 1380);
        QVERIFY(!copy.path.isEmpty());
        QVERIFY(copy.startFrame > 0 && copy.startFrame <= 1300);
        Project originalProject, proxyProject;
        originalProject.clips = {{source, 1300 / 60.0, 1380 / 60.0}};
        proxyProject.clips = {{copy.path, (1300 - copy.startFrame) / 60.0, (1380 - copy.startFrame) / 60.0}};
        auto original = Media::buildTimeline(originalProject, profile), proxy = Media::buildTimeline(proxyProject, profile);
        QVERIFY(original && proxy);
        for (int frame = 0; frame < 80; ++frame) QCOMPARE(frameId(*proxy, frame), frameId(*original, frame));
    }
    QCOMPARE(QDir(directory.filePath("cache")).entryList({"*.part.mov"}, QDir::Files).size(), 0);
}

void PreviewCacheTest::retiredRequestsCannotDeliverQueuedReady_data() {
    QTest::addColumn<bool>("retain");
    QTest::newRow("set-requests") << false;
    QTest::newRow("retain-sources") << true;
}

void PreviewCacheTest::retiredRequestsCannotDeliverQueuedReady() {
    QFETCH(bool, retain);
    QTemporaryDir directory;
    const auto source = QFINDTESTDATA("fixtures/rotation.mp4");
    PreviewCache prepare(directory.path(), nullptr, Performance::Budget{});
    prepare.request(source, 60);
    QTRY_VERIFY_WITH_TIMEOUT(!prepare.readyPath(source).isEmpty(), 5000);
    PreviewCache cache(directory.path(), nullptr, Performance::Budget{});
    QSignalSpy ready(&cache, &PreviewCache::ready);
    cache.request(source, 60);
    if (retain) cache.retainSources({});
    else cache.setRequests({});
    QTest::qWait(50);
    QCOMPARE(ready.count(), 0);
    cache.request(source, 60);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 1000);
}

QTEST_GUILESS_MAIN(PreviewCacheTest)
#include "preview_cache_test.moc"
