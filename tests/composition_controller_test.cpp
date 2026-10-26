#include "editor_controller.h"
#include "preview_item.h"
#include "preview.h"
#include <QQuickWindow>
#include <QMessageBox>
#include <QAbstractButton>
#include <QApplication>
#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QSemaphore>
#include <memory>
class CompositionControllerTest:public QObject {
 Q_OBJECT
private slots:
 void visualDragReturningToOriginalRestoresPixels_data(){
  QTest::addColumn<bool>("cancel");QTest::newRow("release")<<false;QTest::newRow("cancel")<<true;
 }
 void visualDragReturningToOriginalRestoresPixels(){
  QFETCH(bool,cancel);EditorController c;QVERIFY(c.importFiles({QFINDTESTDATA("fixtures/rotation.mp4")}));
  c.beginVisualGesture();c.setTransform({{"opacity",0.}});
  QTRY_VERIFY_WITH_TIMEOUT(c.frame().pixelColor(60,35).value()<20,3000);
  auto *preview=c.findChild<Preview*>();QVERIFY(preview);auto entered=std::make_shared<QSemaphore>(),release=std::make_shared<QSemaphore>();QObject releaseContext;
  preview->requestSeek(c.position(),true,[entered,release]{entered->release();release->tryAcquire(1,3000);});
  QVERIFY(entered->tryAcquire(1,3000));
  c.setTransform({{"opacity",1.}});
  connect(preview,&Preview::seekingChanged,&releaseContext,[release](bool seeking){if(!seeking)release->release();});
  c.endVisualGesture(cancel);
  QTRY_VERIFY_WITH_TIMEOUT(c.frame().pixelColor(60,35).value()>100,3000);
  QCOMPARE(c.project().clips[0].transform.opacity.value,1.);
 }
 void visualPreviewRetainsLatestEditDuringTransport_data(){
  QTest::addColumn<bool>("play");QTest::newRow("play")<<true;QTest::newRow("scrub")<<false;
 }
 void visualPreviewRetainsLatestEditDuringTransport(){
  QFETCH(bool,play);EditorController c;QVERIFY(c.importFiles({QFINDTESTDATA("fixtures/rotation.mp4")}));
  c.beginVisualGesture();c.setTransform({{"opacity",0.}});
  QTRY_VERIFY_WITH_TIMEOUT(c.frame().pixelColor(60,35).value()<20,3000);
  auto *preview=c.findChild<Preview*>();QVERIFY(preview);auto entered=std::make_shared<QSemaphore>(),release=std::make_shared<QSemaphore>();QObject releaseContext;
  preview->requestSeek(c.position(),true,[entered,release]{entered->release();release->tryAcquire(1,3000);});
  QVERIFY(entered->tryAcquire(1,3000));c.setTransform({{"opacity",1.}});
  connect(preview,&Preview::seekingChanged,&releaseContext,[release](bool seeking){if(!seeking)release->release();});
  if(play)c.togglePlayback();else c.beginScrub();
  QVERIFY(c.visualPreviewActive());
  if(play)c.togglePlayback();else c.finishScrub(true);
  QTRY_VERIFY_WITH_TIMEOUT(c.frame().pixelColor(60,35).value()>100,3000);
  c.endVisualGesture(true);
 }
 void visualDragDeliversFramesAsynchronously(){
  EditorController c;QVERIFY(c.importFiles({QFINDTESTDATA("fixtures/rotation.mp4")}));
  c.beginVisualGesture();QSignalSpy frames(&c,&EditorController::frameChanged);
  c.setTransform({{"x",.2},{"scale",.4}});c.setTransform({{"x",.3}});c.setTransform({{"x",.7}});
  QCOMPARE(frames.size(),0);
  QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(),3000);
  QTRY_VERIFY_WITH_TIMEOUT(c.frame().pixelColor(c.frame().width()/10,c.frame().height()/2).value()<20,3000);
  QCOMPARE(c.project().clips[0].transform.x.value,.7);
  c.endVisualGesture();c.undo();QCOMPARE(c.project().clips[0].transform.x.value,.5);
  c.beginVisualGesture();c.setTransform({{"opacity",0.}});c.endVisualGesture(true);
  QCOMPARE(c.project().clips[0].transform.opacity.value,1.);
 }
 void viewerTransformsCancelAndUndo(){
  EditorController c;QVERIFY(c.importFiles({QFINDTESTDATA("fixtures/rotation.mp4")}));c.setTransformMode(true);QTRY_VERIFY(!c.frame().isNull());
  QQuickWindow window;window.resize(800,450);auto *view=new VideoPreview(window.contentItem());view->setSize({800,450});view->setController(&c);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
  QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,{400,225});QTest::mouseMove(&window,{480,270});QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,{480,270});
  QVERIFY(qAbs(c.project().clips[0].transform.x.value-.6)<.01);QVERIFY(qAbs(c.project().clips[0].transform.y.value-.6)<.01);c.undo();QCOMPARE(c.project().clips[0].transform.x.value,.5);
  QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,{400,225});QTest::mouseMove(&window,{480,270});QTest::keyClick(&window,Qt::Key_Escape);QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,{480,270});QCOMPARE(c.project().clips[0].transform.x.value,.5);
  c.setTransform({{"scale",.5}});QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,{600,337});QTest::mouseMove(&window,{700,394});QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,{700,394});QVERIFY(c.project().clips[0].transform.scale.value>.7);c.undo();QCOMPARE(c.project().clips[0].transform.scale.value,.5);
 }
 void viewerRotationHandle(){
  EditorController c;QVERIFY(c.importFiles({QFINDTESTDATA("fixtures/rotation.mp4")}));c.setTransformMode(true);c.setTransform({{"scale",.5}});QTRY_VERIFY(!c.frame().isNull());
  QQuickWindow window;window.resize(800,450);auto *view=new VideoPreview(window.contentItem());view->setSize({800,450});view->setController(&c);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
  QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,{400,89});QTest::mouseMove(&window,{536,225});QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,{536,225});QVERIFY(qAbs(c.project().clips[0].transform.rotation.value-90)<1);c.undo();QCOMPARE(c.project().clips[0].transform.rotation.value,0.);
 }
 void transactionAndKeyframes(){
  EditorController c;QVERIFY(c.importFiles({QFINDTESTDATA("fixtures/rotation.mp4")}));
  c.beginVisualGesture();c.setTransform({{"x",.2},{"scale",.5}});c.endVisualGesture(true);
  QCOMPARE(c.project().clips[0].transform.x.value,.5);c.beginVisualGesture();c.setTransform({{"x",.2}});c.setTransform({{"x",.3}});c.endVisualGesture();
  QCOMPARE(c.project().clips[0].transform.x.value,.3);c.undo();QCOMPARE(c.project().clips[0].transform.x.value,.5);c.redo();
  c.editKeyframe("position","add");QCOMPARE(c.project().clips[0].transform.x.keys.size(),1);
  c.seek(30);c.setTransform({{"x",.7}});QCOMPARE(c.project().clips[0].transform.x.keys.size(),2);
  c.editKeyframe("position","previous");QCOMPARE(c.position(),0);c.editKeyframe("position","delete");QCOMPARE(c.project().clips[0].transform.x.keys.size(),1);
 }
 void hostDeletionIncludesOverlaysInOneUndo(){
  EditorController c;const auto path=QFINDTESTDATA("fixtures/rotation.mp4");QVERIFY(c.importFiles({path}));QVERIFY(c.addVisualOverlay(path,10,0));const auto id=c.project().clips[0].id;
  c.selectClip(0);QTimer::singleShot(0,[]{if(auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))for(auto *button:box->buttons())if(box->buttonRole(button)==QMessageBox::DestructiveRole){button->click();break;}});c.removeSelection();
  QVERIFY(c.project().clips.isEmpty());QVERIFY(c.project().visualOverlays.isEmpty());c.undo();QCOMPARE(c.project().clips[0].id,id);QCOMPARE(c.project().visualOverlays.size(),1);QCOMPARE(c.project().overlayStartFrame(0),10);
  c.selectClip(0);c.detachSelectedHosts();c.removeSelection();QVERIFY(c.project().clips.isEmpty());QCOMPARE(c.project().visualOverlays.size(),1);QCOMPARE(c.project().overlayStartFrame(0),10);
 }
 void legacyRotationHasNumericParityAndReset(){
  EditorController c;QVERIFY(c.importFiles({QFINDTESTDATA("fixtures/rotation.mp4")}));c.rotate(90);QCOMPARE(c.currentTransform()["rotation"].toDouble(),90.);
  c.setTransform({{"rotation",45.}});QCOMPARE(c.currentTransform()["rotation"].toDouble(),45.);QCOMPARE(c.project().clips[0].transform.rotation.value,-45.);
  c.resetTransform();QCOMPARE(c.project().clips[0].rotationDegrees,0);QCOMPARE(c.currentTransform()["rotation"].toDouble(),0.);
 }
 void overlaysAndCanvas(){
  EditorController c;QVERIFY(c.importFiles({QFINDTESTDATA("fixtures/rotation.mp4")}));
  QTemporaryDir d;QImage image(20,20,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(d.filePath("image.png")));
  QVERIFY(c.addVisualOverlay(d.filePath("image.png"),10,1));QCOMPARE(c.project().overlayStartFrame(0),10);QVERIFY(!c.project().visualOverlays[0].hostId.isEmpty());
  c.beginVisualGesture();c.setOverlayTiming(0,20,0,20);c.endVisualGesture();QCOMPARE(c.project().overlayStartFrame(0),20);c.undo();QCOMPARE(c.project().overlayStartFrame(0),10);
  c.setCanvas(1080,1920);QCOMPARE(c.project().canvasHeight,1920);c.undo();QCOMPARE(c.project().canvasHeight,0);
  c.detachVisualOverlay();QVERIFY(c.project().visualOverlays[0].hostId.isEmpty());
 }
};
QTEST_MAIN(CompositionControllerTest)
#include "composition_controller_test.moc"
