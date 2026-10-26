#include "editor_controller.h"
#include "mlt_engine.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

class InteractionsIntegrationTest : public QObject {
    Q_OBJECT
    QString fixture() const {return QDir::current().filePath("tests/fixtures/seek.mp4");}
private slots:
    void canvasPreviewDoesNotCaptureUnrelatedAudioOrTrimEdits(){
        QTemporaryDir dir;EditorController e;QVERIFY(e.importFiles({fixture()}));e.selectClip(0);QVERIFY(e.saveTo(dir.filePath("base.isaac.json")));
        e.beginVisualGesture();e.setCanvas(1080,1920);e.updateAudio(0,{{"gainDb",-8}});e.endVisualGesture(true);
        QCOMPARE(e.project().canvasWidth,0);QCOMPARE(e.project().clips[0].audio.gainDb,-8.);
        e.undo();QCOMPARE(e.project().canvasWidth,0);QCOMPARE(e.project().clips[0].audio.gainDb,0.);QVERIFY(!e.dirty());
        e.beginVisualGesture();e.setCanvas(1080,1920);QVERIFY(e.trimClip(0,0,1));e.endVisualGesture(true);
        QCOMPARE(e.project().canvasWidth,0);QCOMPARE(e.duration(),60);e.undo();QCOMPARE(e.duration(),120);QVERIFY(!e.dirty());
        e.beginVisualGesture();e.setCanvas(1080,1920);e.selectClip(0);QCOMPARE(e.project().canvasWidth,0);QVERIFY(!e.dirty());
    }
    void addingTitleLeavesVisualTransformMode(){
        EditorController e;QVERIFY(e.importFiles({fixture()}));QVERIFY(e.addVisualOverlay(fixture(),0,0,0,30));e.setTransformMode(true);e.addText();
        QCOMPARE(e.selectedOverlay(),-1);QVERIFY(!e.transformMode());QCOMPARE(e.selectedText(),0);
    }
    void insertionTransfersConnectionsAndRetainsSourceCurves(){
        EditorController e;QVERIFY(e.importFiles({fixture()}));e.selectClip(0);e.editKeyframe("position","add");e.seek(60);e.setTransform({{"x",.8}});
        const auto id=e.project().clips[0].id;QVERIFY(e.addVisualOverlay(fixture(),90,1,0,20));
        e.seek(60);e.selectSource(0);e.setSourceRange(10,30);QVERIFY(e.insertSource());
        QCOMPARE(e.project().clips.size(),3);QCOMPARE(e.duration(),140);QCOMPARE(e.project().overlayStartFrame(0),110);
        QVERIFY(e.project().visualOverlays[0].hostId!=id);QCOMPARE(e.project().visualOverlays[0].hostId,e.project().clips[2].id);
        QCOMPARE(e.project().clips[2].transform.x.keys.size(),2);QCOMPARE(e.project().clips[2].transform.x.valueAt(90),.8);
        e.undo();QCOMPARE(e.duration(),120);QCOMPARE(e.project().visualOverlays[0].hostId,id);QCOMPARE(e.project().overlayStartFrame(0),90);
        e.redo();QCOMPARE(e.project().overlayStartFrame(0),110);
    }
    void markedSourceDropCreatesOneAtomicOverlayCommand(){
        EditorController e;QVERIFY(e.importFiles({fixture()}));e.selectSource(0);e.setSourceRange(20,50);
        const QVariantList urls{QUrl::fromLocalFile(fixture()),QUrl::fromLocalFile(fixture())};
        QVERIFY(e.dropVisualMedia(urls,60,2));QCOMPARE(e.project().visualOverlays.size(),2);
        for(const auto &o:e.project().visualOverlays){QCOMPARE(o.sourceInFrame,20);QCOMPARE(o.durationFrames,30);QCOMPARE(o.lane,2);}
        QCOMPARE(e.assets()[0].toMap()["uses"].toInt(),3);
        e.undo();QVERIFY(e.project().visualOverlays.isEmpty());e.redo();QCOMPARE(e.project().visualOverlays.size(),2);
        const auto label=e.undoText();QVERIFY(!e.dropVisualMedia({urls[0],QUrl::fromLocalFile("/no/such/overlay.mov")},30,0));QCOMPARE(e.project().visualOverlays.size(),2);QCOMPARE(e.undoText(),label);
    }
    void localCutTrimClampsConnectionAndAudioTogether(){
        EditorController e;QVERIFY(e.importFiles({fixture(),fixture()}));e.selectClip(1);e.updateAudio(0,{{"fadeInFrames",90}});
        QVERIFY(e.addVisualOverlay(fixture(),200,0,0,40));e.setTransition(1,"dissolve",60);QCOMPARE(e.duration(),180);
        QVERIFY(e.trimClip(1,0,.3));QCOMPARE(e.project().clips[1].audio.fadeInFrames,18);QCOMPARE(e.duration(),121);
        QCOMPARE(e.project().visualOverlays[0].hostSourceFrame,17);QCOMPARE(e.project().overlayStartFrame(0),120);QCOMPARE(e.project().overlayFrames(0),1);
        QTemporaryDir dir;QVERIFY(e.saveTo(dir.filePath("combined.isaac.json")));EditorController reopened;QVERIFY(reopened.loadFrom(dir.filePath("combined.isaac.json")));
        QCOMPARE(reopened.duration(),121);QCOMPARE(reopened.project().visualOverlays[0].hostId,e.project().visualOverlays[0].hostId);
        e.undo();QCOMPARE(e.project().clips[1].audio.fadeInFrames,90);QCOMPARE(e.project().visualOverlays[0].hostSourceFrame,80);
    }
    void visualPreviewNeverEntersRecovery(){
        QTemporaryDir dir;EditorController e;e.enableRecovery(dir.path());QVERIFY(e.importFiles({fixture()}));e.selectClip(0);
        e.beginVisualGesture();e.setTransform({{"opacity",.1}});
        QTRY_VERIFY_WITH_TIMEOUT(!QDir(dir.path()).entryList({"*.recovery.json"},QDir::Files).isEmpty(),2700);
        const auto saved=loadProject(QDir(dir.path()).filePath(QDir(dir.path()).entryList({"*.recovery.json"},QDir::Files).first()));QVERIFY(saved);
        QCOMPARE(saved->clips[0].transform.opacity.value,1.);e.endVisualGesture(true);QCOMPARE(e.project().clips[0].transform.opacity.value,1.);
    }
    void missingOverlayRetainsConnectionAndRelinks(){
        QTemporaryDir dir;const auto imagePath=dir.filePath("replacement.png");QImage image(160,90,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(imagePath));
        Project p;p.clips.append({fixture(),0,2});VisualOverlay o;o.path=dir.filePath("missing.png");o.hostId=p.clips[0].id;o.hostSourceFrame=20;o.durationFrames=40;p.visualOverlays.append(o);
        const auto path=dir.filePath("offline.isaac.json");QVERIFY(saveProject(p,path));EditorController e;QVERIFY(e.loadFrom(path));QVERIFY(!e.frame().isNull());QCOMPARE(e.missingMedia().size(),1);
        QVERIFY(e.relinkMedia(o.path,imagePath));QCOMPARE(e.project().visualOverlays[0].hostId,o.hostId);QCOMPARE(e.project().overlayStartFrame(0),20);
        e.undo();QCOMPARE(e.project().visualOverlays[0].path,o.path);QVERIFY(!e.frame().isNull());
        Mlt::Profile profile;Media::configureOutputProfile(profile,320,180);QString error;QVERIFY(!Media::buildTimeline(e.project(),profile,&error));QVERIFY(error.contains("Overlay",Qt::CaseInsensitive));
    }
    void frozenExportKeepsCapturedTextPixels(){
        QTemporaryDir dir;EditorController e;QVERIFY(e.importFiles({fixture()}));e.addText();e.updateText(0,{{"text","ISAAC"},{"color","#ff00ff"},{"shadow",false},{"fontSize",90},{"x",.5},{"y",.5}});
        const auto path=dir.filePath("frozen.mp4");QVERIFY(e.exportTo(path));e.updateText(0,{{"color","#00ff00"}});QTRY_VERIFY_WITH_TIMEOUT(!e.exporting(),15000);QCOMPARE(e.exportState(),QString("Completed"));
        Project output;output.clips.append({path,0,2});Mlt::Profile profile;Media::configureOutputProfile(profile,320,180);QString error;auto timeline=Media::buildTimeline(output,profile,&error);QVERIFY2(timeline,qPrintable(error));timeline->seek(30);std::unique_ptr<Mlt::Frame> f(timeline->get_frame());QVERIFY(f);
        uint8_t *pixels=nullptr;auto format=mlt_image_rgba;int width=320,height=180;QVERIFY(!mlt_frame_get_image(f->get_frame(),&pixels,&format,&width,&height,0));QVERIFY(pixels);
        QImage frame(pixels,width,height,width*4,QImage::Format_RGBA8888);int magenta=0;for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto c=frame.pixelColor(x,y);magenta+=c.red()>130&&c.blue()>130&&c.green()<110;}QVERIFY(magenta>20);QCOMPARE(e.project().textOverlays[0].color,QString("#00ff00"));
    }
};
QTEST_MAIN(InteractionsIntegrationTest)
#include "interactions_integration_test.moc"
