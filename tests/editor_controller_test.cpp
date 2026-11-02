#include "editor_controller.h"
#include "mlt_engine.h"
#include "preview.h"
#include "preview_cache.h"
#include "recovery_store.h"
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QProcess>
#include <QStandardPaths>
#include <QtTest>

class EditorControllerTest : public QObject {
    Q_OBJECT
    QString fixture() const { return QDir::current().filePath("tests/fixtures/seek.mp4"); }
private slots:
    void preparationRetriesFailedMetadataBeforeChoosingAutoCanvas() {
        QTemporaryDir dir;QFile original(fixture());QVERIFY(original.open(QIODevice::ReadOnly));const auto bytes=original.readAll();
        const auto path=dir.filePath("temporary-failure.mp4");const auto modified=QDateTime::fromMSecsSinceEpoch(1700000000000LL);
        QFile source(path);QVERIFY(source.open(QIODevice::WriteOnly));QCOMPARE(source.write(QByteArray(bytes.size(),'\0')),bytes.size());
        QVERIFY(source.flush());QVERIFY(source.setFileTime(modified,QFileDevice::FileModificationTime));source.close();
        QVERIFY(!Media::mediaInfo(path));
        QVERIFY(source.open(QIODevice::WriteOnly));QCOMPARE(source.write(bytes),bytes.size());QVERIFY(source.flush());
        QVERIFY(source.setFileTime(modified,QFileDevice::FileModificationTime));source.close();QVERIFY(!Media::mediaInfo(path));
        Project project;project.clips={{path,0,1}};const auto saved=dir.filePath("retry.json");QVERIFY(saveProject(project,saved));
        EditorController editor;QVERIFY(editor.loadFrom(saved));QTRY_VERIFY_WITH_TIMEOUT(!editor.loading(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(!editor.frame().isNull(),5000);QVERIFY(Media::mediaInfo(path));
    }
    void uiImportsAreDeferredAndDiscardStaleResults() {
        EditorController e;QVariantList urls{QUrl::fromLocalFile(fixture())};
        e.importUrls(urls);QCOMPARE(e.duration(),0);
        QTRY_COMPARE_WITH_TIMEOUT(e.project().clips.size(),1,10000);
        QTRY_VERIFY_WITH_TIMEOUT(!e.loading(),10000);QTRY_VERIFY_WITH_TIMEOUT(!e.frame().isNull(),10000);
        EditorController superseded;superseded.importUrls(urls);superseded.newProject();
        QTest::qWait(300);QCOMPARE(superseded.duration(),0);QVERIFY(superseded.project().assets.isEmpty());
    }
    void canceledAndSupersededUiImportsDoNotCommit() {
        const QVariantList urls{QUrl::fromLocalFile(fixture())};EditorController canceled;
        canceled.importUrls(urls);canceled.cancelImport();QTest::qWait(100);
        QCOMPARE(canceled.duration(),0);QVERIFY(canceled.project().assets.isEmpty());
        EditorController superseded;superseded.importUrls(urls);superseded.importMediaUrls(urls);
        QTRY_COMPARE_WITH_TIMEOUT(superseded.project().assets.size(),1,5000);QCOMPARE(superseded.duration(),0);
        QTRY_VERIFY_WITH_TIMEOUT(!superseded.frame().isNull(),5000);
    }
    void timelinePreparationDeliversOneSettledStill() {
        EditorController e;QSignalSpy frames(&e,&EditorController::frameChanged);
        QVERIFY(e.importFiles({fixture()}));QCOMPARE(frames.size(),0);
        e.setCanvas(240,136);e.setCanvas(160,90);e.seek(37);
        QTRY_VERIFY_WITH_TIMEOUT(!e.frame().isNull(),5000);
        QCOMPARE(e.frame().size(),QSize(160,90));QCOMPARE(e.position(),37);QCOMPARE(frames.size(),1);
        const auto diagnostics=e.performanceDiagnostics();QVERIFY(diagnostics["timelinePreparationJobs"].toULongLong()>=3);
        QVERIFY(diagnostics["supersededPreparationJobs"].toULongLong()>=2);
    }
    void asyncLegacyRecoveryPreservesOfferAndDirtyState() {
        QTemporaryDir dir;QFile broken(dir.filePath("legacy-audio.wav"));QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("not audio");broken.close();
        QString offer;const auto projectPath=dir.filePath("original.isaac.json");
        {RecoveryStore store(dir.path());Project p;p.clips={{fixture(),0,2}};p.voicePath=broken.fileName();store.schedule(p,projectPath);QVERIFY(store.flush());offer=store.snapshotPath();}
        {EditorController canceled;canceled.enableRecovery(dir.path());canceled.recoverSnapshotAsync(offer);canceled.cancelImport();QTest::qWait(100);QCOMPARE(canceled.duration(),0);QVERIFY(QFile::exists(offer));}
        EditorController e;e.enableRecovery(dir.path());QCOMPARE(e.recoveryCandidates().size(),1);
        e.recoverSnapshotAsync(offer);QCOMPARE(e.duration(),0);QVERIFY(QFile::exists(offer));
        QTRY_COMPARE_WITH_TIMEOUT(e.duration(),120,5000);
        QVERIFY(e.dirty());QVERIFY(e.hasProjectFile());QVERIFY(e.title().startsWith("original.isaac.json"));
        QVERIFY(!QFile::exists(projectPath));
    }
    void failedAsyncRelinkPreservesProject() {
        QTemporaryDir dir;QFile broken(dir.filePath("broken.mp4"));QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("not a media file");broken.close();
        EditorController e;QVERIFY(e.importFiles({fixture()}));e.relinkMediaAsync(fixture(),broken.fileName());
        QTRY_VERIFY_WITH_TIMEOUT(!e.notice().isEmpty(),5000);QCOMPARE(e.project().clips[0].path,fixture());
        QVERIFY(!e.performanceDiagnostics()["importing"].toBool());
    }
    void projectResetsNotifyAudioLevels() {
        EditorController e;auto *preview=e.findChild<Preview*>();QVERIFY(preview);
        preview->audioPeak(1.1);QVERIFY(e.masterClipped());QSignalSpy levels(&e,&EditorController::audioLevelsChanged);
        e.newProject();QVERIFY(!e.masterClipped());QCOMPARE(e.masterPeak(),-60.0);QVERIFY(!levels.isEmpty());
        QTemporaryDir dir;Project saved;saved.clips={{fixture(),0,1}};QVERIFY(saveProject(saved,dir.filePath("levels.json")));
        preview->audioPeak(1.1);levels.clear();QVERIFY(e.loadFrom(dir.filePath("levels.json")));
        QVERIFY(!e.masterClipped());QCOMPARE(e.masterPeak(),-60.0);QVERIFY(!levels.isEmpty());
    }
    void playbackUsesNarrowPositionNotifications() {
        EditorController e;QVERIFY(e.importFiles({fixture()}));QTRY_VERIFY(!e.frame().isNull());
        QSignalSpy positions(&e,&EditorController::positionChanged),states(&e,&EditorController::stateChanged),composition(&e,&EditorController::compositionChanged);
        e.togglePlayback();QTRY_VERIFY(positions.size()>2);states.clear();composition.clear();positions.clear();
        QTest::qWait(120);QVERIFY(positions.size()>0);QCOMPARE(composition.size(),0);QVERIFY(states.size()<positions.size());
        e.togglePlayback();
    }
    void preparedTimelineDiscardsOldRevision() {
        EditorController e;QVERIFY(e.importFiles({fixture()}));
        e.setCanvas(240,136);e.setCanvas(160,90);e.seek(37);
        QTRY_VERIFY_WITH_TIMEOUT(!e.loading(),10000);QTRY_COMPARE(e.frame().size(),QSize(160,90));QCOMPARE(e.position(),37);
        QTemporaryDir dir;QVERIFY(e.saveTo(dir.filePath("prepared.json")));
        e.setCanvas(240,136);QVERIFY(e.saveTo(dir.filePath("prepared.json")));e.newProject();QTest::qWait(300);
        QCOMPARE(e.duration(),0);QVERIFY(e.frame().isNull());
    }
    void autoCanvasFollowsCameraOrientation() {
        EditorController e;
        QVERIFY(e.importFiles({QFINDTESTDATA("fixtures/rotation-metadata.mp4")}));
        QTRY_COMPARE(e.format(), QString("90×160 · 60 fps"));
        QTRY_COMPARE(e.frame().size(), QSize(90, 160));
        e.setCanvas(160, 90);
        QTRY_COMPARE(e.format(), QString("160×90 · 60 fps"));
        QTRY_COMPARE(e.frame().size(), QSize(160, 90));
        e.setCanvas(0, 0);
        QTRY_COMPARE(e.format(), QString("90×160 · 60 fps"));
        QTRY_COMPARE(e.frame().size(), QSize(90, 160));
    }
    void damagedSourceProxyFallsBackToOriginal() {
        QTemporaryDir dir;const auto source=dir.filePath("source.mp4");QVERIFY(QFile::copy(fixture(),source));
        EditorController e;QVERIFY(e.importMedia({source}));auto *cache=e.findChild<PreviewCache*>();QVERIFY(cache);QVERIFY(cache->setCacheDirectory(dir.filePath("cache")));
        cache->request(source,120);QTRY_VERIFY_WITH_TIMEOUT(!cache->readyCopy(source,0,120).path.isEmpty(),10000);
        const auto proxy=cache->readyCopy(source,0,120).path;QFile corrupt(proxy);QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("invalid proxy");corrupt.close();
        QSignalSpy frames(&e,&EditorController::frameChanged);e.selectSource(0);e.seekSource(30);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(),3000);QVERIFY(!e.frame().isNull());
        QVERIFY(!e.notice().contains("Could not open source preview"));QCOMPARE(e.sourcePosition(),30);
        QVERIFY(cache->readyCopy(source,0,120).path.isEmpty());
    }
    void workstationPreviewBudgetIsCoordinated() {
        QTemporaryDir dir;const auto source=dir.filePath("large.mp4");QProcess encode;
        auto ffmpeg=QStandardPaths::findExecutable("ffmpeg");
        if(ffmpeg.isEmpty())ffmpeg=QStandardPaths::findExecutable("ffmpeg",{"/opt/homebrew/bin","/usr/local/bin"});
        QVERIFY2(!ffmpeg.isEmpty(),"FFmpeg is required for the preview integration test");
        encode.start(ffmpeg,{"-v","error","-f","lavfi","-i","testsrc2=size=2048x1152:rate=30:duration=1",
            "-c:v","libx264","-preset","ultrafast","-pix_fmt","yuv420p",source});
        QVERIFY(encode.waitForFinished(15000));QCOMPARE(encode.exitCode(),0);
        const auto budget=Performance::budgetFor({32,quint64(168)<<30,quint64(128)<<30});
        EditorController e(nullptr,budget);QVERIFY(e.importFiles({source}));
        QTRY_COMPARE(e.frame().size(),QSize(1920,1080));
        auto *cache=e.findChild<PreviewCache*>();QVERIFY(cache);QVERIFY(cache->setCacheDirectory(dir.filePath("cache")));
        QTRY_VERIFY_WITH_TIMEOUT(!cache->readyCopy(source,0,60).path.isEmpty(),20000);
        QTRY_COMPARE_WITH_TIMEOUT(e.previewStatus(),QString("Smooth preview"),5000);
        QSignalSpy changed(&e,&EditorController::frameChanged);
        e.seek(10);QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(),3000);
        const QImage timelineFrame=e.frame();
        changed.clear();e.selectSource(0);cache->discard(source);e.seekSource(10);
        QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(),3000);QTRY_COMPARE_WITH_TIMEOUT(e.sourcePosition(),10,3000);
        QTRY_COMPARE_WITH_TIMEOUT(e.frame(),timelineFrame,3000);
        QTRY_COMPARE(e.frame().size(),QSize(1920,1080));
        cache->discard(source);
    }
    void appendingPreservesPreviewWithMusicAndTransitions() {
        EditorController e;const auto source=QFINDTESTDATA("fixtures/rotation.mp4");
        QVERIFY(e.importFiles({source,source}));e.setCrossfade(.25);e.setMusicPath(source);e.seek(15);
        QTRY_VERIFY(!e.frame().isNull());const auto original=e.frame();const int before=e.duration();
        QVERIFY(e.importFiles({source}));QCOMPARE(e.project().clips.size(),3);QVERIFY(e.duration()>before);
        QCOMPARE(e.position(),15);QCOMPARE(e.frame(),original);
        e.selectClip(0);e.beginVisualGesture();e.setTransform({{"opacity",0.}});
        QTRY_VERIFY_WITH_TIMEOUT(e.frame().pixelColor(e.frame().width()/2,e.frame().height()/2).red()<20,3000);
        e.endVisualGesture(true);QTRY_COMPARE(e.frame(),original);
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
        e.setVisualLaneVisible(0,false);QTRY_COMPARE(e.previewStatus(),QString("Original media"));
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
    void exportDetailsShowCompatibleDimensionsWithoutChangingCanvas_data() {
        QTest::addColumn<bool>("optimize");QTest::addColumn<QString>("dimensions");
        QTest::newRow("native-default")<<false<<QString("2880×3840 · 60 fps");
        QTest::newRow("iPhone")<<true<<QString("2160×2880 · 60 fps");
    }
    void exportDetailsShowCompatibleDimensionsWithoutChangingCanvas() {
        QFETCH(bool,optimize);QFETCH(QString,dimensions);
        EditorController e;QVERIFY(e.importFiles({fixture()}));e.setCanvas(2880,3840);
        QTemporaryDir dir;QVERIFY(e.exportTo(dir.filePath("phone.mp4"),optimize));
        const auto details=e.exportDetails();
        e.cancelExport();QTRY_VERIFY_WITH_TIMEOUT(!e.exporting(),10000);
        QVERIFY2(details.startsWith(dimensions),qPrintable(details));
        QCOMPARE(e.project().canvasWidth,2880);QCOMPARE(e.project().canvasHeight,3840);
        e.setCanvas(160,90);QVERIFY(e.retryExport());const auto retriedDetails=e.exportDetails();
        e.cancelExport();QTRY_VERIFY_WITH_TIMEOUT(!e.exporting(),10000);
        QVERIFY2(retriedDetails.startsWith(dimensions),qPrintable(retriedDetails));
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
