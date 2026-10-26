#include "project.h"
#include "mlt_engine.h"
#include "visual_renderer.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
namespace {
QImage frameAt(const Project &p,int f,int w=160,int h=90){Mlt::Profile profile;Media::configureOutputProfile(profile,w,h);auto timeline=Media::buildTimeline(p,profile);if(!timeline)return {};timeline->seek(f);std::unique_ptr<Mlt::Frame> frame(timeline->get_frame());auto fmt=mlt_image_rgba;uint8_t *pixels=nullptr;if(mlt_frame_get_image(frame->get_frame(),&pixels,&fmt,&w,&h,0)||!pixels)return {};return QImage(pixels,w,h,w*4,QImage::Format_RGBA8888).copy();}
}
class CompositionTest:public QObject {
 Q_OBJECT
private slots:
 void overlayProxyRetainsAnimationOrigin(){
  Project original;original.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,.5}};original.clips[0].transform.opacity.value=0;
  VisualOverlay overlay;overlay.path=original.clips[0].path;overlay.sourceInFrame=30;overlay.durationFrames=30;overlay.transform.scale.value=.4;
  overlay.transform.x.setKey(0,.2);overlay.transform.x.setKey(59,.8);original.visualOverlays.append(overlay);
  const auto expected=frameAt(original,5);QVERIFY(!expected.isNull());
  auto proxy=original;proxy.visualOverlays[0].sourceInFrame=0;proxy.visualOverlays[0].sourceFrameOffset=30;
  QCOMPARE(frameAt(proxy,5),expected);
  QTemporaryDir dir;QVERIFY(saveProject(proxy,dir.filePath("runtime.json")));const auto loaded=loadProject(dir.filePath("runtime.json"));QVERIFY(loaded);QCOMPARE(loaded->visualOverlays[0].sourceFrameOffset,0);
 }
 void liveTransformsUpdateBothSidesOfCrossfade(){
  Project p;p.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,1},{QFINDTESTDATA("fixtures/rotation.mp4"),0,1}};p.crossfadeFrames=12;
  Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);auto timeline=Media::buildTimeline(p,profile);QVERIFY(timeline);
  for(auto &clip:p.clips)clip.transform.opacity.value=0;
  VisualRendering::update(*timeline,p);
  for(int at:{10,54,90}){timeline->seek(at);std::unique_ptr<Mlt::Frame> frame(timeline->get_frame());auto fmt=mlt_image_rgba;uint8_t *pixels=nullptr;int w=160,h=90;QVERIFY(!mlt_frame_get_image(frame->get_frame(),&pixels,&fmt,&w,&h,0));QVERIFY(pixels);QImage image(pixels,w,h,w*4,QImage::Format_RGBA8888);QVERIFY(image.pixelColor(60,35).value()<20);}
 }
 void videoOverlayTimingAndStacking(){
  Project p;const auto path=QFINDTESTDATA("fixtures/rotation.mp4");p.clips={{path,0,1}};p.clips[0].transform.opacity.value=0;
  VisualOverlay video;video.path=path;video.startFrame=10;video.durationFrames=20;video.transform.scale.value=.5;p.visualOverlays.append(video);
  QVERIFY(frameAt(p,9).pixelColor(60,35).value()<20);QVERIFY(frameAt(p,10).pixelColor(60,35).red()>180);QVERIFY(frameAt(p,30).pixelColor(60,35).value()<20);
  QTemporaryDir d;QImage blue(80,80,QImage::Format_RGB32);blue.fill(Qt::blue);QVERIFY(blue.save(d.filePath("blue.png")));VisualOverlay top;top.path=d.filePath("blue.png");top.lane=2;top.durationFrames=60;p.visualOverlays.append(top);
  QVERIFY(frameAt(p,15).pixelColor(60,35).blue()>240);p.visualLaneVisible[2]=false;QVERIFY(frameAt(p,15).pixelColor(60,35).red()>180);
 }
 void sourceCurvesSurviveTrimSplitAndProxyOrigin(){
  Project p;p.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,1}};auto &t=p.clips[0].transform;t.scale.value=.4;t.x.setKey(0,.2);t.x.setKey(59,.8);
  const auto expected=frameAt(p,35);QVERIFY(!expected.isNull());QVERIFY(p.splitClip(0,30));QCOMPARE(frameAt(p,35),expected);
  p.clips.removeFirst();QCOMPARE(frameAt(p,5),expected);
  // A partial proxy's local zero maps to the retained original source timestamp.
  p.clips[0].inSeconds=0;p.clips[0].outSeconds=.5;p.clips[0].sourceFrameOffset=30;QCOMPARE(frameAt(p,5),expected);
 }
 void exportAgreesAndCanvasFraming(){
  QTemporaryDir d;QImage color(40,80,QImage::Format_RGB32);color.fill(Qt::magenta);QVERIFY(color.save(d.filePath("magenta.png")));
  Project p;p.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,.5}};p.canvasWidth=160;p.canvasHeight=160;
  VisualOverlay o;o.path=d.filePath("magenta.png");o.durationFrames=30;o.transform.scale.value=.4;o.transform.rotation.value=30;p.visualOverlays.append(o);
  QString e;QVERIFY(Media::projectOutputSize(p,&e)->height==160);const auto preview=frameAt(p,10,160,160);QVERIFY(!preview.isNull());QVERIFY(preview.pixelColor(80,80).blue()>240);
  QVERIFY2(Media::exportMp4(p,d.filePath("render.mp4"),{},&e),qPrintable(e));Project exported;exported.clips={{d.filePath("render.mp4"),0,.5}};const auto rendered=frameAt(exported,10,160,160);QVERIFY(rendered.pixelColor(80,80).blue()>220);QVERIFY(rendered.pixelColor(80,80).red()>220);QVERIFY(rendered.pixelColor(80,80).green()<40);
  const auto small=frameAt(p,10,80,80);QVERIFY(small.pixelColor(40,40).blue()>240);
  p.visualOverlays.clear();p.clips[0].transform.framing="fit";QVERIFY(frameAt(p,10,160,160).pixelColor(10,10).value()<20);
  p.clips[0].transform.framing="fill";QVERIFY(frameAt(p,10,160,160).pixelColor(10,10).red()>150);
  p.clips[0].transform.framing="crop";p.clips[0].transform.cropRight=.5;p.clips[0].transform.cropBottom=.5;const auto crop=frameAt(p,10,160,160);QVERIFY(crop.pixelColor(80,80).red()>150);QVERIFY(crop.pixelColor(80,80).green()<80);
 }
 void rendersTransformsAndLayers() {
  QTemporaryDir d;QImage red(80,80,QImage::Format_RGB32);red.fill(Qt::red);QVERIFY(red.save(d.filePath("red.png")));
  Project p;p.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,1}};p.canvasWidth=160;p.canvasHeight=90;
  VisualOverlay o;o.path=d.filePath("red.png");o.durationFrames=60;o.transform.scale.value=.5;p.visualOverlays.append(o);
  Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);QString e;
  auto render=[&](int f){auto timeline=Media::buildTimeline(p,profile,&e);if(!timeline)return QImage{};timeline->seek(f);std::unique_ptr<Mlt::Frame> frame(timeline->get_frame());auto fmt=mlt_image_rgba;int w=160,h=90;uint8_t *pixels=nullptr;mlt_frame_get_image(frame->get_frame(),&pixels,&fmt,&w,&h,0);return pixels?QImage(pixels,w,h,w*4,QImage::Format_RGBA8888).copy():QImage{};};
  auto image=render(20);QVERIFY2(!image.isNull(),qPrintable(e));QVERIFY(image.pixelColor(80,45).red()>230);QVERIFY(image.pixelColor(80,45).green()<20);
  p.visualLaneVisible[0]=false;image=render(20);QVERIFY(image.pixelColor(90,55).green()>150);
  p.visualLaneVisible[0]=true;p.visualOverlays[0].transform.x.setKey(0,.25);p.visualOverlays[0].transform.x.setKey(59,.75);
  image=render(0);QVERIFY(image.pixelColor(40,45).red()>230);image=render(59);QVERIFY(image.pixelColor(120,45).red()>230);
  p.visualOverlays.clear();p.clips[0].transform.scale.value=.5;image=render(20);QVERIFY(image.pixelColor(5,5).value()<20);QVERIFY(image.pixelColor(60,35).red()>150);
 }
 void attachments() {
  Project p;p.clips={{"host",1,5},{"second",0,2}};
  VisualOverlay o;o.path="overlay";o.durationFrames=60;o.hostId=p.clips[0].id;o.hostSourceFrame=180;p.visualOverlays.append(o);
  QCOMPARE(p.overlayStartFrame(0),120);QVERIFY(p.splitClip(0,120));
  QCOMPARE(p.visualOverlays[0].hostId,p.clips[1].id);QVERIFY(p.clips[0].id!=p.clips[1].id);
  QCOMPARE(p.overlayStartFrame(0),120);QVERIFY(p.reorderClips({2,0,1}));QCOMPARE(p.overlayStartFrame(0),240);
  p.clips[2].inSeconds=3.5;p.normalizeVisuals();QCOMPARE(p.visualOverlays[0].hostSourceFrame,210);QCOMPARE(p.overlayStartFrame(0),240);
  p.visualLaneVisible[1]=false;QTemporaryDir directory;QString error;QVERIFY(saveProject(p,directory.filePath("attached.json"),&error));const auto saved=loadProject(directory.filePath("attached.json"),&error);QVERIFY(saved);QCOMPARE(saved->visualOverlays,p.visualOverlays);QCOMPARE(saved->visualLaneVisible,p.visualLaneVisible);
  QVERIFY(p.detachOverlay(0));const int start=p.overlayStartFrame(0);QVERIFY(p.reorderClips({2,0,1}));QCOMPARE(p.overlayStartFrame(0),start);
 }
 void curvesAndRoundtrip() {
  Project p;p.clips={{"host",1,5}};auto &t=p.clips[0].transform;t.x.setKey(60,.1);t.x.setKey(180,.9);
  QCOMPARE(t.x.valueAt(120),.5);t.x.keys[0].ease=true;QVERIFY(t.x.valueAt(90)<.3);
  QVERIFY(p.splitClip(0,120));QCOMPARE(p.clips[0].transform,p.clips[1].transform);
  p.canvasWidth=1080;p.canvasHeight=1920;QTemporaryDir d;QString e;QVERIFY(saveProject(p,d.filePath("p.json"),&e));
  const auto loaded=loadProject(d.filePath("p.json"),&e);QVERIFY2(loaded,qPrintable(e));QCOMPARE(loaded->canvasWidth,1080);
  QCOMPARE(loaded->clips[1].transform,p.clips[1].transform);QCOMPARE(loaded->clips[0].id,p.clips[0].id);
 }
 void removalAndClamp() {
  Project p;p.clips={{"host",0,2},{"other",0,2}};VisualOverlay a;a.path="a";a.hostId=p.clips[0].id;a.hostSourceFrame=100;a.durationFrames=200;p.visualOverlays.append(a);
  p.normalizeVisuals();QCOMPARE(p.visualOverlays[0].durationFrames,140);QVERIFY(p.removeClip(0));QVERIFY(p.visualOverlays.isEmpty());
 }
};
QTEST_GUILESS_MAIN(CompositionTest)
#include "composition_test.moc"
