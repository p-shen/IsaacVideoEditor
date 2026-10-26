#include "project.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

class ProjectTest : public QObject {
    Q_OBJECT
private slots:
    void durationAndCrossfade();
    void shortMiddleClipClampsCrossfade();
    void rejectsBadTrim();
    void savesAndLoads();
    void loadsLegacyCrossfadeAtSameDuration();
    void reordersClipsByDraggedRowOrder();
    void splitsTrimmedSourceIntoContiguousPieces();
    void rejectsSplitAtEdgesAndInsideFades();
    void splitPreservesOtherFadesAndTiming();
    void hardCutsDoNotClampUnrelatedFades();
    void splitRoundTripsAsStraightCut();
    void loadsVersionTwoCrossfades();
    void rejectsMalformedHardCutSetting();
    void rotationSettingsRoundTrip();
    void rotationsComposeAndWrap();
    void rotationPreservesTimingAndSplitPieces();
    void loadsVersionThreeWithoutRotation();
    void rejectsMalformedRotationSettings();
    void rejectsInvalidRotationInMemory();
};

void ProjectTest::durationAndCrossfade() {
    QCOMPARE(Project::fps, 60);
    Project p;
    p.clips = {{"a.mp4", 0, 2}, {"b.mp4", 1, 4}};
    p.crossfadeFrames = 30;
    QCOMPARE(p.effectiveCrossfadeFrames(), 30);
    QCOMPARE(p.durationFrames(), 270);
}

void ProjectTest::shortMiddleClipClampsCrossfade() {
    Project p;
    p.clips = {{"a.mp4", 0, 3}, {"b.mp4", 0, 1}, {"c.mp4", 0, 3}};
    p.crossfadeFrames = 48;
    QCOMPARE(p.effectiveCrossfadeFrames(), 29);
    QCOMPARE(p.durationFrames(), 362);
}

void ProjectTest::rejectsBadTrim() {
    Project p;
    p.clips = {{"a.mp4", 2, 1}};
    QVERIFY(!p.validate().isEmpty());
}

void ProjectTest::savesAndLoads() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Project p;
    p.clips = {{"/tmp/one.mp4", 0.5, 3.5}};
    p.crossfadeFrames = 12;
    p.voicePath = "/tmp/voice.m4a";
    p.voiceStartSeconds = 1.25;
    p.musicPath = "/tmp/music.mp3";
    p.musicDb = -24;
    const auto file = dir.filePath("vlog.isaac.json");
    QString error;
    QVERIFY2(saveProject(p, file, &error), qPrintable(error));
    QFile saved(file);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto data = QJsonDocument::fromJson(saved.readAll()).object();
    QCOMPARE(data.value("version").toInt(), 7);
    QCOMPARE(data.value("crossfadeSeconds").toDouble(), 0.2);
    QVERIFY(!data.contains("crossfadeFrames"));
    QVERIFY(!data.value("clips").toArray().first().toObject().contains("rotationDegrees"));
    auto loaded = loadProject(file, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->clips.size(), 1);
    QCOMPARE(loaded->clips[0].inSeconds, 0.5);
    QCOMPARE(loaded->clips[0].outSeconds, 3.5);
    QCOMPARE(loaded->clips[0].rotationDegrees, 0);
    QCOMPARE(loaded->crossfadeFrames, 12);
    QCOMPARE(loaded->voicePath, p.voicePath);
    QCOMPARE(loaded->voiceStartSeconds, p.voiceStartSeconds);
    QCOMPARE(loaded->musicPath, p.musicPath);
    QCOMPARE(loaded->musicDb, p.musicDb);
}

void ProjectTest::loadsLegacyCrossfadeAtSameDuration() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("legacy.isaac.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray legacy = R"({
        "version": 1,
        "clips": [{"path":"a.mp4","in":0,"out":2},
                  {"path":"b.mp4","in":0,"out":2}],
        "crossfadeFrames": 15
    })";
    QCOMPARE(file.write(legacy), qint64(legacy.size()));
    file.close();
    QString error;
    const auto loaded = loadProject(path, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->crossfadeFrames, 30);
    QCOMPARE(double(loaded->crossfadeFrames) / Project::fps, 0.5);
    QCOMPARE(loaded->durationFrames(), 210);
}

void ProjectTest::reordersClipsByDraggedRowOrder() {
    Project p;
    p.clips = {{"first.mp4", 0, 1}, {"second.mp4", 0, 1}, {"third.mp4", 0, 1}};
    QVERIFY(p.reorderClips({2, 0, 1}));
    QCOMPARE(p.clips[0].path, QString("third.mp4"));
    QCOMPARE(p.clips[1].path, QString("first.mp4"));
    QCOMPARE(p.clips[2].path, QString("second.mp4"));
    QVERIFY(!p.reorderClips({0, 0, 2}));
    QCOMPARE(p.clips[0].path, QString("third.mp4"));
}

void ProjectTest::splitsTrimmedSourceIntoContiguousPieces() {
    Project p;
    p.clips = {{"source.mp4", 1.5, 5.5}};
    const int duration = p.durationFrames();
    QVERIFY(p.splitClip(0, 90));
    QCOMPARE(p.clips.size(), 2);
    QCOMPARE(p.clips[0].path, QString("source.mp4"));
    QCOMPARE(p.clips[1].path, p.clips[0].path);
    QCOMPARE(p.clips[0].inSeconds, 1.5);
    QCOMPARE(p.clips[0].outSeconds, 3.0);
    QCOMPARE(p.clips[1].inSeconds, 3.0);
    QCOMPARE(p.clips[1].outSeconds, 5.5);
    QVERIFY(!p.clips[0].hardCutBefore);
    QVERIFY(p.clips[1].hardCutBefore);
    QCOMPARE(p.clipStartFrame(1), 90);
    QCOMPARE(p.durationFrames(), duration);
    QVERIFY(p.splitClip(1, 60));
    QCOMPARE(p.clips.size(), 3);
    QCOMPARE(p.clips[1].outSeconds, 4.0);
    QCOMPARE(p.clips[2].inSeconds, 4.0);
    QCOMPARE(p.clips[2].outSeconds, 5.5);
    QCOMPARE(p.clipStartFrame(2), 150);
    QCOMPARE(p.durationFrames(), duration);
    QVERIFY(p.validate().isEmpty());
}

void ProjectTest::rejectsSplitAtEdgesAndInsideFades() {
    Project p;
    p.clips = {{"a.mp4", 0, 5}, {"b.mp4", 1, 7}, {"c.mp4", 0, 5}};
    p.crossfadeFrames = 30;
    QVERIFY(!p.splitClip(-1, 60));
    QVERIFY(!p.splitClip(3, 60));
    QVERIFY(!p.splitClip(1, 0));
    QVERIFY(!p.splitClip(1, 360));
    QVERIFY(!p.splitClip(1, 15));
    QVERIFY(!p.splitClip(1, 30));
    QVERIFY(!p.splitClip(1, 330));
    QVERIFY(!p.splitClip(1, 345));
    QCOMPARE(p.clips.size(), 3);
    QCOMPARE(p.durationFrames(), 900);
    QVERIFY(p.splitClip(1, 31));
    QCOMPARE(p.durationFrames(), 900);
}

void ProjectTest::splitPreservesOtherFadesAndTiming() {
    Project p;
    p.clips = {{"a.mp4", 0, 5}, {"b.mp4", 1, 7}, {"c.mp4", 0, 5}};
    p.crossfadeFrames = 30;
    const int duration = p.durationFrames();
    const int thirdStart = p.clipStartFrame(2);
    QVERIFY(p.splitClip(1, 120));
    QCOMPARE(p.effectiveCrossfadeFrames(), 30);
    QCOMPARE(p.transitionFramesBefore(1), 30);
    QCOMPARE(p.transitionFramesBefore(2), 0);
    QCOMPARE(p.transitionFramesBefore(3), 30);
    QCOMPARE(p.clipStartFrame(1), 270);
    QCOMPARE(p.clipStartFrame(2), 390);
    QCOMPARE(p.clipStartFrame(3), thirdStart);
    QCOMPARE(p.durationFrames(), duration);
    QVERIFY(p.splitClip(2, 180));
    QCOMPARE(p.clipStartFrame(4), thirdStart);
    QCOMPARE(p.durationFrames(), duration);
}

void ProjectTest::hardCutsDoNotClampUnrelatedFades() {
    Project p;
    p.clips = {{"a.mp4", 0, 3}, {"tiny.mp4", 0, 0.1, true},
               {"c.mp4", 0, 3, true}, {"d.mp4", 0, 3}};
    p.crossfadeFrames = 48;
    QCOMPARE(p.effectiveCrossfadeFrames(), 48);
    QCOMPARE(p.durationFrames(), 498);
    QCOMPARE(p.transitionFramesBefore(0), 0);
    QCOMPARE(p.transitionFramesBefore(1), 0);
    QCOMPARE(p.transitionFramesBefore(2), 0);
    QCOMPARE(p.transitionFramesBefore(3), 48);
    QCOMPARE(p.transitionFramesBefore(4), 0);
    QCOMPARE(p.clipStartFrame(-1), -1);
    QCOMPARE(p.clipStartFrame(4), -1);
}

void ProjectTest::splitRoundTripsAsStraightCut() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Project p;
    p.clips = {{"source.mp4", 1, 5}};
    p.crossfadeFrames = 30;
    QVERIFY(p.splitClip(0, 90));
    QString error;
    const auto file = dir.filePath("split.isaac.json");
    QVERIFY2(saveProject(p, file, &error), qPrintable(error));
    auto loaded = loadProject(file, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->clips.size(), 2);
    QVERIFY(loaded->clips[1].hardCutBefore);
    QCOMPARE(loaded->transitionFramesBefore(1), 0);
    QCOMPARE(loaded->clips[0].outSeconds, loaded->clips[1].inSeconds);
    QCOMPARE(loaded->durationFrames(), 240);
}

void ProjectTest::loadsVersionTwoCrossfades() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath("v2.isaac.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray data = R"({"version":2,"clips":[{"path":"a.mp4","in":0,"out":2},
        {"path":"b.mp4","in":0,"out":2}],"crossfadeSeconds":0.25})";
    QCOMPARE(file.write(data), qint64(data.size()));
    file.close();
    const auto loaded = loadProject(file.fileName());
    QVERIFY(loaded.has_value());
    QVERIFY(!loaded->clips[1].hardCutBefore);
    QCOMPARE(loaded->crossfadeFrames, 15);
    QCOMPARE(loaded->durationFrames(), 225);
}

void ProjectTest::rejectsMalformedHardCutSetting() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath("invalid.isaac.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray data = R"({"version":3,"clips":[{"path":"a.mp4","in":0,"out":2,
        "hardCutBefore":"yes"}]})";
    QCOMPARE(file.write(data), qint64(data.size()));
    file.close();
    QString error;
    QVERIFY(!loadProject(file.fileName(), &error).has_value());
    QVERIFY(!error.isEmpty());
}

void ProjectTest::rotationSettingsRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath("rotated.isaac.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray data = R"({"version":4,"clips":[{"path":"a.mp4","in":0.5,"out":2,
        "hardCutBefore":true,"rotationDegrees":90}]})";
    QCOMPARE(file.write(data), qint64(data.size()));
    file.close();
    QString error;
    const auto loaded = loadProject(file.fileName(), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QVERIFY2(saveProject(*loaded, file.fileName(), &error), qPrintable(error));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto saved = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(saved.value("version").toInt(), 7);
    const auto clip = saved.value("clips").toArray().first().toObject();
    QCOMPARE(clip.value("rotationDegrees").toInt(), 90);
    QVERIFY(clip.value("hardCutBefore").toBool());
    QCOMPARE(clip.value("in").toDouble(), 0.5);
    QCOMPARE(clip.value("out").toDouble(), 2.0);
}

void ProjectTest::rotationsComposeAndWrap() {
    Project p;
    p.clips = {{"source.mp4", 0, 2}};
    QVERIFY(p.rotateClip(0, -90));
    QCOMPARE(p.clips[0].rotationDegrees, 270);
    QVERIFY(p.rotateClip(0, 90));
    QCOMPARE(p.clips[0].rotationDegrees, 0);
    QVERIFY(p.rotateClip(0, 180));
    QCOMPARE(p.clips[0].rotationDegrees, 180);
    QVERIFY(p.rotateClip(0, 180));
    QCOMPARE(p.clips[0].rotationDegrees, 0);
    for (int i = 0; i < 4; ++i) QVERIFY(p.rotateClip(0, 90));
    QCOMPARE(p.clips[0].rotationDegrees, 0);
    QVERIFY(!p.rotateClip(0, 0));
    QVERIFY(!p.rotateClip(0, 360));
    QVERIFY(!p.rotateClip(0, -720));
    QVERIFY(!p.rotateClip(0, 45));
    QVERIFY(!p.rotateClip(-1, 90));
    QVERIFY(!p.rotateClip(1, 90));
    QVERIFY(!p.rotateClip(0, std::numeric_limits<int>::min()));
    QVERIFY(!p.rotateClip(0, std::numeric_limits<int>::max()));
    QVERIFY(p.rotateClip(0, std::numeric_limits<int>::max() - 37));
    QCOMPARE(p.clips[0].rotationDegrees, 90);
    QVERIFY(p.rotateClip(0, std::numeric_limits<int>::min() + 38));
    QCOMPARE(p.clips[0].rotationDegrees, 0);
}

void ProjectTest::rotationPreservesTimingAndSplitPieces() {
    Project p;
    p.clips = {{"source.mp4", 1.5, 5.5, true, 270}, {"next.mp4", 0, 2, false, 90}};
    p.crossfadeFrames = 30;
    const int duration = p.durationFrames();
    const int secondStart = p.clipStartFrame(1);
    QVERIFY(p.rotateClip(0, -90));
    QCOMPARE(p.clips[0].rotationDegrees, 180);
    QCOMPARE(p.clips[1].rotationDegrees, 90);
    QCOMPARE(p.clips[0].path, QString("source.mp4"));
    QCOMPARE(p.clips[0].inSeconds, 1.5);
    QCOMPARE(p.clips[0].outSeconds, 5.5);
    QVERIFY(p.clips[0].hardCutBefore);
    QCOMPARE(p.durationFrames(), duration);
    QCOMPARE(p.clipStartFrame(1), secondStart);
    QVERIFY(p.splitClip(0, 90));
    QCOMPARE(p.clips[0].rotationDegrees, 180);
    QCOMPARE(p.clips[1].rotationDegrees, 180);
    QCOMPARE(p.clips[2].rotationDegrees, 90);
    QVERIFY(p.clips[1].hardCutBefore);
    QCOMPARE(p.clips[0].outSeconds, p.clips[1].inSeconds);
    QCOMPARE(p.durationFrames(), duration);
    QCOMPARE(p.clipStartFrame(2), secondStart);
}

void ProjectTest::loadsVersionThreeWithoutRotation() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath("v3.isaac.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray data = R"({"version":3,"clips":[{"path":"a.mp4","in":0,"out":2},
        {"path":"b.mp4","in":0,"out":2,"hardCutBefore":true}],"crossfadeSeconds":0.25})";
    QCOMPARE(file.write(data), qint64(data.size()));
    file.close();
    const auto loaded = loadProject(file.fileName());
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->clips[0].rotationDegrees, 0);
    QCOMPARE(loaded->clips[1].rotationDegrees, 0);
    QVERIFY(loaded->clips[1].hardCutBefore);
    QCOMPARE(loaded->durationFrames(), 240);
}

void ProjectTest::rejectsMalformedRotationSettings() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const QByteArray &value : {QByteArray("\"90\""), QByteArray("true"), QByteArray("null"),
                                  QByteArray("[]"), QByteArray("{}"), QByteArray("45"),
                                  QByteArray("360"), QByteArray("-90"), QByteArray("90.5"),
                                  QByteArray("1e100")}) {
        QFile file(dir.filePath("invalid-rotation.isaac.json"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray data = QByteArray(R"({"version":4,"clips":[{"path":"a.mp4","in":0,"out":2,"rotationDegrees":)")
            + value + "}]}";
        QVERIFY(!QJsonDocument::fromJson(data).isNull());
        QCOMPARE(file.write(data), qint64(data.size()));
        file.close();
        QString error;
        QVERIFY2(!loadProject(file.fileName(), &error).has_value(), value.constData());
        QVERIFY(!error.isEmpty());
    }
}

void ProjectTest::rejectsInvalidRotationInMemory() {
    Project p;
    p.clips = {{"source.mp4", 0, 2}};
    for (int degrees : {-90, 45, 360}) {
        p.clips[0].rotationDegrees = degrees;
        QVERIFY(!p.validate().isEmpty());
        QVERIFY(!p.rotateClip(0, 90));
    }
    for (int degrees : {0, 90, 180, 270}) {
        p.clips[0].rotationDegrees = degrees;
        QVERIFY(p.validate().isEmpty());
    }
}

QTEST_GUILESS_MAIN(ProjectTest)
#include "project_test.moc"
