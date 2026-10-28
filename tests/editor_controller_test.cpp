#include "editor_controller.h"
#include "mlt_engine.h"
#include "preview.h"
#include "preview_cache.h"
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QProcess>
#include <QtTest>

class EditorControllerTest : public QObject {
    Q_OBJECT
    QString fixture() const { return QDir::current().filePath("tests/fixtures/seek.mp4"); }
private slots:
    void appendingPreservesPreviewWithMusicAndTransitions() {
        EditorController e;const auto source=QFINDTESTDATA("fixtures/rotation.mp4");
        QVERIFY(e.importFiles({source,source}));e.setCrossfade(.25);e.setMusicPath(source);e.seek(15);
        const auto original=e.frame();QVERIFY(!original.isNull());const int before=e.duration();
        QVERIFY(e.importFiles({source}));QCOMPARE(e.project().clips.size(),3);QVERIFY(e.duration()>before);
        QCOMPARE(e.position(),15);QCOMPARE(e.frame(),original);
        e.selectClip(0);e.beginVisualGesture();e.setTransform({{"opacity",0.}});
        QTRY_VERIFY_WITH_TIMEOUT(e.frame().pixelColor(e.frame().width()/2,e.frame().height()/2).red()<20,3000);
        e.endVisualGesture(true);QCOMPARE(e.frame(),original);
        e.undo();QCOMPARE(e.duration(),before);QCOMPARE(e.frame(),original);
        e.redo();QCOMPARE(e.project().clips.size(),3);QCOMPARE(e.frame(),original);
        e.seek(e.duration()-1);QVERIFY(!e.frame().isNull());
    }
    void preparesVideoOverlayPreviewWithoutChangingSavedMedia() {
        QTemporaryDir dir;
        const auto source=dir.filePath("overlay.mp4");
        QProcess encode;
        encode.start("/opt/homebrew/bin/ffmpeg",{"-v","error","-f","lavfi","-i","color=c=red:size=2048x1152:rate=30:duration=3","-c:v","libx264","-preset","ultrafast","-pix_fmt","yuv420p",source});
        QVERIFY(encode.waitForFinished(15000));QCOMPARE(encode.exitCode(),0);
        EditorController e;QVERIFY(e.importFiles({fixture()}));
        QVERIFY(e.addVisualOverlay(source,0,0,30,90));
        auto *cache=e.findChild<PreviewCache*>();QVERIFY(cache);
        QTRY_VERIFY_WITH_TIMEOUT(!cache->readyCopy(source,30,90).path.isEmpty(),20000);
        QTRY_COMPARE_WITH_TIMEOUT(e.previewStatus(),QString("Smooth preview"),5000);
        QCOMPARE(e.project().visualOverlays[0].path,source);
        QCOMPARE(e.project().visualOverlays[0].sourceInFrame,30);
        const auto saved=dir.filePath("overlay.isaac.json");QVERIFY(e.saveTo(saved));
        const auto loaded=loadProject(saved);QVERIFY(loaded);
        QCOMPARE(loaded->visualOverlays[0].path,source);QCOMPARE(loaded->visualOverlays[0].sourceInFrame,30);
        e.setVisualLaneVisible(0,false);QCOMPARE(e.previewStatus(),QString("Original media"));
        cache->discard(source);
    }
    void initTestCase() { qputenv("SDL_AUDIODRIVER","dummy"); Media::initializeMlt(); }
    void textOverlaysAddEditDeleteAndUndo() {
        EditorController e;QVERIFY(e.importFiles({fixture()}));e.seek(30);
        QVERIFY(QMetaObject::invokeMethod(&e,"addText",Qt::DirectConnection));
        auto texts=e.property("textOverlays").toList();QCOMPARE(texts.size(),1);QCOMPARE(texts[0].toMap()["startFrame"].toInt(),30);
        const QVariantMap edit{{"text","Adventure Park Day\nwith Isaac and Daddy"},{"fontSize",71},{"style","box"},{"color","#ffc040"}};
        QVERIFY(QMetaObject::invokeMethod(&e,"updateText",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(QVariantMap,edit)));
        QCOMPARE(e.property("currentText").toMap()["fontSize"].toInt(),71);
        QTemporaryDir dir;const auto path=dir.filePath("titles.isaac.json");QVERIFY(e.saveTo(path));
        EditorController restored;QVERIFY(restored.loadFrom(path));QCOMPARE(restored.property("textOverlays").toList().size(),1);
        QVERIFY(QMetaObject::invokeMethod(&e,"deleteText",Qt::DirectConnection));QCOMPARE(e.property("textOverlays").toList().size(),0);
        e.undo();QCOMPARE(e.property("textOverlays").toList().size(),1);QCOMPARE(e.property("currentText").toMap()["text"].toString(),edit["text"].toString());
        e.redo();QCOMPARE(e.property("textOverlays").toList().size(),0);
        QVERIFY(QMetaObject::invokeMethod(&e,"addText",Qt::DirectConnection));QCOMPARE(e.property("textOverlays").toList().size(),1);
        const auto before=e.property("textOverlays");
        QVERIFY(QMetaObject::invokeMethod(&e,"updateText",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(QVariantMap,QVariantMap({{"fontSize",-1}}))));
        QCOMPARE(e.property("textOverlays"),before);
    }
    void textDragCommitsOnceAndCancels() {
        EditorController e;QVERIFY(e.importFiles({fixture()}));
        QVERIFY(QMetaObject::invokeMethod(&e,"addText",Qt::DirectConnection));
        const auto original=e.property("currentText").toMap();
        QVERIFY(QMetaObject::invokeMethod(&e,"beginTextDrag",Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(&e,"dragTextTo",Qt::DirectConnection,Q_ARG(double,.3),Q_ARG(double,.7)));
        QVERIFY(QMetaObject::invokeMethod(&e,"dragTextTo",Qt::DirectConnection,Q_ARG(double,.4),Q_ARG(double,.6)));
        QVERIFY(QMetaObject::invokeMethod(&e,"endTextDrag",Qt::DirectConnection,Q_ARG(bool,false)));
        QCOMPARE(e.property("currentText").toMap()["x"].toDouble(),.4);
        e.undo();QCOMPARE(e.property("currentText").toMap()["x"],original["x"]);
        e.redo();
        QVERIFY(QMetaObject::invokeMethod(&e,"beginTextDrag",Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(&e,"dragTextTo",Qt::DirectConnection,Q_ARG(double,.8),Q_ARG(double,.2)));
        QVERIFY(QMetaObject::invokeMethod(&e,"endTextDrag",Qt::DirectConnection,Q_ARG(bool,true)));
        QCOMPARE(e.property("currentText").toMap()["x"].toDouble(),.4);
    }
    void liveTextEditsRemainDirtyAndSaveWhileFocused() {
        EditorController e;QVERIFY(e.importFiles({fixture()}));e.addText();QTemporaryDir dir;const auto path=dir.filePath("live.isaac.json");QVERIFY(e.saveTo(path));
        e.beginTextDrag();e.previewTextContent("Adventure Park Day");QVERIFY(e.dirty());QVERIFY(e.saveTo(path));QVERIFY(!e.dirty());
        e.previewTextContent("Adventure Park Day with Isaac and Daddy");QVERIFY(e.dirty());e.endTextDrag(false);e.undo();
        QCOMPARE(e.currentText()["text"].toString(),QString("Adventure Park Day"));
        EditorController loaded;QVERIFY(loaded.loadFrom(path));QCOMPARE(loaded.textOverlays()[0].toMap()["text"].toString(),QString("Adventure Park Day"));
    }
    void liveTextColorAndDeletionUpdateTheActualPreview() {
        EditorController e;QVERIFY(e.importFiles({QDir::current().filePath("tests/fixtures/rotation.mp4")}));
        e.addText();e.updateText(0,{{"text","Isaac"},{"color","#ff00ff"},{"fontSize",150},{"shadow",false}});
        const auto magenta=[&](){int count=0;const auto image=e.frame();for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);count+=c.red()>160&&c.blue()>150&&c.green()<110;}return count;};
        QTRY_VERIFY_WITH_TIMEOUT(magenta()>20,3000);e.deleteText();QTRY_COMPARE_WITH_TIMEOUT(magenta(),0,3000);
        e.undo();QTRY_VERIFY_WITH_TIMEOUT(magenta()>20,3000);
    }
    void importSelectionAndHistory() {
        EditorController e;
        QVERIFY(e.importFiles({fixture(),fixture(),fixture(),fixture()}));
        QCOMPARE(e.clips().size(),4); QVERIFY(e.dirty());
        e.selectClip(1); e.selectClip(3,Qt::ControlModifier);
        QCOMPARE(e.selectedIndices(),QVariantList({1,3}));
        e.moveGroup(e.selectedIndices(),0);
        QCOMPARE(e.selectedIndices(),QVariantList({0,1}));
        e.undo(); QCOMPARE(e.selectedIndices(),QVariantList({1,3}));
        e.removeSelection(); QCOMPARE(e.clips().size(),2);
        e.undo(); QCOMPARE(e.clips().size(),4);
        e.undo(); QCOMPARE(e.clips().size(),0);
        e.redo(); QCOMPARE(e.clips().size(),4);
    }
    void trimSplitRotateAndReopen() {
        EditorController e; QVERIFY(e.importFiles({fixture()}));
        QVERIFY(e.trimClip(0,0.5,1.8));
        e.seek(30); QVERIFY(e.canSplit()); e.split();
        QCOMPARE(e.clips().size(),2); e.rotate(90);
        QCOMPARE(e.currentClip().value("rotation").toInt(),90);
        QTemporaryDir dir; const auto path=dir.filePath("edit.isaac.json");
        QVERIFY(e.saveTo(path)); QVERIFY(!e.dirty());
        e.selectClip(0); QVERIFY(!e.dirty());
        EditorController reopened; QVERIFY(reopened.loadFrom(path));
        QCOMPARE(reopened.project().durationFrames(),e.project().durationFrames());
        QCOMPARE(reopened.project().clips[1].rotationDegrees,90);
        QVERIFY(reopened.project().clips[1].hardCutBefore);
        QVERIFY(!reopened.trimClip(0,-1,1));
        QVERIFY(!reopened.trimClip(0,0,100000));
    }
    void noOpSelectionAndAudioTiming() {
        EditorController e; QVERIFY(e.importFiles({fixture(),fixture()}));
        e.setVoicePath(fixture()); e.setVoiceStart(1.5); e.setMusicPath(fixture()); e.setMusicDb(-12);
        QTemporaryDir dir; QVERIFY(e.saveTo(dir.filePath("audio.isaac.json")));
        e.selectClip(0); e.moveGroup({0},1); QVERIFY(!e.dirty());
        QVERIFY(!e.trimClip(0,0,e.project().clips[0].outSeconds)); QVERIFY(!e.dirty());
        QVERIFY(e.trimClip(0,0,1)); QCOMPARE(e.project().voiceStartSeconds,1.5);
        QCOMPARE(e.project().musicDb,-12.0); e.undo(); QVERIFY(!e.dirty());
    }
    void preservesPositiveMusicGain() {
        EditorController e; QVERIFY(e.importFiles({fixture()}));
        e.setMusicDb(12); QCOMPARE(e.project().musicDb,12.0);
        QTemporaryDir dir; const auto path=dir.filePath("gain.isaac.json"); QVERIFY(e.saveTo(path));
        EditorController reopened; QVERIFY(reopened.loadFrom(path)); QCOMPARE(reopened.project().musicDb,12.0);
    }
    void repeatedScrubsPreservePlaybackIntent() {
        EditorController e; QVERIFY(e.importFiles({fixture()}));
        e.togglePlayback(); QVERIFY(e.playing());
        e.beginScrub(); e.seek(20); e.finishScrub();
        e.beginScrub(); e.seek(30); e.finishScrub();
        QTRY_VERIFY_WITH_TIMEOUT(e.playing(),2000);
    }
    void proxyReadyDoesNotCancelPendingResume() {
        EditorController e; QVERIFY(e.importFiles({fixture()}));
        e.togglePlayback(); e.beginScrub(); e.seek(30); e.finishScrub();
        auto *cache=e.findChild<PreviewCache*>(); QVERIFY(cache);
        cache->ready(fixture(),fixture()+".proxy.mp4");
        QTRY_VERIFY_WITH_TIMEOUT(e.playing(),2000);
    }
    void exportCancelsPendingResume() {
        EditorController e; QVERIFY(e.importFiles({fixture()}));
        e.togglePlayback(); e.beginScrub(); e.seek(30); e.finishScrub();
        QTemporaryDir dir; QVERIFY(e.exportTo(dir.filePath("out.mp4")));
        QTRY_VERIFY_WITH_TIMEOUT(!e.exporting(),15000);
        e.skim(60); e.endSkim();
        auto *preview=e.findChild<Preview*>(); QVERIFY(preview);
        QTRY_VERIFY_WITH_TIMEOUT(!preview->isSeeking(),2000);
        QVERIFY(!e.playing());
    }
    void skimAndScrubKeepIndependentPosition() {
        EditorController e; QVERIFY(e.importFiles({fixture()}));
        e.seek(30); QCOMPARE(e.position(),30);
        e.skim(100); QCOMPARE(e.position(),30);
        e.endSkim(); QCOMPARE(e.position(),30);
        e.beginScrub(); e.seek(80); e.finishScrub(true);
        QCOMPARE(e.position(),30);
    }
};
QTEST_MAIN(EditorControllerTest)
#include "editor_controller_test.moc"
