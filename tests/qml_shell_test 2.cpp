#include "editor_controller.h"
#include "qml_registration.h"
#include "mlt_engine.h"
#include "quick_timeline.h"
#include "theme.h"
#include <QQuickStyle>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QStyleHints>
#include <QAccessible>
#include <QSignalSpy>
#include <QtTest>

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
    void productionInterfaceIsQuick() {
        EditorController editor; QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine,&QQmlEngine::warnings);
        QVERIFY(load(engine,editor));
        QCOMPARE(warnings.count(),0);
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
        auto *slider=window->findChild<QQuickItem*>("positionSlider"); QVERIFY(slider);
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
    void sliderCancellationRestoresPlaybackAndLostGrab() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window); QVERIFY(editor.importFiles({fixture()}));
        editor.seek(30); editor.togglePlayback(); QVERIFY(editor.playing());
        auto *slider=window->findChild<QQuickItem*>("positionSlider"); QVERIFY(slider);
        const auto point=slider->mapToScene(QPointF(slider->width()*0.7,slider->height()/2)).toPoint();
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
    void sliderEscapeRestoresPosition() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        QVERIFY(editor.importFiles({fixture()})); editor.seek(30);
        auto *slider=window->findChild<QQuickItem*>("positionSlider"); QVERIFY(slider);
        auto start=slider->mapToScene(QPointF(slider->width()*0.75,slider->height()/2)).toPoint();
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
    void rootAcceptsLocalFileDrops() {
        EditorController editor; QQmlApplicationEngine engine;
        auto *window=load(engine,editor); QVERIFY(window);
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(fixture())});
        QDragEnterEvent enter(QPointF(100,150),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&enter);
        QDropEvent drop(QPointF(100,150),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&drop);
        QVERIFY(drop.isAccepted()); QCOMPARE(editor.clips().size(),1);
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
        auto *slider=window->findChild<QQuickItem*>("positionSlider"); QVERIFY(slider);
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
