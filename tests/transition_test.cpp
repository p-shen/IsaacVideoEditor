#include "editor_controller.h"
#include "mlt_engine.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <array>

namespace {
const QStringList effects{
    "wipe-left", "wipe-right", "wipe-up", "wipe-down",
    "diagonal-down-right", "diagonal-up-left", "circle-open", "circle-close",
    "barn-doors-horizontal", "barn-doors-vertical", "blinds-horizontal", "blinds-vertical"
};
QImage imageAt(Mlt::Producer &timeline, int position, QSize size = {160, 90}) {
    timeline.seek(position);
    std::unique_ptr<Mlt::Frame> frame(timeline.get_frame());
    if (!frame || !frame->is_valid()) return {};
    auto format = mlt_image_rgba;
    int width = size.width(), height = size.height();
    uint8_t *pixels = nullptr;
    if (mlt_frame_get_image(frame->get_frame(), &pixels, &format, &width, &height, 0) || !pixels) return {};
    return QImage(pixels, width, height, width * 4, QImage::Format_RGBA8888).copy();
}
char colorAt(const QImage &image, QPoint point) {
    const auto c = image.pixelColor(point);
    if (c.red() > 180 && c.blue() < 70) return 'R';
    if (c.blue() > 180 && c.red() < 70) return 'B';
    return '?';
}
}

class TransitionTest : public QObject {
    Q_OBJECT
    QTemporaryDir media;
    QString red, blue;
    Project project() const {
        Project p;
        p.clips = {{red, 0, 1}, {blue, 0, 1}};
        return p;
    }
private slots:
    void initTestCase() {
        Media::initializeMlt();
        red = media.filePath("red.mp4"); blue = media.filePath("blue.mp4");
        for (const auto &entry : {qMakePair(QString("red"), red), qMakePair(QString("blue"), blue)}) {
            QProcess ffmpeg;
            ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
                "color=c=" + entry.first + ":s=160x90:r=60:d=1", "-f", "lavfi", "-i",
                "sine=frequency=440:sample_rate=48000:duration=1", "-c:v", "libx264", "-c:a", "aac", "-shortest", "-y", entry.second});
            QVERIFY(ffmpeg.waitForFinished(10000)); QCOMPARE(ffmpeg.exitCode(), 0);
        }
    }
    void presetsRoundTrip_data() {
        QTest::addColumn<QString>("effect");
        for (const auto &effect : effects) QTest::newRow(qPrintable(effect)) << effect;
    }
    void presetsRoundTrip() {
        QFETCH(QString, effect);
        auto p = project(); QVERIFY(p.setTransition(1, effect, 30));
        QCOMPARE(p.transitionFramesBefore(1), 30); QCOMPARE(p.durationFrames(), 90);
        QString error; const auto path = media.filePath("round-trip.json");
        QVERIFY2(saveProject(p, path, &error), qPrintable(error));
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->clips[1].transitionType, effect); QCOMPARE(loaded->transitionFramesBefore(1), 30);
    }
    void changingOnlyEffectCommitsAndUndoes() {
        EditorController editor; QVERIFY(editor.importFiles({red, blue}));
        editor.setTransition(1, "dissolve", 30); editor.setTransition(1, "wipe-left", 30);
        QCOMPARE(editor.project().clips[1].transitionType, "wipe-left");
        editor.selectTransition(1); QCOMPARE(editor.currentTransition()["type"].toString(), "wipe-left");
        const int duration = editor.duration();
        editor.setTransition(1, "circle-open", 30);
        QCOMPARE(editor.currentTransition()["type"].toString(), "circle-open"); QCOMPARE(editor.duration(), duration);
        editor.undo(); QCOMPARE(editor.project().clips[1].transitionType, "wipe-left");
        editor.redo(); QCOMPARE(editor.project().clips[1].transitionType, "circle-open");
    }
    void invalidEffectAndShortClips() {
        auto p = project(); QVERIFY(!p.setTransition(1, "made-up", 30));
        QVERIFY(p.setTransition(1, "wipe-right", 600)); QCOMPARE(p.transitionFramesBefore(1), 59);
        QVERIFY(p.setTransition(1, "circle-close", 600)); QCOMPARE(p.transitionFramesBefore(1), 59);
        QVERIFY(!p.setTransition(1, "circle-close", 600));
        p.clips[1].transitionType = "made-up"; QVERIFY(!p.validate().isEmpty());
        auto valid = project(); QVERIFY(valid.setTransition(1, "dissolve", 30));
        const auto path = media.filePath("invalid.json"); QVERIFY(saveProject(valid, path));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        auto json = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        auto clips = json["clips"].toArray(); auto clip = clips[1].toObject();
        clip["transitionType"] = "made-up"; clips[1] = clip; json["clips"] = clips;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write(QJsonDocument(json).toJson()); file.close();
        QVERIFY(!loadProject(path));
    }
    void renderedMasks_data() {
        QTest::addColumn<QString>("effect"); QTest::addColumn<QPoint>("incoming"); QTest::addColumn<QPoint>("outgoing");
        QTest::newRow("left") << QString("wipe-left") << QPoint(140,45) << QPoint(20,45);
        QTest::newRow("right") << QString("wipe-right") << QPoint(20,45) << QPoint(140,45);
        QTest::newRow("up") << QString("wipe-up") << QPoint(80,75) << QPoint(80,15);
        QTest::newRow("down") << QString("wipe-down") << QPoint(80,15) << QPoint(80,75);
        QTest::newRow("diagonal-down") << QString("diagonal-down-right") << QPoint(20,15) << QPoint(140,75);
        QTest::newRow("diagonal-up") << QString("diagonal-up-left") << QPoint(140,75) << QPoint(20,15);
        QTest::newRow("circle-open") << QString("circle-open") << QPoint(80,45) << QPoint(150,80);
        QTest::newRow("circle-close") << QString("circle-close") << QPoint(150,80) << QPoint(80,45);
        QTest::newRow("doors-horizontal") << QString("barn-doors-horizontal") << QPoint(80,15) << QPoint(10,45);
        QTest::newRow("doors-vertical") << QString("barn-doors-vertical") << QPoint(20,45) << QPoint(80,5);
        QTest::newRow("blinds-horizontal") << QString("blinds-horizontal") << QPoint(80,2) << QPoint(80,9);
        QTest::newRow("blinds-vertical") << QString("blinds-vertical") << QPoint(3,45) << QPoint(17,45);
    }
    void renderedMasks() {
        QFETCH(QString, effect); QFETCH(QPoint, incoming); QFETCH(QPoint, outgoing);
        auto p = project(); QVERIFY(p.setTransition(1, effect, 30));
        Mlt::Profile profile; Media::configureOutputProfile(profile, 160, 90);
        QString error; auto timeline = Media::buildTimeline(p, profile, &error); QVERIFY2(timeline, qPrintable(error));
        QCOMPARE(timeline->get_playtime(), 90);
        const auto midpoint = imageAt(*timeline, 45); QVERIFY(!midpoint.isNull());
        QCOMPARE(colorAt(midpoint, incoming), 'B'); QCOMPARE(colorAt(midpoint, outgoing), 'R');
        // Seek out of order: each effect still runs from the outgoing to the incoming clip.
        QCOMPARE(colorAt(imageAt(*timeline, 65), {80,45}), 'B');
        QCOMPARE(colorAt(imageAt(*timeline, 20), {80,45}), 'R');
        QCOMPARE(colorAt(imageAt(*timeline, 45), incoming), 'B');
        const auto captures = qEnvironmentVariable("ISAAC_TRANSITION_SCREENSHOTS");
        if (!captures.isEmpty()) { QDir().mkpath(captures); QVERIFY(midpoint.save(captures + "/" + effect + ".png")); }
    }
    void exportedMasks_data() { renderedMasks_data(); }
    void exportedMasks() {
        QFETCH(QString, effect); QFETCH(QPoint, incoming); QFETCH(QPoint, outgoing);
        auto p = project(); QVERIFY(p.setTransition(1, effect, 30));
        QString error; const auto output = media.filePath(effect + ".mp4");
        QVERIFY2(Media::exportMp4(p, output, {}, &error, {}, {}, true), qPrintable(error));
        const auto info = Media::mediaInfo(output, &error); QVERIFY2(info, qPrintable(error)); QVERIFY(info->hasAudio);
        Mlt::Profile profile; Media::configureOutputProfile(profile,160,90);
        Mlt::Producer encoded(profile, QFile::encodeName(output).constData()); QVERIFY(encoded.is_valid());
        // AAC padding can extend the container; count decoded video frames independently.
        QProcess probe;probe.start("ffprobe",{"-v","error","-select_streams","v:0","-count_frames",
            "-show_entries","stream=nb_read_frames","-of","csv=p=0",output});
        QVERIFY(probe.waitForFinished(10000));QCOMPARE(probe.exitCode(),0);
        QCOMPARE(probe.readAllStandardOutput().trimmed().toInt(),90);
        const auto midpoint = imageAt(encoded, 45); QVERIFY(!midpoint.isNull());
        QCOMPARE(colorAt(midpoint, incoming), 'B'); QCOMPARE(colorAt(midpoint, outgoing), 'R');
        encoded.set_speed(1); encoded.seek(45); std::unique_ptr<Mlt::Frame> frame(encoded.get_frame());
        auto format=mlt_audio_f32le;int rate=48000,channels=2,samples=800;
        const auto *audio=static_cast<float*>(frame->get_audio(format,rate,channels,samples));QVERIFY(audio);
        QCOMPARE(format,mlt_audio_f32le);
        double peak=0;for(int i=0;i<samples*channels;++i)peak=std::max(peak,double(std::abs(audio[i])));
        QVERIFY2(peak>.01 && peak<.5,"Export must preserve the clip audio through the transition");
    }
    void oneFrameOverlapsAndPortraitFrames() {
        for (const auto &effect : effects) {
            auto p=project(); QVERIFY(p.setTransition(1,effect,1));
            Mlt::Profile profile; Media::configureOutputProfile(profile,90,160);
            for(auto &clip:p.clips)clip.transform.framing="fill";
            QString error;auto timeline=Media::buildTimeline(p,profile,&error);QVERIFY2(timeline,qPrintable(error));
            QCOMPARE(timeline->get_playtime(),119);
            const auto frame=imageAt(*timeline,59,{90,160});QVERIFY(!frame.isNull());QCOMPARE(frame.size(),QSize(90,160));
            QCOMPARE(colorAt(imageAt(*timeline,90,{90,160}),{45,80}),'B');
            QCOMPARE(colorAt(imageAt(*timeline,20,{90,160}),{45,80}),'R');
        }
    }
};
QTEST_MAIN(TransitionTest)
#include "transition_test.moc"
