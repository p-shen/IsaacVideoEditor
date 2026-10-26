#include "editor_controller.h"
#include "recovery_store.h"
#include "mlt_engine.h"
#include "preview_item.h"
#include "qml_registration.h"
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QFile>
#include <QSignalSpy>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>

class RecoveryExportTest : public QObject {
    Q_OBJECT
    QString fixture() const { return QDir::current().filePath("tests/fixtures/seek.mp4"); }
    Project sample() const { Project p; p.clips.append({fixture(),0,2}); return p; }
private slots:
    void initTestCase(){QQuickStyle::setStyle("Basic");registerEditorQmlTypes();}
    void crashWriter() {
        const auto directory=qEnvironmentVariable("ISAAC_RECOVERY_CRASH_TEST");
        if(directory.isEmpty())return;
        RecoveryStore store(directory);store.schedule(sample(),{});QVERIFY(store.flush());
        QTest::qWait(30000); // Parent kills this process while its lock is live.
    }
    void recoverySurvivesForcedTermination() {
        QTemporaryDir dir;QProcess child;auto environment=QProcessEnvironment::systemEnvironment();
        environment.insert("ISAAC_RECOVERY_CRASH_TEST",dir.path());child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(),{"crashWriter"});QVERIFY(child.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(!QDir(dir.path()).entryList({"*.recovery.json"},QDir::Files).isEmpty(),5000);
        child.kill();QVERIFY(child.waitForFinished());
        const auto offers=RecoveryStore::candidates(dir.path());QCOMPARE(offers.size(),1);
        auto p=loadProject(offers[0].toMap()["path"].toString());QVERIFY(p);QCOMPARE(p->durationFrames(),120);
    }
    void transientTextNeverEntersRecovery() {
        QTemporaryDir dir;EditorController editor;editor.enableRecovery(dir.path());QVERIFY(editor.importFiles({fixture()}));
        editor.addText();editor.beginTextDrag();editor.dragTextTo(.8,.8);
        QTRY_VERIFY_WITH_TIMEOUT(!QDir(dir.path()).entryList({"*.recovery.json"},QDir::Files).isEmpty(),2600);
        const auto path=QDir(dir.path()).filePath(QDir(dir.path()).entryList({"*.recovery.json"},QDir::Files).first());
        auto p=loadProject(path);QVERIFY(p);QCOMPARE(p->textOverlays[0].x,.5);QCOMPARE(p->textOverlays[0].y,.45);
        editor.endTextDrag(true);QCOMPARE(editor.project().textOverlays[0].x,.5);
    }
    void recoveryIsSeparateAtomicAndKeepsUntitledEdits() {
        QTemporaryDir dir;
        RecoveryStore store(dir.path());
        auto p=sample();p.textOverlays.append(TextOverlay{});
        store.schedule(p,{});
        QCOMPARE(RecoveryStore::candidates(dir.path()).size(),0); // Current process owns its recovery.
        QVERIFY(store.flush());
        const auto path=store.snapshotPath();
        QString error;auto saved=loadProject(path,&error);QVERIFY2(saved,qPrintable(error));
        QCOMPARE(saved->textOverlays.size(),1);
        QVERIFY(!QFile::exists(dir.filePath("Untitled.isaac.json")));
        store.clear();QVERIFY(!QFile::exists(path));
    }
    void recoveryOfferSurvivesSessionAndFailureDoesNotLoseIt() {
        QTemporaryDir dir;QString path;
        { RecoveryStore store(dir.path());store.schedule(sample(),"/original/movie.isaac.json");QVERIFY(store.flush());path=store.snapshotPath(); }
        const auto offers=RecoveryStore::candidates(dir.path());QCOMPARE(offers.size(),1);
        QCOMPARE(offers[0].toMap().value("projectPath").toString(),QString("/original/movie.isaac.json"));
        EditorController editor;editor.enableRecovery(dir.path());
        QCOMPARE(editor.recoveryCandidates().size(),1);QVERIFY(editor.recoverSnapshot(path));
        QVERIFY(editor.dirty());QCOMPARE(editor.duration(),120);QVERIFY(editor.recoveryCandidates().isEmpty());
        QFile blocked(dir.filePath("blocked"));QVERIFY(blocked.open(QIODevice::WriteOnly));blocked.close();
        RecoveryStore failing(blocked.fileName()+"/inside");failing.schedule(sample(),{});QVERIFY(!failing.flush());
        QVERIFY(failing.status().contains("failed",Qt::CaseInsensitive));
    }
    void recoveryRetryWorksAfterFolderIsRepaired() {
        QTemporaryDir dir;QFile blocker(dir.filePath("blocked"));QVERIFY(blocker.open(QIODevice::WriteOnly));blocker.close();
        RecoveryStore store(blocker.fileName()+"/inside");store.schedule(sample(),{});QVERIFY(!store.flush());
        QVERIFY(QFile::remove(blocker.fileName()));QVERIFY(QDir().mkpath(blocker.fileName()+"/inside"));
        QVERIFY(store.flush());QVERIFY(loadProject(store.snapshotPath()).has_value());
    }
    void relinkRejectsShorterSourceRange() {
        QTemporaryDir dir;auto p=sample();
        p.assets.append({dir.filePath("missing.mov"),"video",600,300,240,500});
        auto path=dir.filePath("movie.isaac.json");QVERIFY(saveProject(p,path));
        EditorController editor;QVERIFY(editor.loadFrom(path));
        QVERIFY(!editor.relinkMedia(p.assets[0].path,fixture()));
        QCOMPARE(editor.project().assets[0].inFrame,240);QCOMPARE(editor.project().assets[0].outFrame,500);QVERIFY(!editor.dirty());
    }
    void relinkRejectsVideoOnlyAudio() {
        QTemporaryDir dir;auto p=sample();p.voicePath=dir.filePath("missing.wav");auto path=dir.filePath("movie.isaac.json");QVERIFY(saveProject(p,path));
        EditorController editor;QVERIFY(editor.loadFrom(path));
        QVERIFY(!editor.relinkMedia(p.voicePath,fixture()));QCOMPARE(editor.project().voicePath,p.voicePath);QVERIFY(!editor.dirty());
    }
    void relinkDoesNotDiscardConflictingLibraryRanges() {
        QTemporaryDir dir;auto p=sample();p.assets={{dir.filePath("missing.mov"),"video",120,60,30,100},{fixture(),"video",120,0,0,120}};
        const auto path=dir.filePath("movie.isaac.json");QVERIFY(saveProject(p,path));EditorController editor;QVERIFY(editor.loadFrom(path));
        QVERIFY(!editor.relinkMedia(p.assets[0].path,fixture()));QCOMPARE(editor.project().assets.size(),2);
        QCOMPARE(editor.project().assets[0].inFrame,30);QCOMPARE(editor.project().assets[1].inFrame,0);QVERIFY(!editor.dirty());
    }
    void idleAndContinuousEditsWriteCommittedSnapshots() {
        QTemporaryDir dir;RecoveryStore store(dir.path());
        store.schedule(sample(),{});QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(store.snapshotPath()),2600);
        auto p=sample();p.musicDb=-11;store.schedule(p,{});
        QElapsedTimer elapsed;elapsed.start();
        while(elapsed.elapsed()<31000){p.musicDb=-11-(elapsed.elapsed()%5);store.schedule(p,{});QTest::qWait(150);}
        auto saved=loadProject(store.snapshotPath());QVERIFY(saved);QVERIFY(saved->musicDb!=-18);
    }
    void offlineProjectKeepsTimingAndRelinksAsOneEdit() {
        QTemporaryDir dir;auto p=sample();p.clips[0].path=dir.filePath("gone.mp4");
        p.textOverlays.append(TextOverlay{});QString error;auto file=dir.filePath("offline.isaac.json");QVERIFY(saveProject(p,file,&error));
        EditorController editor;QVERIFY(editor.loadFrom(file));QCOMPARE(editor.duration(),120);QCOMPARE(editor.missingMedia().size(),1);
        QVERIFY(!editor.frame().isNull());
        QVERIFY(editor.relinkMedia(p.clips[0].path,fixture()));QVERIFY(editor.missingMedia().isEmpty());QCOMPARE(editor.project().textOverlays.size(),1);
        editor.undo();QCOMPARE(editor.project().clips[0].path,p.clips[0].path);QCOMPARE(editor.duration(),120);
        editor.redo();QCOMPARE(editor.project().clips[0].path,fixture());
    }
    void folderRelinkingKeepsAmbiguousMatchesForReview() {
        QTemporaryDir dir;QVERIFY(QDir(dir.path()).mkdir("first"));QVERIFY(QDir(dir.path()).mkdir("second"));
        QVERIFY(QFile::copy(fixture(),dir.filePath("first/gone.mp4")));QVERIFY(QFile::copy(fixture(),dir.filePath("second/gone.mp4")));
        auto p=sample();p.clips[0].path=dir.filePath("missing/gone.mp4");const auto project=dir.filePath("offline.isaac.json");QVERIFY(saveProject(p,project));
        EditorController editor;QVERIFY(editor.loadFrom(project));editor.searchRelinkFolder(dir.path());
        QCOMPARE(editor.relinkProposals().size(),1);const auto proposal=editor.relinkProposals()[0].toMap();
        QVERIFY(proposal["ambiguous"].toBool());QCOMPARE(proposal["candidates"].toStringList().size(),2);
        QCOMPARE(editor.project().clips[0].path,p.clips[0].path); // Suggestions never change the edit.
        QVERIFY(editor.relinkMedia(p.clips[0].path,dir.filePath("second/gone.mp4")));QVERIFY(editor.relinkProposals().isEmpty());
        QVERIFY(QFile::exists(dir.filePath("first/gone.mp4")));QVERIFY(QFile::exists(project));
    }
    void cancelKeepsExistingDestinationAndCleansOwnTemporary() {
        QTemporaryDir dir;const auto output=dir.filePath("existing.mp4");QFile old(output);QVERIFY(old.open(QIODevice::WriteOnly));old.write("KEEP");old.close();
        std::atomic_bool cancel{true};QString error;
        QVERIFY(!Media::exportMp4(sample(),output,{},&error,[&]{return cancel.load();}));
        QVERIFY(error.contains("cancel",Qt::CaseInsensitive));QVERIFY(old.open(QIODevice::ReadOnly));QCOMPARE(old.readAll(),QByteArray("KEEP"));old.close();
        cancel=false;
        QVERIFY(!Media::exportMp4(sample(),output,[&](int){cancel=true;},&error,[&]{return cancel.load();}));
        QVERIFY(old.open(QIODevice::ReadOnly));QCOMPARE(old.readAll(),QByteArray("KEEP"));
        QCOMPARE(QDir(dir.path()).entryList({".isaac-*"},QDir::Files|QDir::Hidden).size(),0);
    }
    void exportUsesFrozenRevisionWhileEditingContinues() {
        QTemporaryDir dir;EditorController editor;QVERIFY(editor.importFiles({fixture()}));
        const auto output=dir.filePath("frozen.mp4");QVERIFY(editor.exportTo(output));
        QVERIFY(editor.trimClip(0,0,1));QCOMPARE(editor.duration(),60);
        editor.addText();QCOMPARE(editor.project().textOverlays.size(),1);editor.undo();QVERIFY(editor.project().textOverlays.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!editor.exporting(),20000);
        QCOMPARE(editor.exportState(),QString("Completed"));
        auto info=Media::mediaInfo(output);QVERIFY(info);QCOMPARE(qRound(info->durationSeconds*60),120);
        QVERIFY(editor.exportRevision().contains("120 frames"));
    }
    void viewerTextCanStillMoveDuringExport() {
        QTemporaryDir dir;EditorController editor;QStringList clips;for(int i=0;i<20;++i)clips.append(fixture());QVERIFY(editor.importFiles(clips));editor.addText();
        QQuickWindow window;window.resize(800,450);auto *viewer=new VideoPreview(window.contentItem());viewer->setSize({800,450});viewer->setController(&editor);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto center=viewer->textRect(0).center().toPoint();QVERIFY(editor.exportTo(dir.filePath("frozen.mp4")));QVERIFY(editor.exporting());
        QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,center);QTest::mouseMove(&window,center+QPoint(60,0));QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,center+QPoint(60,0));
        QVERIFY(editor.project().textOverlays[0].x>.5);editor.cancelExport();QTRY_VERIFY_WITH_TIMEOUT(!editor.exporting(),10000);
    }
    void exportPanelIsNonmodalAndEditingActionsStayEnabled() {
        QTemporaryDir dir;EditorController editor;editor.enableRecovery(dir.path());QStringList clips;for(int i=0;i<20;++i)clips.append(fixture());QVERIFY(editor.importFiles(clips));
        QQmlApplicationEngine engine;setEditorQmlContext(engine,editor);QSignalSpy warnings(&engine,&QQmlEngine::warnings);
        engine.load(QUrl::fromLocalFile(QDir::current().filePath("qml/Main.qml")));QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(editor.exportTo(dir.filePath("frozen.mp4")));auto *panel=window->findChild<QObject*>("exportPanel");QVERIFY(panel);
        QVERIFY(!panel->property("modal").toBool());QVERIFY(panel->property("visible").toBool());
        auto *add=window->findChild<QQuickItem*>("addTextButton");QVERIFY(add);QVERIFY(add->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(panel,"close"));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,add->mapToScene({add->width()/2,add->height()/2}).toPoint());QCOMPARE(editor.project().textOverlays.size(),1);
        editor.cancelExport();QTRY_VERIFY_WITH_TIMEOUT(!editor.exporting(),10000);QCOMPARE(warnings.count(),0);
    }
    void cancelDuringRenderStopsAndPreservesDestination() {
        QTemporaryDir dir;auto p=sample();for(int i=0;i<59;++i)p.clips.append({fixture(),0,2});
        const auto path=dir.filePath("existing.mp4");QFile output(path);QVERIFY(output.open(QIODevice::WriteOnly));output.write("ORIGINAL");output.close();
        bool cancel=false;QString error;
        QVERIFY(!Media::exportMp4(p,path,[&](int progress){if(progress>0)cancel=true;},&error,[&]{return cancel;}));
        QVERIFY(cancel);QVERIFY2(error.contains("cancel",Qt::CaseInsensitive),qPrintable(error));
        QVERIFY(output.open(QIODevice::ReadOnly));QCOMPARE(output.readAll(),QByteArray("ORIGINAL"));
        QCOMPARE(QDir(dir.path()).entryList({".isaac-*"},QDir::Files|QDir::Hidden).size(),0);
    }
};
QTEST_MAIN(RecoveryExportTest)
#include "recovery_export_test.moc"
