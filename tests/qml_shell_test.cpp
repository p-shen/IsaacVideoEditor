#include "editor_controller.h"
#include "qml_registration.h"
#include "mlt_engine.h"
#include "quick_timeline.h"
#include "theme.h"
#include "update_checker.h"
#include "update_fixtures.h"
#include <QQuickStyle>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QMessageBox>
#include <QQuickWindow>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QMimeData>
#include <QScopeGuard>
#include <QStyleHints>
#include <QTimer>
#include <QAccessible>
#include <QSignalSpy>
#include <QtTest>
#include <cmath>

class QmlShellTest : public QObject {
    Q_OBJECT
    QString fixture() const { return QDir::current().filePath("tests/fixtures/seek.mp4"); }
    QQuickWindow *load(QQmlApplicationEngine &engine,EditorController &editor) {
        setEditorQmlContext(engine,editor);
        engine.load(QUrl::fromLocalFile(QDir::current().filePath("qml/Main.qml")));
        auto *window=engine.rootObjects().isEmpty()?nullptr:qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if(window) { window->requestActivate(); if(!QTest::qWaitForWindowExposed(window,2000)) return nullptr; }
        return window;
    }
private slots:
    void initTestCase() { qputenv("SDL_AUDIODRIVER","dummy"); Media::initializeMlt(); QQuickStyle::setStyle("Basic"); applyAppTheme(*qobject_cast<QApplication*>(qApp)); registerEditorQmlTypes(); }
    void visualOverlayTracksAndActionsAreRemoved() {
        EditorController editor;QQmlApplicationEngine engine;QSignalSpy warnings(&engine,&QQmlEngine::warnings);
        auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importFiles({fixture()}));
        for(int width:{1320,760}){
            window->resize(width,760);QVERIFY(QMetaObject::invokeMethod(window,"focusInspector",Q_ARG(QVariant,4),Q_ARG(QVariant,false)));QTest::qWait(30);
            QVERIFY(!window->findChild<QQuickItem*>("overlayTimeline"));
            const QStringList removed={"Add overlay","+ Overlay","Delete overlay","Detach host’s overlays","V1","V2","V3"};
            for(auto *item:window->findChildren<QQuickItem*>())QVERIFY2(!removed.contains(item->property("text").toString()),qPrintable(item->property("text").toString()));
            QVERIFY(window->findChild<QQuickItem*>("canvasWidthField"));QVERIFY(window->findChild<QQuickItem*>("addTextButton"));QVERIFY(window->findChild<QuickTimeline*>("timeline"));
        }
        window->resize(1320,860);editor.selectClip(0);
        const auto directory=qEnvironmentVariable("ISAAC_COMPOSITION_SCREENSHOTS");
        if(!directory.isEmpty()){
            QDir().mkpath(directory);QMetaObject::invokeMethod(window,"focusInspector",Q_ARG(QVariant,4),Q_ARG(QVariant,false));editor.setTransformMode(true);
            for(bool dark:{true,false}){setAppColorScheme(*qobject_cast<QApplication*>(qApp),dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);QTest::qWait(150);QVERIFY(window->grabWindow().save(directory+(dark?"/composition-dark.png":"/composition-light.png")));}
            window->resize(760,560);auto font=qApp->font();font.setPointSize(24);window->setProperty("font",font);QTest::qWait(150);QVERIFY(window->grabWindow().save(directory+"/composition-narrow.png"));
        }

        editor.addText();QCOMPARE(editor.project().textOverlays.size(),1);editor.addCaption();QCOMPARE(editor.project().captions.size(),1);
        QCOMPARE(warnings.count(),0);
    }
    void audioDragEscapeRetainsTimelineFocusAndCancels() {
        EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importFiles({fixture(),fixture()}));editor.setVoicePath(fixture());
        auto *timeline=window->findChild<QuickTimeline*>("timeline");QVERIFY(timeline);QTest::qWait(50);
        const auto from=timeline->mapToScene(timeline->audioRect(1).center()+QPointF(0,8)).toPoint();const auto to=from+QPoint(60,0);
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,from);QTest::mouseMove(window,to);QTest::qWait(30);
        QTest::keyClick(window,Qt::Key_Escape);QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,to);
        QCOMPARE(editor.project().voiceStartSeconds,0.0);QCOMPARE(editor.undoText(),QString("Add narration"));
    }
    void audioAndCutFieldsEditTheAuthoritativeProject() {
        EditorController editor;QQmlApplicationEngine engine;QSignalSpy warnings(&engine,&QQmlEngine::warnings);
        auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importFiles({fixture(),fixture()}));editor.setVoicePath(fixture());editor.setMusicPath(fixture());
        editor.selectAudio(1);QVERIFY(QMetaObject::invokeMethod(window,"focusInspector",Q_ARG(QVariant,1),Q_ARG(QVariant,false)));
        auto *gain=window->findChild<QQuickItem*>("audioGainField");QVERIFY(gain);gain->forceActiveFocus();gain->setProperty("text","-8.5");QVERIFY(QMetaObject::invokeMethod(gain,"editingFinished"));
        QCOMPARE(editor.project().voiceAudio.gainDb,-8.5);editor.undo();QCOMPARE(editor.project().voiceAudio.gainDb,0.0);
        editor.selectTransition(1);QVERIFY(QMetaObject::invokeMethod(window,"focusInspector",Q_ARG(QVariant,2),Q_ARG(QVariant,false)));
        auto *precision=window->findChild<QQuickItem*>("transitionPrecisionDetails");QVERIFY(precision);precision->setProperty("expanded",true);
        auto *cut=window->findChild<QQuickItem*>("localTransitionFrames");QVERIFY(cut);cut->forceActiveFocus();cut->setProperty("text","18");QVERIFY(QMetaObject::invokeMethod(cut,"editingFinished"));QCOMPARE(editor.project().transitionFramesBefore(1),18);
        const auto directory=qEnvironmentVariable("ISAAC_AUDIO_SCREENSHOTS");
        if(!directory.isEmpty()){
            QDir().mkpath(directory);
            for(bool dark:{true,false}){
                setAppColorScheme(*qobject_cast<QApplication*>(qApp),dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);window->resize(1320,860);
                QMetaObject::invokeMethod(window,"focusInspector",Q_ARG(QVariant,1),Q_ARG(QVariant,false));QTest::qWait(300);
                QVERIFY(window->grabWindow().save(directory+(dark?"/audio-dark.png":"/audio-light.png")));
                QMetaObject::invokeMethod(window,"focusInspector",Q_ARG(QVariant,2),Q_ARG(QVariant,false));QTest::qWait(100);
                QVERIFY(window->grabWindow().save(directory+(dark?"/cuts-dark.png":"/cuts-light.png")));
            }
            window->resize(760,560);auto font=qApp->font();font.setPointSize(24);window->setProperty("font",font);QMetaObject::invokeMethod(window,"focusInspector",Q_ARG(QVariant,1),Q_ARG(QVariant,false));QTest::qWait(300);
            QVERIFY(window->grabWindow().save(directory+"/audio-narrow-large-text.png"));
        }
        QCOMPARE(warnings.size(),0);
    }

    void captionDurationEscapeKeepsReviewHistoryAndAttachmentIdentity() {
        EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importFiles({fixture(),fixture()}));editor.addCaption();editor.selectClip(0);QVERIFY(editor.trimClip(0,.5,2));editor.selectCaption(0);QVERIFY(editor.project().captions[0].timingNeedsReview);
        auto *field=window->findChild<QQuickItem*>("captionDurationField");QVERIFY(field);const int frames=editor.project().captions[0].durationFrames;const auto history=editor.undoText();field->forceActiveFocus();QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);QTest::keyClick(window,'1');QTest::keyClick(window,Qt::Key_Escape);
        QCOMPARE(editor.project().captions[0].durationFrames,frames);QVERIFY(editor.project().captions[0].timingNeedsReview);QCOMPARE(editor.undoText(),history);
        editor.attachTimedText(true,1);const auto host=editor.project().captions[0].hostId;const auto attachedHistory=editor.undoText();field->forceActiveFocus();QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);QTest::keyClick(window,'2');QTest::keyClick(window,Qt::Key_Escape);QCOMPARE(editor.project().captions[0].hostId,host);QCOMPARE(editor.undoText(),attachedHistory);
    }
    void timedTextEndEdgesPreserveChosenCrossfadeHosts() {
        for(bool caption:{false,true}){
            EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importFiles({fixture(),fixture()}));editor.setCrossfade(.5);editor.seek(100);
            if(caption){editor.addCaption();editor.setCaptionTiming(0,100,20);}else{editor.addText();editor.setTextTiming(0,100,20);}editor.attachTimedText(caption,1);const auto host=editor.project().clips[1].id;
            auto *timeline=window->findChild<QuickTimeline*>("timeline");QVERIFY(timeline);QTest::qWait(60);const auto r=caption?timeline->captionRect(0):timeline->textRect(0);const auto edge=timeline->mapToScene({r.right()-2,r.center().y()}).toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::AltModifier,edge);QTest::mouseMove(window,edge+QPoint(30,0));QTest::mouseRelease(window,Qt::LeftButton,Qt::AltModifier,edge+QPoint(30,0));
            const auto &text=caption?editor.project().captions[0]:editor.project().textOverlays[0];QVERIFY(text.durationFrames>20);QCOMPARE(text.hostId,host);editor.undo();QCOMPARE((caption?editor.project().captions[0]:editor.project().textOverlays[0]).durationFrames,20);
        }
    }
    void captionVisualLayoutsAndExport() {
        const auto directory=qEnvironmentVariable("ISAAC_CAPTION_SCREENSHOTS");if(directory.isEmpty())return;QDir().mkpath(directory);
        EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);const auto source=qEnvironmentVariable("ISAAC_CAPTION_FOOTAGE",fixture());QVERIFY(editor.importFiles({source,source,source}));editor.addText();editor.updateText(0,{{"text","Adventure Park Day\nwith Isaac and Daddy"},{"durationFrames",90}});
        const auto input=directory+"/input.srt";QFile file(input);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QString::fromUtf8("1\n00:00:01,000 --> 00:00:03,000\n冒険の日\nIsaac and Daddy\n\n2\n00:00:02,000 --> 00:00:04,000\nمرحبا بالعالم\n").toUtf8());file.close();QVERIFY(editor.importSrt(input));editor.selectCaption(1);editor.styleCaptions({{"style","box"},{"y",.68}});editor.selectCaption(0);editor.seek(75);
        for(bool dark:{true,false}){setAppColorScheme(*qobject_cast<QApplication*>(qApp),dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);window->resize(1320,860);QTest::qWait(250);QVERIFY(window->grabWindow().save(directory+(dark?"/captions-dark.png":"/captions-light.png")));}
        editor.selectCaption(1,Qt::ControlModifier);editor.seek(150);QTest::qWait(200);QVERIFY(window->grabWindow().save(directory+"/captions-overlap.png"));
        editor.setBurnCaptions(false);editor.seek(75);QTest::qWait(200);QVERIFY(window->grabWindow().save(directory+"/captions-burn-off.png"));editor.setBurnCaptions(true);
        window->resize(760,560);auto font=qApp->font();font.setPointSize(24);window->setProperty("font",font);QMetaObject::invokeMethod(window,"focusInspector",Q_ARG(QVariant,5),Q_ARG(QVariant,false));QTest::qWait(200);QVERIFY(window->grabWindow().save(directory+"/captions-narrow-large.png"));
        QVERIFY(editor.saveTo(directory+"/caption-demo.isaac.json"));QVERIFY(editor.exportSrt(directory+"/caption-demo.srt"));QString error;QVERIFY2(Media::exportMp4(editor.project(),directory+"/caption-demo.mp4",{},&error),qPrintable(error));
    }
    void captionRowsUseDedicatedEditingAndPlaybackFocus() {
        EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importFiles({fixture()}));editor.addCaption();editor.seek(60);
        auto *list=window->findChild<QQuickItem*>("captionList");QVERIFY(list);QTRY_VERIFY(list->isVisible());
        QQuickItem *row=nullptr;QTRY_VERIFY(row=qvariant_cast<QQuickItem*>(list->property("currentItem")));row->forceActiveFocus();QTest::keyClick(window,Qt::Key_Return);
        auto *field=window->findChild<QQuickItem*>("captionContentField");QVERIFY(field);QTRY_VERIFY(field->hasActiveFocus());
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);for(char key:QByteArray("First"))QTest::keyClick(window,key);QTest::keyClick(window,Qt::Key_Return,Qt::ShiftModifier);for(char key:QByteArray("second"))QTest::keyClick(window,key);QTest::keyClick(window,Qt::Key_Space);QVERIFY(!editor.playing());QCOMPARE(editor.position(),60);
        QTest::keyClick(window,Qt::Key_Escape);QCOMPARE(editor.project().captions[0].text,QString("New caption"));
        row=qvariant_cast<QQuickItem*>(list->property("currentItem"));QVERIFY(row);QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,row->mapToScene({row->width()/2,row->height()/2}).toPoint());QTRY_VERIFY(field->hasActiveFocus());QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);for(char key:QByteArray("Committed"))QTest::keyClick(window,key);QTest::keyClick(window,Qt::Key_Return);QCOMPARE(editor.project().captions[0].text,QString("Committed"));editor.undo();QCOMPARE(editor.project().captions[0].text,QString("New caption"));
    }
    void productionInterfaceIsQuick() {
        EditorController editor; QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine,&QQmlEngine::warnings);
        QVERIFY(load(engine,editor));
        QCOMPARE(warnings.count(),0);
    }
    void softwareUpdateIsReachableFromTheMenu() {
        const auto previousVersion=QCoreApplication::applicationVersion();
        const auto restoreVersion=qScopeGuard([&]{QCoreApplication::setApplicationVersion(previousVersion);});
        QCoreApplication::setApplicationVersion(qEnvironmentVariable("ISAAC_UPDATE_INSTALLED_VERSION","0.1.0"));
        EditorController editor; QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine,&QQmlEngine::warnings);
        auto *window=load(engine,editor); QVERIFY(window);
        auto *menu=window->findChild<QObject*>("checkForUpdatesMenuItem"); QVERIFY(menu);
        auto *panel=window->findChild<QObject*>("softwareUpdateDialog"); QVERIFY(panel);
        auto *checker=window->findChild<UpdateChecker*>("updateChecker"); QVERIFY(checker);
        QCOMPARE(checker->state(),UpdateChecker::Idle);
        QVERIFY(!panel->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu,"triggered"));
        QVERIFY(checker->checking());
        QTRY_VERIFY(panel->property("visible").toBool());
        const auto captures=qEnvironmentVariable("ISAAC_UPDATE_SCREENSHOTS");
        if(!captures.isEmpty()) {
            QDir().mkpath(captures);
            QTRY_VERIFY_WITH_TIMEOUT(!checker->checking(),20000);
            qInfo().noquote()<<checker->message();
            for(bool dark:{true,false}) {
                setAppColorScheme(*qobject_cast<QApplication*>(qApp),dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);
                QTest::qWait(100);
                QVERIFY(window->grabWindow().save(captures+(dark?"/updates-dark.png":"/updates-light.png")));
            }
            window->resize(760,560); auto font=qApp->font(); font.setPointSize(24); window->setProperty("font",font);
            QTest::qWait(100); QVERIFY(window->grabWindow().save(captures+"/updates-large-text.png"));
        }
        QVERIFY(QMetaObject::invokeMethod(panel,"close"));
        QTRY_VERIFY(!panel->property("visible").toBool());
        QCOMPARE(warnings.count(),0);
    }
    void updateDialogDownloadsPreparesAndProtectsUnsavedWork() {
        const auto archive=UpdateTest::preparedArchiveFixture();
        if(archive.isEmpty())QSKIP("Native update archive fixture unavailable");
        const auto previousVersion=QCoreApplication::applicationVersion();
        const auto restoreVersion=qScopeGuard([&]{QCoreApplication::setApplicationVersion(previousVersion);});
        QCoreApplication::setApplicationVersion("1.2.0");
        UpdateTest::ReleaseNetwork network; network.body=UpdateTest::downloadableRelease(archive);
        UpdateChecker checker(network); QSignalSpy restart(&checker,&UpdateChecker::restartRequested);
        EditorController editor; QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine,&QQmlEngine::warnings);
        setEditorQmlContext(engine,editor);
        QQmlEngine::setObjectOwnership(&checker,QQmlEngine::CppOwnership);
        engine.rootContext()->setContextProperty("testUpdater",&checker);
        QFile source("qml/Main.qml"); QVERIFY(source.open(QIODevice::ReadOnly));
        auto qml=source.readAll();
        // Use the actual dialog with a deterministic network-backed service.
        const QByteArray declaration="UpdateChecker { id: updater; objectName: \"updateChecker\" }";
        QVERIFY(qml.contains(declaration));
        qml.replace(declaration,"property var updater: testUpdater");
        engine.loadData(qml,QUrl::fromLocalFile(QDir::current().filePath("qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *menu=window->findChild<QObject*>("checkForUpdatesMenuItem"); QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu,"triggered"));
        QTRY_VERIFY(checker.canDownload());
        auto *download=window->findChild<QQuickItem*>("downloadUpdateButton"); QVERIFY(download);
        auto *cancel=window->findChild<QQuickItem*>("cancelUpdateDownloadButton"); QVERIFY(cancel);
        auto *install=window->findChild<QQuickItem*>("installUpdateButton"); QVERIFY(install);
        auto *progress=window->findChild<QQuickItem*>("updateDownloadProgress"); QVERIFY(progress);
        QTRY_VERIFY(download->isVisible()); QVERIFY(!install->isVisible());
        const auto captures=qEnvironmentVariable("ISAAC_UPDATE_FLOW_SCREENSHOTS");
        const auto capture=[&](const QString &name){
            if(captures.isEmpty())return true;
            QDir().mkpath(captures);
            window->resize(1320,860);
            for(bool dark:{true,false}){
                setAppColorScheme(*qobject_cast<QApplication*>(qApp),dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);
                QTest::qWait(50);
                if(!window->grabWindow().save(captures+"/"+name+(dark?"-dark.png":"-light.png")))return false;
            }
            window->resize(760,560); auto font=qApp->font(); font.setPointSize(24); window->setProperty("font",font);
            QTest::qWait(50); const bool saved=window->grabWindow().save(captures+"/"+name+"-large.png");
            window->setProperty("font",qApp->font()); window->resize(1320,860);
            return saved;
        };
        QVERIFY(capture("available"));
        network.body=archive; network.delay=10000;
        QVERIFY(QMetaObject::invokeMethod(download,"clicked"));
        QCOMPARE(checker.state(),UpdateChecker::Downloading);
        QTRY_VERIFY(progress->isVisible()); QTRY_VERIFY(cancel->isVisible());
        QVERIFY(capture("downloading"));
        QVERIFY(QMetaObject::invokeMethod(cancel,"clicked"));
        QTRY_VERIFY(checker.canDownload());
        network.delay=0;
        QVERIFY(QMetaObject::invokeMethod(download,"clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(!checker.busy(),20000);
        QVERIFY2(checker.readyToInstall(),qPrintable(checker.message()));
        QTRY_VERIFY(install->isVisible()); QVERIFY(!progress->isVisible());
        QVERIFY(capture("ready"));
        QVERIFY(editor.importFiles({fixture()})); QVERIFY(editor.dirty());
        bool savePrompt=false; QTimer cancelSave;
        connect(&cancelSave,&QTimer::timeout,window,[&]{
            if(auto *message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){
                savePrompt=true; message->done(QMessageBox::Cancel);
            }
        });
        cancelSave.start(10);
        QVERIFY(QMetaObject::invokeMethod(install,"clicked"));
        cancelSave.stop();
        QVERIFY(savePrompt); QVERIFY(checker.readyToInstall()); QVERIFY(window->isVisible());
        QVERIFY(editor.dirty()); QCOMPARE(restart.count(),0); QCOMPARE(warnings.count(),0);
        auto *message=window->findChild<QQuickItem*>("softwareUpdateMessage"); QVERIFY(message);
        message->setProperty("text",QString("A long installation error with a folder path. ").repeated(100));
        window->resize(760,560); auto font=qApp->font(); font.setPointSize(24); window->setProperty("font",font);
        auto *panel=window->findChild<QObject*>("softwareUpdateDialog"); QVERIFY(panel);
        auto *scroll=window->findChild<QQuickItem*>("updateContentScroll"); QVERIFY(scroll);
        QTRY_VERIFY(panel->property("height").toReal()<=520);
        QTRY_VERIFY(scroll->property("contentHeight").toReal()>scroll->height());
        QCOMPARE(warnings.count(),0);
    }
    void compositionNumericEscapeRestoresValuesAndBindings(){
        EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importFiles({fixture()}));editor.selectClip(0);
        editor.setTransform({{"framing","crop"}});editor.editKeyframe("scale","add");QVERIFY(QMetaObject::invokeMethod(window,"focusInspector",Q_ARG(QVariant,4),Q_ARG(QVariant,false)));QTest::qWait(30);
        std::function<QQuickItem*(QQuickItem*,const QString&)> find=[&](QQuickItem *item,const QString &name)->QQuickItem*{if(item->objectName()==name)return item;for(auto *child:item->childItems())if(auto *found=find(child,name))return found;return nullptr;};
        const auto cancel=[&](const char *name,const QString &value){auto *field=find(window->contentItem(),QString::fromLatin1(name));if(!field)return false;field->forceActiveFocus();QMetaObject::invokeMethod(field,"selectAll");for(const auto c:value)QTest::keyClick(window,c==QChar('.')?Qt::Key_Period:Qt::Key(Qt::Key_0+c.digitValue()));QTest::keyClick(window,Qt::Key_Escape);auto *other=window->findChild<QQuickItem*>("canvasWidthField");if(other)other->forceActiveFocus();QCoreApplication::processEvents();return true;};
        const auto history=editor.undoText();
        QVERIFY(cancel("visualcropLeftField","0.2"));QCOMPARE(editor.project().clips[0].transform.cropLeft,0.);
        QVERIFY(cancel("movescaleKeyframeField","12"));QCOMPARE(editor.project().clips[0].transform.scale.keys[0].frame,0);QCOMPARE(editor.undoText(),history);
        editor.setTransform({{"cropLeft",.1}});QCoreApplication::processEvents();auto *crop=find(window->contentItem(),"visualcropLeftField");QVERIFY(crop);QCOMPARE(crop->property("text").toString(),QString("10.0"));
    }
    void canceledSourceScrubIgnoresRemainingPointerMovement(){
        QTemporaryDir dir;EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importMedia({fixture()}));editor.seekSource(12);QVERIFY(editor.saveTo(dir.filePath("source.isaac.json")));
        auto *slider=window->findChild<QQuickItem*>("sourcePositionSlider");QVERIFY(slider);QTest::qWait(30);
        const auto point=[&](double fraction){return slider->mapToScene({slider->width()*fraction,slider->height()/2}).toPoint();};
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point(.4));QTest::mouseMove(window,point(.7));QTest::keyClick(window,Qt::Key_Escape);
        QTest::mouseMove(window,point(.9));QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point(.9));
        QCOMPARE(editor.project().assets[0].cursorFrame,12);QVERIFY(!editor.dirty());
    }


    void recoveryToolsStayOutOfTheWorkspaceHeader() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        EditorController editor;editor.enableRecovery(directory.path());
        QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);
        QCOMPARE(editor.recoveryStatus(),QString("Recovery ready"));
        for(auto *item:window->findChildren<QQuickItem*>())
            if(item->property("text").toString()==editor.recoveryStatus())
                QVERIFY2(!item->isVisible(),"Recovery status should not reserve a permanent header row");
        auto *menu=window->findChild<QObject*>("recoveryMenuItem");QVERIFY(menu);
        auto *panel=window->findChild<QObject*>("recoveryPanel");QVERIFY(panel);
        auto *tools=window->findChild<QQuickItem*>("recoveryTools");QVERIFY(tools);
        QVERIFY(!panel->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu,"triggered"));
        QTRY_VERIFY(panel->property("visible").toBool());QVERIFY(tools->isVisible());
        auto *workspace=window->findChild<QQuickItem*>("workspaceScroll");QVERIFY(workspace);
        for(auto *parent=tools->parentItem();parent;parent=parent->parentItem())QVERIFY(parent!=workspace);
        QVERIFY(QMetaObject::invokeMethod(panel,"close"));
        QTRY_VERIFY(!tools->isVisible());
        const auto captures=qEnvironmentVariable("ISAAC_SCREENSHOT_DIR");
        if(!captures.isEmpty()) {QDir().mkpath(captures);window->requestUpdate();QTest::qWait(100);QVERIFY(window->grabWindow().save(captures+"/workspace-without-status-header.png"));}
    }
    void emptyPreviewOpensSavedJsonProject() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path=directory.filePath("saved-edit.json");
        Project project; project.clips={{fixture(),0.25,0.75}};
        QVERIFY(saveProject(project,path));
        const auto noNativeDialogs=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restoreDialogs=qScopeGuard([=]{QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,noNativeDialogs);});
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        auto *button=window->findChild<QQuickItem*>("openProjectButton"); QVERIFY(button);
        bool sawDialog=false; QStringList filters;
        QTimer timer,deadline; deadline.setSingleShot(true);
        connect(&deadline,&QTimer::timeout,window,[]{
            if(auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        connect(&timer,&QTimer::timeout,window,[&]{
            if(auto *dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                if(!sawDialog) {
                    sawDialog=true; filters=dialog->nameFilters();
                    if(!filters.join(';').contains("*.json")) { dialog->reject(); return; }
                    dialog->selectNameFilter("JSON files (*.json)");
                    dialog->setDirectory(directory.path()); dialog->selectFile(path);
                } else QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
            }
        });
        timer.start(10); deadline.start(5000);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,button->mapToScene(button->boundingRect().center()).toPoint());
        timer.stop(); deadline.stop();
        QVERIFY(sawDialog);
        QVERIFY2(filters.join(';').contains("*.json"),qPrintable("Preview opened the wrong file picker: "+filters.join(';')));
        QCOMPARE(editor.project().clips.size(),1);
        QCOMPARE(editor.project().clips[0].inSeconds,0.25);
        QCOMPARE(editor.project().clips[0].outSeconds,0.75);
        QVERIFY(window->title().contains("saved-edit.json"));
        QVERIFY(!editor.dirty()); QVERIFY(!button->isVisible());
    }
    void emptyPreviewProjectPromptTracksProjectAndSaveState() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY2(!qvariant_cast<QQuickItem*>(window->property("header")),"Project controls should not consume a header row");
        auto *button=window->findChild<QQuickItem*>("openProjectButton"); QVERIFY(button);
        auto *hint=window->findChild<QQuickItem*>("emptyImportHint"); QVERIFY(hint);
        auto *exportItem=window->findChild<QObject*>("exportMenuItem"); QVERIFY(exportItem);
        QVERIFY(button->isVisible()); QVERIFY(hint->isVisible());
        QCOMPARE(QAccessible::queryAccessibleInterface(button)->text(QAccessible::Name),QString("Open Project"));
        QVERIFY(window->title().contains("Untitled"));
        QVERIFY(editor.importFiles({fixture()}));
        QTRY_VERIFY(!button->isVisible()); QVERIFY(!hint->isVisible());
        QVERIFY(window->title().contains("Unsaved changes"));
        QVERIFY(exportItem->property("enabled").toBool());
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(editor.saveTo(directory.filePath("My edit.isaac.json")));
        QVERIFY(window->title().contains("My edit.isaac.json"));
        QVERIFY(!window->title().contains("Unsaved changes"));
        QVERIFY2(editor.notice().isEmpty(),"Save feedback should stay in the window title");
        QVERIFY(window->title().contains(" — Saved — "));
        editor.removeSelection();
        QTRY_VERIFY(button->isVisible()); QVERIFY(hint->isVisible());
        editor.undo(); QTRY_VERIFY(!button->isVisible());
        editor.newProject();
        QVERIFY(!window->title().contains(" — Saved — "));
        const auto captureDir=qEnvironmentVariable("ISAAC_SCREENSHOT_DIR");
        if(!captureDir.isEmpty()) {
            QDir().mkpath(captureDir);
            for(bool dark:{false,true}) {
                setAppColorScheme(*qobject_cast<QApplication*>(qApp),dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);
                window->requestUpdate();QTest::qWait(100);
                QVERIFY(window->grabWindow().save(captureDir+(dark?"/empty-preview-dark.png":"/empty-preview-light.png")));
            }
        }
        window->resize(760,560);auto font=qApp->font();font.setPointSize(24);window->setProperty("font",font);
        auto *viewer=window->findChild<QQuickItem*>("viewer");QVERIFY(viewer);
        QTRY_VERIFY(viewer->boundingRect().adjusted(-1,-1,1,1).contains(button->mapRectToItem(viewer,button->boundingRect())));
        QTRY_VERIFY(viewer->boundingRect().adjusted(-1,-1,1,1).contains(hint->mapRectToItem(viewer,hint->boundingRect())));
        if(!captureDir.isEmpty()) {
            window->requestUpdate();QTest::qWait(100);
            QVERIFY(window->grabWindow().save(captureDir+"/empty-preview-large-text.png"));
        }
    }
    void primaryActionTextRemainsReadable() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        auto *button=window->findChild<QQuickItem*>("openProjectButton"); QVERIFY(button);
        auto *label=qvariant_cast<QQuickItem*>(button->property("contentItem")); QVERIFY(label);
        auto *background=qvariant_cast<QQuickItem*>(button->property("background")); QVERIFY(background);
        const auto luminance=[](const QColor &color) {
            const auto linear=[](double v) { return v<=0.04045?v/12.92:std::pow((v+0.055)/1.055,2.4); };
            return 0.2126*linear(color.redF())+0.7152*linear(color.greenF())+0.0722*linear(color.blueF());
        };
        for (auto scheme : {Qt::ColorScheme::Light,Qt::ColorScheme::Dark}) {
            setAppColorScheme(*qobject_cast<QApplication*>(qApp),scheme);
            for (bool down : {false,true}) {
                QVERIFY(button->setProperty("down",down));
                QCoreApplication::processEvents();
                const auto foreground=label->property("color").value<QColor>();
                const auto fill=background->property("color").value<QColor>();
                const auto a=luminance(foreground), b=luminance(fill);
                const auto contrast=(std::max(a,b)+0.05)/(std::min(a,b)+0.05);
                QVERIFY2(contrast>=4.5,qPrintable(QString("Primary action text %1 on %2 has contrast %3 (dark=%4, down=%5)").arg(foreground.name(),fill.name()).arg(contrast).arg(scheme==Qt::ColorScheme::Dark).arg(down)));
            }
        }
    }
    void dropdownTextRemainsReadable_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("objectModel");
        QTest::newRow("light-strings") << false << false;
        QTest::newRow("dark-strings") << true << false;
        QTest::newRow("light-objects") << false << true;
        QTest::newRow("dark-objects") << true << true;
    }
    void dropdownTextRemainsReadable() {
        QFETCH(bool,dark); QFETCH(bool,objectModel);
        QTest::failOnWarning(QRegularExpression(".*"));
        setAppColorScheme(*qobject_cast<QApplication*>(qApp),dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);
        EditorController editor; QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine,&QQmlEngine::warnings);
        auto *window=load(engine,editor); QVERIFY(window);
        auto *combo=window->findChild<QQuickItem*>("mediaTypeFilter"); QVERIFY(combo);
        if(objectModel) {
            QVERIFY(combo->setProperty("textRole","text"));
            QVERIFY(combo->setProperty("model",QVariantList{QVariantMap{{"text","First"}},QVariantMap{{"text","Second"}}}));
        }
        const auto luminance=[](const QColor &color) {
            const auto linear=[](double v) { return v<=0.04045?v/12.92:std::pow((v+0.055)/1.055,2.4); };
            return 0.2126*linear(color.redF())+0.7152*linear(color.greenF())+0.0722*linear(color.blueF());
        };
        const auto contrast=[&](QQuickItem *label,QQuickItem *background) {
            const auto foreground=label->property("color").value<QColor>();
            const auto fill=background->property("color").value<QColor>();
            const auto a=luminance(foreground),b=luminance(fill);
            return (std::max(a,b)+0.05)/(std::min(a,b)+0.05);
        };
        auto *closedLabel=qvariant_cast<QQuickItem*>(combo->property("contentItem")); QVERIFY(closedLabel);
        auto *closedBackground=qvariant_cast<QQuickItem*>(combo->property("background")); QVERIFY(closedBackground);
        QVERIFY(contrast(closedLabel,closedBackground)>=4.5);
        auto *indicator=qvariant_cast<QQuickItem*>(combo->property("indicator")); QVERIFY(indicator);
        QVERIFY2(contrast(indicator,closedBackground)>=3.0,"The dropdown arrow must be visible in both themes");
        combo->forceActiveFocus(Qt::TabFocusReason);
        auto *popup=qvariant_cast<QObject*>(combo->property("popup")); QVERIFY(popup);
        QVERIFY(QMetaObject::invokeMethod(popup,"open"));
        QTRY_VERIFY(popup->property("opened").toBool());
        auto *list=qvariant_cast<QQuickItem*>(popup->property("contentItem")); QVERIFY(list);
        QTRY_VERIFY(qvariant_cast<QQuickItem*>(list->property("currentItem")));
        auto *firstRow=qvariant_cast<QQuickItem*>(list->property("currentItem"));
        for(int index=0;index<2;++index) {
            auto *row=qvariant_cast<QQuickItem*>(list->property("currentItem")); QVERIFY(row);
            QTRY_VERIFY(row->property("highlighted").toBool());
            QCOMPARE(row->property("text").toString(),objectModel?(index?"Second":"First"):(index?"Video":"All types"));
            auto *label=qvariant_cast<QQuickItem*>(row->property("contentItem")); QVERIFY(label);
            auto *background=qvariant_cast<QQuickItem*>(row->property("background")); QVERIFY(background);
            for(bool down:{false,true}) {
                QVERIFY(row->setProperty("down",down));
                QCoreApplication::processEvents();
                QVERIFY2(contrast(label,background)>=4.5,qPrintable(QString("Dropdown text %1 on %2 has contrast %3 (dark=%4, down=%5)")
                    .arg(label->property("color").value<QColor>().name(),background->property("color").value<QColor>().name())
                    .arg(contrast(label,background)).arg(dark).arg(down)));
            }
            QVERIFY(row->setProperty("down",false));
            if(index==0) QTest::keyClick(window,Qt::Key_Down);
        }
        QVERIFY(!firstRow->property("highlighted").toBool());
        QVERIFY(contrast(qvariant_cast<QQuickItem*>(firstRow->property("contentItem")),qvariant_cast<QQuickItem*>(firstRow->property("background")))>=4.5);
        const auto captureDir=qEnvironmentVariable("ISAAC_DROPDOWN_SCREENSHOTS");
        if(!captureDir.isEmpty()&&!objectModel) {
            QDir().mkpath(captureDir); window->requestUpdate(); QTest::qWait(100);
            QVERIFY(window->grabWindow().save(captureDir+(dark?"/dropdown-dark.png":"/dropdown-light.png")));
        }
        QTest::keyClick(window,Qt::Key_Return);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(combo->property("currentIndex").toInt(),1);
        // The dropdown fix must not change the accent selection colors used by text fields.
        const auto selection=closedLabel->property("selectionColor").value<QColor>();
        const auto selectedText=closedLabel->property("selectedTextColor").value<QColor>();
        const auto a=luminance(selection),b=luminance(selectedText);
        QVERIFY((std::max(a,b)+0.05)/(std::min(a,b)+0.05)>=4.5);
        auto *menuItem=window->findChild<QQuickItem*>("recoveryMenuItem"); QVERIFY(menuItem);
        auto *menuLabel=qvariant_cast<QQuickItem*>(menuItem->property("contentItem")); QVERIFY(menuLabel);
        auto *menuBackground=qvariant_cast<QQuickItem*>(menuItem->property("background")); QVERIFY(menuBackground);
        QVERIFY(menuItem->setProperty("highlighted",true));
        for(bool down:{false,true}) {
            QVERIFY(menuItem->setProperty("down",down)); QCoreApplication::processEvents();
            QVERIFY2(contrast(menuLabel,menuBackground)>=4.5,qPrintable(QString("Menu text %1 on %2 has contrast %3 (down=%4)")
                .arg(menuLabel->property("color").value<QColor>().name(),menuBackground->property("color").value<QColor>().name())
                .arg(contrast(menuLabel,menuBackground)).arg(down)));
        }
        QCOMPARE(warnings.size(),0);
    }
    void iconControlsStayCompactNamedAndKeyboardOperable() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        auto *play=window->findChild<QQuickItem*>("playButton"); QVERIFY(play);
        QVERIFY2(play->property("iconOnly").toBool(),"Playback should be an icon-only control");
        QVERIFY(play->implicitWidth()<76);
        QVERIFY(editor.importFiles({fixture()}));
        auto *undo=window->findChild<QQuickItem*>("undoButton"); QVERIFY(undo);
        auto *snap=window->findChild<QQuickItem*>("snapButton"); QVERIFY(snap);
        auto *timeline=window->findChild<QuickTimeline*>("timeline"); QVERIFY(timeline);
        for(auto *button:{play,undo,snap}) {
            auto *accessible=QAccessible::queryAccessibleInterface(button); QVERIFY(accessible);
            QVERIFY(!accessible->text(QAccessible::Name).isEmpty());
            QVERIFY(!button->property("iconName").toString().isEmpty());
        }
        undo->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(undo->property("visualFocus").toBool());
        QVERIFY(!undo->property("helpText").toString().isEmpty());
        snap->forceActiveFocus(Qt::TabFocusReason); QTRY_VERIFY(snap->hasActiveFocus());
        QTest::keyClick(window,Qt::Key_Space);
        QVERIFY(!timeline->snapping());
        QTest::keyClick(window,Qt::Key_Space); QVERIFY(timeline->snapping());
        undo->forceActiveFocus(Qt::TabFocusReason); QTest::keyClick(window,Qt::Key_Space);
        QCOMPARE(editor.clips().size(),0);
    }
    void clipInspectorDisclosesOnlyWithSelection() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        auto *empty=window->findChild<QQuickItem*>("clipInspectorEmpty"); QVERIFY(empty);
        auto *start=window->findChild<QQuickItem*>("trimStartField"); QVERIFY(start);
        QVERIFY(empty->isVisible()); QVERIFY(!start->isVisible());
        QVERIFY(editor.importFiles({fixture()}));
        QTRY_VERIFY(start->isVisible()); QVERIFY(!empty->isVisible());
        start->forceActiveFocus(Qt::TabFocusReason); QTRY_VERIFY(start->hasActiveFocus()); QTest::keyClick(window,Qt::Key_Up);
        QVERIFY(std::abs(editor.currentClip().value("inSeconds").toDouble()-1.0/60)<0.001);
        editor.clearSelection(); QTRY_VERIFY(!start->isVisible());
        QVERIFY(QMetaObject::invokeMethod(window,"focusInspector",Q_ARG(QVariant,QVariant(0)),Q_ARG(QVariant,QVariant(true))));
        QTRY_VERIFY(!window->property("textEditing").toBool());
    }
    void textFormattingControlsRetainStateAndContext() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window); QVERIFY(editor.importFiles({fixture()}));
        editor.addText();
        auto *bold=window->findChild<QQuickItem*>("textBoldButton"); QVERIFY(bold);
        auto *color=window->findChild<QQuickItem*>("textColorButton"); QVERIFY(color);
        auto *remove=window->findChild<QQuickItem*>("deleteTextButton"); QVERIFY(remove);
        QVERIFY(remove->isVisible());
        QVERIFY(remove->parentItem()!=window->findChild<QQuickItem*>("timelineTools"));
        QVERIFY(bold->property("checkable").toBool());
        const bool original=editor.currentText().value("bold").toBool();
        bold->forceActiveFocus(Qt::TabFocusReason); QTRY_VERIFY(bold->hasActiveFocus()); QTest::keyClick(window,Qt::Key_Space);
        QCOMPARE(editor.currentText().value("bold").toBool(),!original);
        QCOMPARE(bold->property("checked").toBool(),!original);
        QVERIFY(!QAccessible::queryAccessibleInterface(color)->text(QAccessible::Name).isEmpty());
        remove->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Space);
        QCOMPARE(editor.textOverlays().size(),0);
        QVERIFY(!remove->isVisible()); editor.undo(); QCOMPARE(editor.textOverlays().size(),1);
    }
    void largeTextFocusKeepsInspectorAndToolbarActionsVisible() {
        EditorController editor;QQmlApplicationEngine engine;
        auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importFiles({fixture()}));
        window->resize(760,560);auto font=qApp->font();font.setPointSize(24);window->setProperty("font",font);
        QTest::qWait(30);
        const auto fullyVisible=[](QQuickItem *item){
            for(auto *parent=item->parentItem();parent;parent=parent->parentItem()) {
                if(!parent->clip())continue;
                const auto rect=item->mapRectToItem(parent,item->boundingRect());
                if(!parent->boundingRect().adjusted(-1,-1,1,1).contains(rect))return false;
            }
            return true;
        };
        QVERIFY(QMetaObject::invokeMethod(window,"focusInspector",Q_ARG(QVariant,QVariant(1)),Q_ARG(QVariant,QVariant(true))));
        auto *choose=window->findChild<QQuickItem*>("audioSourceChoice");
        QVERIFY(choose);QTRY_VERIFY(choose->hasActiveFocus());
        window->requestUpdate();QTest::qWait(100);
        QTRY_VERIFY2(fullyVisible(choose),"Audio import must fit the compact inspector");
        for(auto *item:window->findChildren<QQuickItem*>()) {
            const auto text=item->property("text").toString();
            if(item->isVisible() && (text=="Audio" || text=="Fades") && item->property("truncated").isValid())
                QTRY_VERIFY2(!item->property("truncated").toBool(),qPrintable("Inspector tab label clipped: "+text));
        }
        QQuickItem *timeLabel=nullptr;
        for(auto *item:window->findChildren<QQuickItem*>())
            if(item->property("text").toString()==editor.timeText()) {timeLabel=item;break;}
        QVERIFY(timeLabel);
        QTRY_VERIFY2(fullyVisible(timeLabel),"Large timecode must wrap inside the narrow viewer");
        const auto directory=qEnvironmentVariable("ISAAC_SCREENSHOT_DIR");
        if(!directory.isEmpty()) { window->requestUpdate();QTest::qWait(100);QVERIFY(window->grabWindow().save(directory+"/compact-audio.png")); }
        editor.addText();QTest::qWait(30);
        auto *bold=window->findChild<QQuickItem*>("textBoldButton");QVERIFY(bold);
        bold->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY2(fullyVisible(bold),"Keyboard focus must reveal lower text controls");
        if(!directory.isEmpty()) { window->requestUpdate();QTest::qWait(100);QVERIFY(window->grabWindow().save(directory+"/compact-text.png")); }
        auto *fit=window->findChild<QQuickItem*>("fitTimelineButton");
        QVERIFY(fit);fit->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY2(fullyVisible(fit),"Keyboard focus must reveal wrapped timeline tools");
        if(!directory.isEmpty()) { window->requestUpdate();QTest::qWait(100);QVERIFY(window->grabWindow().save(directory+"/compact-toolbar.png")); }
    }
    void textButtonsCreateAndDeleteThroughInspector() {
        EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importFiles({fixture()}));
        auto *add=window->findChild<QQuickItem*>("addTextButton");QVERIFY(add);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,add->mapToScene({add->width()/2,add->height()/2}).toPoint());
        QCOMPARE(editor.textOverlays().size(),1);QCOMPARE(editor.selectedText(),0);
        auto *field=window->findChild<QQuickItem*>("textContentField");QVERIFY(field);field->forceActiveFocus();QTest::keyClick(window,Qt::Key_Space);QVERIFY(!editor.playing());QTest::keyClick(window,Qt::Key_Delete);QCOMPARE(editor.textOverlays().size(),1);
        auto *remove=window->findChild<QQuickItem*>("deleteTextButton");QVERIFY(remove);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,remove->mapToScene({remove->width()/2,remove->height()/2}).toPoint());
        QCOMPARE(editor.textOverlays().size(),0);editor.undo();QCOMPARE(editor.textOverlays().size(),1);
        QVERIFY(!window->findChild<QQuickItem*>("positionSlider"));
    }
    void escapeKeepsInspectorBindingsAcrossSelection() {
        EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importFiles({fixture()}));
        editor.addText();editor.updateText(0,{{"text","FIRST"},{"fontSize",45},{"durationFrames",60}});
        editor.addText();editor.updateText(1,{{"text","SECOND"},{"fontSize",100},{"durationFrames",90}});editor.selectText(0);
        const auto check=[&](const char *name,const QString &expected){
            auto *field=window->findChild<QQuickItem*>(name);if(!field)return false;
            field->forceActiveFocus();QTest::keyClick(window,Qt::Key_Escape);editor.selectText(1);QCoreApplication::processEvents();
            const bool correct=field->property("text").toString()==expected;editor.selectText(0);return correct;
        };
        QVERIFY(check("textContentField","SECOND"));
        QVERIFY(check("textSizeField","100"));
        QVERIFY(check("textDurationField","1.500"));
    }
    void textGestureCancelAndUndoRestorePreviousSelection() {
        EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importFiles({fixture()}));editor.addText();editor.setTextTiming(0,30,60);editor.selectClip(0);
        auto *timeline=window->findChild<QuickTimeline*>("timeline");QVERIFY(timeline);QTest::qWait(50);
        const auto center=timeline->mapToScene(timeline->textRect(0).center()).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,center);QTest::mouseMove(window,center+QPoint(40,0));QTest::keyClick(window,Qt::Key_Escape);QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,center+QPoint(40,0));
        QCOMPARE(editor.selectedText(),-1);QCOMPARE(editor.selectedClip(),0);
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,center);QTest::mouseMove(window,center-QPoint(40,0));QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,center-QPoint(40,0));
        editor.undo();QCOMPARE(editor.selectedText(),-1);QCOMPARE(editor.selectedClip(),0);
    }
    void textOverlayVisualLayouts() {
        EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);
        const auto source=qEnvironmentVariable("ISAAC_OVERLAY_FOOTAGE",fixture());QVERIFY(editor.importFiles({source,source,source}));
        editor.addText();editor.updateText(0,{{"text","Adventure Park Day\nwith Isaac and Daddy"},{"durationFrames",120}});
        editor.seek(180);editor.addText();editor.updateText(1,{{"text","Adventure Park\nMain entrance"},{"style","box"},{"x",.24},{"y",.77}});editor.selectText(0);
        auto *field=window->findChild<QQuickItem*>("textSizeField");QVERIFY(field);QVERIFY(field->isVisible());
        const auto directory=qEnvironmentVariable("ISAAC_OVERLAY_SCREENSHOTS");if(directory.isEmpty())return;QDir().mkpath(directory);
        for(bool dark:{true,false}){
            setAppColorScheme(*qobject_cast<QApplication*>(qApp),dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);window->resize(1320,860);window->requestUpdate();QTest::qWait(500);
            QVERIFY(window->grabWindow().save(directory+(dark?"/text-overlays-dark.png":"/text-overlays-light.png")));
        }
        setAppColorScheme(*qobject_cast<QApplication*>(qApp),Qt::ColorScheme::Dark);editor.selectText(1);QTest::qWait(400);
        QVERIFY(window->grabWindow().save(directory+"/text-overlays-place.png"));
        window->resize(760,560);auto font=qApp->font();font.setPointSize(24);window->setProperty("font",font);QTest::qWait(400);
        auto *timeline=window->findChild<QuickTimeline*>("timeline");QVERIFY(timeline);QVERIFY(timeline->height()>=timeline->implicitHeight());
        QVERIFY(window->grabWindow().save(directory+"/text-overlays-large-text.png"));
        editor.selectText(0);QVERIFY(editor.saveTo(directory+"/text-overlay-demo.isaac.json"));
        QString error;QVERIFY2(Media::exportMp4(editor.project(),directory+"/text-overlay-demo.mp4",{},&error),qPrintable(error));
    }
    void textEntryAndPlaybackKeysHaveSeparateFocus() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture()}));
        auto *field=window->findChild<QQuickItem*>("trimStartField"); QVERIFY(field);
        field->forceActiveFocus();
        QTRY_VERIFY(window->property("textEditing").toBool());
        QTest::keyClick(window,Qt::Key_Space);
        QVERIFY(!editor.playing());
        QTest::keyClick(window,Qt::Key_B,Qt::ControlModifier);
        QCOMPARE(editor.clips().size(),1);
        auto *slider=window->findChild<QuickTimeline*>("timeline"); QVERIFY(slider);
        slider->forceActiveFocus();
        QTest::keyClick(window,Qt::Key_Right); QCOMPARE(editor.position(),1);
        QTest::keyClick(window,Qt::Key_PageUp); QCOMPARE(editor.position(),61);
    }
    void quickGesturesCommitThroughController() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture(),fixture()})); editor.seek(30);
        auto *timeline=window->findChild<QuickTimeline*>("timeline"); QVERIFY(timeline);
        QTRY_VERIFY(timeline->width()>500);
        auto center=timeline->mapToScene(timeline->clipRect(0).center()).toPoint();
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center);
        QCOMPARE(editor.selectedIndices(),QVariantList({QVariant(0)})); QCOMPARE(editor.position(),30);
        auto edge=timeline->mapToScene(QPointF(timeline->clipRect(0).right()-3,timeline->clipRect(0).center().y())).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,edge);
        QTest::mouseMove(window,edge-QPoint(40,0));
        QTest::keyClick(window,Qt::Key_Escape);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,edge-QPoint(40,0));
        QCOMPARE(editor.project().clips[0].outSeconds,2.0);
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,edge);
        QTest::mouseMove(window,edge-QPoint(50,0));
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,edge-QPoint(50,0));
        QVERIFY(editor.project().clips[0].outSeconds<2.0);
        editor.undo(); QCOMPARE(editor.project().clips[0].outSeconds,2.0);
    }
    void frontTrimCommitsComparedFrame_data() {
        QTest::addColumn<int>("clip");
        QTest::newRow("first-clip") << 0;
        QTest::newRow("after-cut") << 1;
    }
    void frontTrimCommitsComparedFrame() {
        QFETCH(int, clip);
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        const auto path=QDir::current().filePath("tests/fixtures/proxy-timing-30.mp4");
        QVERIFY(editor.importFiles({path,path}));
        editor.seek(clip * 24);
        auto *timeline=window->findChild<QuickTimeline*>("timeline"); QVERIFY(timeline);
        QTRY_VERIFY(timeline->width()>500);
        timeline->setSnapping(false);
        auto *incoming=window->findChild<QQuickItem*>("trimIncomingFrame"); QVERIFY(incoming);
        const auto rect=timeline->clipRect(clip);
        const auto from=timeline->mapToScene(QPointF(rect.left()+3,rect.top()+5)).toPoint();
        const auto to=from+QPoint(qRound(13*timeline->pixelsPerSecond()/60),0);
        QSignalSpy trims(timeline,&QuickTimeline::trimRequested);
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,from);
        QTest::mouseMove(window,to);
        QVERIFY(timeline->trimPreview().value("active").toBool());
        QCOMPARE(timeline->trimPreview().value("inTime").toDouble(),13/60.0);
        QTRY_VERIFY_WITH_TIMEOUT(incoming->property("ready").toBool(),5000);
        QCOMPARE(incoming->property("seconds").toDouble(),13/60.0);
        QCOMPARE(editor.project().clips[clip].inSeconds,0.0);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,to);
        QCOMPARE(trims.count(),1);
        QCOMPARE(editor.project().clips[clip].inSeconds,13/60.0);
        QCOMPARE(editor.project().clips[clip].outSeconds,.4);
        QCOMPARE(editor.duration(),35);
        const auto frameId=[&]{
            const auto image=editor.frame(); if(image.isNull()) return -1;
            int id=0;
            for(int bit=0;bit<12;++bit)if(image.pixelColor((bit*8+4)*image.width()/128,image.height()/2).red()>127)id|=1<<bit;
            return id;
        };
        QTRY_COMPARE_WITH_TIMEOUT(frameId(),7,3000);
        editor.undo(); QCOMPARE(editor.project().clips[clip].inSeconds,0.0); QCOMPARE(editor.duration(),48);
        editor.redo(); QCOMPARE(editor.project().clips[clip].inSeconds,13/60.0); QCOMPARE(editor.duration(),35);
        QTRY_COMPARE_WITH_TIMEOUT(frameId(),7,3000);
    }
    void trimComparisonLoadsBeforeRelease() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window); QVERIFY(editor.importFiles({fixture(),fixture()}));
        auto *timeline=window->findChild<QuickTimeline*>("timeline"); QVERIFY(timeline);
        QTRY_VERIFY(timeline->width()>500);
        auto *out=window->findChild<QQuickItem*>("trimOutgoingFrame"); QVERIFY(out);
        auto *in=window->findChild<QQuickItem*>("trimIncomingFrame"); QVERIFY(in);
        const auto edge=timeline->mapToScene(QPointF(timeline->clipRect(0).right()-3,timeline->clipRect(0).center().y())).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,edge);
        QTest::mouseMove(window,edge-QPoint(63,0));
        QVERIFY(timeline->trimPreview().value("active").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(out->property("ready").toBool()&&in->property("ready").toBool(),5000);
        QCOMPARE(editor.project().clips[0].outSeconds,2.0);
        QTest::keyClick(window,Qt::Key_Escape);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,edge-QPoint(63,0));
    }
    void rulerCancellationRestoresPlaybackAndLostGrab() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window); QVERIFY(editor.importFiles({fixture()}));
        editor.seek(30); editor.togglePlayback(); QVERIFY(editor.playing());
        auto *slider=window->findChild<QuickTimeline*>("timeline"); QVERIFY(slider);
        const auto point=slider->mapToScene(QPointF(slider->width()*0.7,10)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point);
        QTest::mouseMove(window,point+QPoint(10,0));
        QTest::keyClick(window,Qt::Key_Escape);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point+QPoint(10,0));
        QCOMPARE(editor.position(),30);
        QTRY_VERIFY_WITH_TIMEOUT(editor.playing(),2000);
        editor.togglePlayback(); editor.seek(20);
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point);
        auto *grabber=window->mouseGrabberItem(); QVERIFY(grabber); grabber->ungrabMouse();
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point);
        QCOMPARE(editor.position(),20);
    }
    void rulerEscapeRestoresPosition() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture()})); editor.seek(30);
        auto *slider=window->findChild<QuickTimeline*>("timeline"); QVERIFY(slider);
        auto start=slider->mapToScene(QPointF(slider->width()*0.75,10)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,start+QPoint(10,0));
        QVERIFY(editor.position()!=30);
        QTest::keyClick(window,Qt::Key_Escape);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(15,0));
        QCOMPARE(editor.position(),30);
    }
    void keyboardClipSelectionStaysVisible() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        QStringList paths; for(int i=0;i<40;++i) paths.append(fixture());
        QVERIFY(editor.importFiles(paths)); editor.selectClip(0);
        auto *list=window->findChild<QQuickItem*>("clipList"); QVERIFY(list); list->forceActiveFocus();
        for(int i=0;i<30;++i) QTest::keyClick(window,Qt::Key_Down);
        QCOMPARE(editor.selectedClip(),30);
        QTRY_COMPARE(list->property("currentIndex").toInt(),30);
        auto *item=qvariant_cast<QQuickItem*>(list->property("currentItem")); QVERIFY(item);
        const auto rect=item->mapRectToItem(list,item->boundingRect());
        QVERIFY(rect.top()>=0&&rect.bottom()<=list->height()+1);
    }
    void numericEscapeRestoresValueAndInspectorFocus() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window); QVERIFY(editor.importFiles({fixture()}));
        QTest::keyClick(window,Qt::Key_4,Qt::ControlModifier);
        auto *field=window->findChild<QQuickItem*>("trimStartField"); QVERIFY(field);
        QTRY_VERIFY(field->hasActiveFocus());
        field->setProperty("text","0.5"); QTest::keyClick(window,Qt::Key_Escape);
        QCOMPARE(editor.project().clips[0].inSeconds,0.0);
        QCOMPARE(field->property("text").toString(),QString("0.000"));
    }
    void mediaDropsImportWithoutChangingSequence() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(fixture())});
        QDragEnterEvent enter(QPointF(100,150),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&enter);
        QDropEvent drop(QPointF(100,150),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&drop);
        QVERIFY(drop.isAccepted()); QCOMPARE(editor.clips().size(),0); QCOMPARE(editor.assets().size(),1);
    }
    void sourceRangeButtonsKeyboardAndTimelineDrop() {
        EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);
        QVERIFY(editor.importMedia({fixture()}));
        auto *list=window->findChild<QQuickItem*>("mediaList");QVERIFY(list);list->forceActiveFocus();
        editor.seekSource(12);QTest::keyClick(window,Qt::Key_I);editor.seekSource(29);QTest::keyClick(window,Qt::Key_O);
        QCOMPARE(editor.currentSource()["rangeFrames"].toInt(),18);
        auto *append=window->findChild<QQuickItem*>("appendSourceButton");QVERIFY(append);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,append->mapToScene({append->width()/2,append->height()/2}).toPoint());
        QCOMPARE(editor.duration(),18);QVERIFY(editor.sourceMode());
        auto *timeline=window->findChild<QuickTimeline*>("timeline");QVERIFY(timeline);
        QMimeData mime;mime.setUrls({QUrl::fromLocalFile(fixture())});
        const auto point=timeline->mapToScene(QPointF(timeline->clipRect(0).right(),timeline->clipRect(0).center().y()));
        QDragEnterEvent enter(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&enter);
        QDragMoveEvent move(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&move);
        QVERIFY(timeline->gestureText().contains("Result"));QCOMPARE(editor.duration(),18);
        QDropEvent drop(point,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&drop);
        QVERIFY(drop.isAccepted());QCOMPARE(editor.duration(),36);editor.undo();QCOMPARE(editor.duration(),18);
        auto *search=window->findChild<QQuickItem*>("mediaSearch");QVERIFY(search);search->forceActiveFocus();
        QTest::keyClick(window,Qt::Key_I);QCOMPARE(editor.currentSource()["inFrame"].toInt(),12);
    }
    void sourceBrowserLayoutsAndFocus() {
        EditorController editor;QQmlApplicationEngine engine;auto *window=load(engine,editor);QVERIFY(window);QVERIFY(editor.importMedia({fixture()}));
        auto *append=window->findChild<QQuickItem*>("appendSourceButton");QVERIFY(append);
        auto *insert=window->findChild<QQuickItem*>("insertSourceButton");QVERIFY(insert);QVERIFY(insert->isEnabled());
        const auto directory=qEnvironmentVariable("ISAAC_SOURCE_SCREENSHOTS");if(!directory.isEmpty())QDir().mkpath(directory);
        for(bool dark:{true,false}) {
            setAppColorScheme(*qobject_cast<QApplication*>(qApp),dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);
            window->resize(1320,860);window->requestUpdate();QTest::qWait(250);
            QVERIFY(append->isVisible());QVERIFY(append->width()>20);
            if(!directory.isEmpty())QVERIFY(window->grabWindow().save(directory+(dark?"/source-dark.png":"/source-light.png")));
        }
        window->resize(760,560);auto font=qApp->font();font.setPointSize(24);window->setProperty("font",font);QTest::qWait(250);
        auto *type=window->findChild<QQuickItem*>("mediaTypeFilter");QVERIFY(type);QVERIFY(type->width()>120);
        auto *selector=window->findChild<QQuickItem*>("compactBrowserSelector");QVERIFY(selector);QVERIFY(selector->isVisible());
        QVERIFY(append->isVisible());auto *viewer=window->findChild<QQuickItem*>("viewer");QVERIFY(viewer);QVERIFY(viewer->height()>80);
        auto *timeline=window->findChild<QuickTimeline*>("timeline");QVERIFY(timeline);QVERIFY(timeline->height()>=timeline->implicitHeight());
        if(!directory.isEmpty())QVERIFY(window->grabWindow().save(directory+"/source-large-text.png"));
    }
    void largeTextAndShortWindowKeepAllLanesReachable() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        window->resize(760,560);
        auto font=qApp->font(); font.setPointSize(24); window->setProperty("font",font);
        auto *timeline=window->findChild<QuickTimeline*>("timeline"); QVERIFY(timeline);
        QTRY_VERIFY(timeline->height()>=timeline->implicitHeight());
        QVERIFY(window->findChild<QQuickItem*>("workspaceScroll"));
    }
    void accessibleAlternativesAndVisualLayouts() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({QDir::current().filePath("tests/fixtures/rotation.mp4"),fixture(),fixture()}));
        const auto audio=qEnvironmentVariable("ISAAC_SCREENSHOT_AUDIO",fixture());
        editor.setVoicePath(audio); editor.setMusicPath(audio);
        auto *slider=window->findChild<QuickTimeline*>("timeline"); QVERIFY(slider);
        auto *accessible=QAccessible::queryAccessibleInterface(slider);
        QVERIFY(accessible); QVERIFY(!accessible->text(QAccessible::Name).isEmpty());
        const auto directory=qEnvironmentVariable("ISAAC_SCREENSHOT_DIR");
        if(directory.isEmpty()) return;
        QDir().mkpath(directory);
        auto *timeline=window->findChild<QuickTimeline*>("timeline"); QVERIFY(timeline);
        for(bool dark:{true,false}) {
            setAppColorScheme(*qobject_cast<QApplication*>(qApp),dark?Qt::ColorScheme::Dark:Qt::ColorScheme::Light);
            window->resize(1320,860);
            window->requestUpdate();
            QTRY_VERIFY_WITH_TIMEOUT(!window->grabWindow().isNull(),3000);
            QTest::qWait(800);
            QVERIFY(window->grabWindow().save(directory+(dark?"/editor-qml-dark.png":"/editor-qml-light.png")));
        }
        window->resize(760,560); auto font=qApp->font(); font.setPointSize(24); window->setProperty("font",font);
        window->requestUpdate(); QTest::qWait(300);
        QVERIFY(window->grabWindow().save(directory+"/editor-qml-large-text.png"));
    }
};
QTEST_MAIN(QmlShellTest)
#include "qml_shell_test.moc"
