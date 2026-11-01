#include "project.h"
#include "mlt_engine.h"
#include "visual_renderer.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QDateTime>
#include <QFile>
#include <QProcess>
#include <future>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif
namespace {
qint64 residentBytes(){
#ifdef Q_OS_MACOS
 mach_task_basic_info_data_t info{};mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
 return task_info(mach_task_self(),MACH_TASK_BASIC_INFO,reinterpret_cast<task_info_t>(&info),&count)==KERN_SUCCESS?qint64(info.resident_size):0;
#elif defined(Q_OS_LINUX)
 QFile file("/proc/self/statm");if(!file.open(QIODevice::ReadOnly))return 0;return file.readAll().split(' ').value(1).toLongLong()*sysconf(_SC_PAGESIZE);
#else
 return 0;
#endif
}
QImage frameAt(const Project &p,int f,int w=160,int h=90){Mlt::Profile profile;Media::configureOutputProfile(profile,w,h);auto timeline=Media::buildTimeline(p,profile);if(!timeline)return {};timeline->seek(f);std::unique_ptr<Mlt::Frame> frame(timeline->get_frame());auto fmt=mlt_image_rgba;uint8_t *pixels=nullptr;if(mlt_frame_get_image(frame->get_frame(),&pixels,&fmt,&w,&h,0)||!pixels)return {};return QImage(pixels,w,h,w*4,QImage::Format_RGBA8888).copy();}
QImage timelineFrame(Mlt::Tractor &timeline,int at){timeline.seek(at);std::unique_ptr<Mlt::Frame> frame(timeline.get_frame());auto fmt=mlt_image_rgba;int w=160,h=90;uint8_t *pixels=nullptr;if(!frame||mlt_frame_get_image(frame->get_frame(),&pixels,&fmt,&w,&h,0)||!pixels)return {};return QImage(pixels,w,h,w*4,QImage::Format_RGBA8888).copy();}
struct RawFrame {QByteArray pixels;mlt_image_format format=mlt_image_none;int colorspace=0,fullRange=0;};
RawFrame rawFrame(const Project &project,int at,mlt_image_format requested){
 Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);auto timeline=Media::buildTimeline(project,profile);if(!timeline)return {};
 timeline->seek(at);std::unique_ptr<Mlt::Frame> frame(timeline->get_frame());int w=160,h=90;uint8_t *pixels=nullptr;
 if(!frame||mlt_frame_get_image(frame->get_frame(),&pixels,&requested,&w,&h,0)||!pixels)return {};
 mlt_image_s image{};mlt_image_set_values(&image,pixels,requested,w,h);
 return {QByteArray(reinterpret_cast<char*>(pixels),mlt_image_calculate_size(&image)),requested,frame->get_int("colorspace"),frame->get_int("full_range")};
}
}
class CompositionTest:public QObject {
 Q_OBJECT
private slots:
 void inactiveAndHiddenOverlaysPreservePlainVideoYuv_data(){
  QTest::addColumn<QString>("colorspace");QTest::addColumn<QString>("range");
  QTest::newRow("709-limited")<<QString("bt709")<<QString("tv");
  QTest::newRow("601-full")<<QString("smpte170m")<<QString("pc");
 }
 void inactiveAndHiddenOverlaysPreservePlainVideoYuv(){
  QFETCH(QString,colorspace);QFETCH(QString,range);QTemporaryDir dir;
  const auto path=dir.filePath("bars.mkv");QProcess ffmpeg;
  ffmpeg.start("ffmpeg",{"-v","error","-f","lavfi","-i","smptebars=size=160x90:rate=60","-t","1","-c:v","ffv1","-pix_fmt","yuv420p","-colorspace",colorspace,"-color_range",range,path});
  QVERIFY(ffmpeg.waitForFinished(30000));QCOMPARE(ffmpeg.exitCode(),0);
  QImage still(40,40,QImage::Format_RGB32);still.fill(Qt::magenta);QVERIFY(still.save(dir.filePath("still.png")));
  Project plain;plain.clips={{path,0,1}};auto withOverlay=plain;VisualOverlay overlay;overlay.path=dir.filePath("still.png");overlay.startFrame=10;overlay.durationFrames=20;withOverlay.visualOverlays={overlay};
  for(const auto format:{mlt_image_yuv420p,mlt_image_yuv422})for(int at:{0,9,30,59}){
   const auto expected=rawFrame(plain,at,format),actual=rawFrame(withOverlay,at,format);QVERIFY(!expected.pixels.isEmpty());
   QCOMPARE(actual.format,expected.format);QCOMPARE(actual.colorspace,expected.colorspace);QCOMPARE(actual.fullRange,expected.fullRange);QCOMPARE(actual.pixels,expected.pixels);
  }
  for(int at:{10,29}){const auto image=frameAt(withOverlay,at);QVERIFY(image.pixelColor(80,45).red()>240);QVERIFY(image.pixelColor(80,45).blue()>240);}
  withOverlay.visualLaneVisible[0]=false;
  for(int at:{9,10,29,30}){const auto expected=rawFrame(plain,at,mlt_image_yuv420p),actual=rawFrame(withOverlay,at,mlt_image_yuv420p);QCOMPARE(actual.format,expected.format);QCOMPARE(actual.colorspace,expected.colorspace);QCOMPARE(actual.fullRange,expected.fullRange);QCOMPARE(actual.pixels,expected.pixels);}
 }
 void stillImagesLoadWhenFirstActive(){
  QTemporaryDir dir;const auto path=dir.filePath("still.bmp");QImage still(40,40,QImage::Format_RGB32);still.fill(Qt::red);QVERIFY(still.save(path));
  Project p;p.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,1}};VisualOverlay overlay;overlay.path=path;overlay.startFrame=10;overlay.durationFrames=20;p.visualOverlays={overlay};
  Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);auto timeline=Media::buildTimeline(p,profile);QVERIFY(timeline);QVERIFY(!timelineFrame(*timeline,9).isNull());
  still.fill(Qt::blue);QVERIFY(still.save(path));const auto active=timelineFrame(*timeline,10);QVERIFY(!active.isNull());QVERIFY(active.pixelColor(80,45).blue()>240);QVERIFY(active.pixelColor(80,45).red()<20);
 }
 void repeatedStillSourcesShareImmutablePixelsAndRefreshIdentity(){
  QTemporaryDir dir;const auto path=dir.filePath("still.bmp");QString alias=path;
  QImage still(40,40,QImage::Format_RGB32);still.fill(Qt::red);QVERIFY(still.save(path));
#ifndef Q_OS_WIN
  alias=dir.filePath("alias.bmp");QVERIFY(QFile::link(path,alias));
#endif
  const auto timestamp=QDateTime::fromMSecsSinceEpoch(1700000000000LL);QFile file(path);QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.setFileTime(timestamp,QFileDevice::FileModificationTime));file.close();
  Project p;p.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,1}};VisualOverlay overlay;overlay.path=path;overlay.durationFrames=60;p.visualOverlays={overlay};
  Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);auto activeTimeline=Media::buildTimeline(p,profile);QVERIFY(activeTimeline);
  const auto first=timelineFrame(*activeTimeline,10);QVERIFY(first.pixelColor(80,45).red()>240);
  // Same canonical path, size and mtime identify the already decoded immutable image.
  still.fill(Qt::blue);QVERIFY(still.save(path));QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.setFileTime(timestamp,QFileDevice::FileModificationTime));file.close();
  p.visualOverlays[0].path=alias;QCOMPARE(frameAt(p,10),first);
  activeTimeline.reset(); // The following eviction checks concern only inactive reuse.
  const auto other=dir.filePath("churn.bmp");QImage large(1024,1024,QImage::Format_RGB32);large.fill(Qt::green);QVERIFY(large.save(other));
  auto churn=p;churn.visualOverlays[0].path=other;QFile churnFile(other);
  for(int i=0;i<24;++i){QVERIFY(churnFile.open(QIODevice::ReadWrite));QVERIFY(churnFile.setFileTime(timestamp.addSecs(i),QFileDevice::FileModificationTime));churnFile.close();QVERIFY(frameAt(churn,10).pixelColor(80,45).green()>240);}
  // More than 90 MiB of newer stills must evict the old decoded image.
  const auto evicted=frameAt(p,10);QVERIFY(evicted.pixelColor(80,45).blue()>240);QVERIFY(evicted.pixelColor(80,45).red()<20);
  still.fill(Qt::red);QVERIFY(still.save(path));
  QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.setFileTime(timestamp.addSecs(1),QFileDevice::FileModificationTime));file.close();
  const auto replaced=frameAt(p,10);QVERIFY(replaced.pixelColor(80,45).red()>240);QVERIFY(replaced.pixelColor(80,45).blue()<20);
  QImage resized(48,48,QImage::Format_RGB32);resized.fill(Qt::blue);QVERIFY(resized.save(path));QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.setFileTime(timestamp.addSecs(1),QFileDevice::FileModificationTime));file.close();
  const auto sizeChanged=frameAt(p,10);QVERIFY(sizeChanged.pixelColor(80,45).blue()>240);QVERIFY(sizeChanged.pixelColor(80,45).red()<20);
 }
 void oversizedStillsDecodeOncePerActiveInterval(){
  QTemporaryDir dir;const auto path=dir.filePath("large.bmp");QImage still(3072,3072,QImage::Format_RGB32);still.fill(Qt::red);QVERIFY(still.save(path));
  const auto timestamp=QDateTime::fromMSecsSinceEpoch(1700000000000LL);QFile file(path);QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.setFileTime(timestamp,QFileDevice::FileModificationTime));file.close();
  Project p;p.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,1}};VisualOverlay overlay;overlay.path=path;overlay.startFrame=10;overlay.durationFrames=20;p.visualOverlays={overlay};
  Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);auto timeline=Media::buildTimeline(p,profile);QVERIFY(timeline);const auto first=timelineFrame(*timeline,10);QVERIFY(first.pixelColor(80,45).red()>240);
  still.fill(Qt::blue);QVERIFY(still.save(path));QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.setFileTime(timestamp,QFileDevice::FileModificationTime));file.close();
  QCOMPARE(timelineFrame(*timeline,11),first);
  QVERIFY(!timelineFrame(*timeline,30).isNull());const auto nextInterval=timelineFrame(*timeline,10);QVERIFY(nextInterval.pixelColor(80,45).blue()>240);QVERIFY(nextInterval.pixelColor(80,45).red()<20);
 }
 void activeNormalStillsDoNotDecodeAgainWhenTheSharedCacheFills(){
  QTemporaryDir dir;QImage still(1024,1024,QImage::Format_RGB32);still.fill(Qt::red);Project p;p.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,1}};
  for(int i=0;i<10;++i){VisualOverlay overlay;overlay.path=dir.filePath(QString::number(i)+".bmp");overlay.durationFrames=60;overlay.transform.x.value=(i+.5)/10.0;overlay.transform.scale.value=.15;QVERIFY(still.save(overlay.path));p.visualOverlays.append(overlay);}
  const auto path=p.visualOverlays[0].path;const auto timestamp=QDateTime::fromMSecsSinceEpoch(1700000000000LL);QFile file(path);
  const auto stamp=[&]{if(!file.open(QIODevice::ReadWrite))return false;const bool ok=file.setFileTime(timestamp,QFileDevice::FileModificationTime);file.close();return ok;};QVERIFY(stamp());
  Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);auto timeline=Media::buildTimeline(p,profile);QVERIFY(timeline);QVERIFY(timelineFrame(*timeline,10).pixelColor(8,45).red()>240);
  // The other nine active images exceed the inactive-reuse cache; this identity is unchanged.
  still.fill(Qt::blue);QVERIFY(still.save(path));QVERIFY(stamp());const auto next=timelineFrame(*timeline,11);QVERIFY(next.pixelColor(8,45).red()>240);QVERIFY(next.pixelColor(8,45).blue()<20);
 }
 void oversizedAliasesShareActivePixelsUntilTheLastReferenceEnds(){
  QTemporaryDir dir;const auto path=dir.filePath("large.bmp");QString alias=path;QImage still(3072,3072,QImage::Format_RGB32);still.fill(Qt::red);QVERIFY(still.save(path));
#ifndef Q_OS_WIN
  alias=dir.filePath("alias.bmp");QVERIFY(QFile::link(path,alias));
#endif
  const auto timestamp=QDateTime::fromMSecsSinceEpoch(1700000000000LL);QFile file(path);
  const auto stamp=[&](const QDateTime &time){if(!file.open(QIODevice::ReadWrite))return false;const bool ok=file.setFileTime(time,QFileDevice::FileModificationTime);file.close();return ok;};QVERIFY(stamp(timestamp));
  Project p;p.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,1}};
  for(int i=0;i<8;++i){VisualOverlay overlay;overlay.path=i%2?alias:path;overlay.startFrame=i?11:10;overlay.durationFrames=20;p.visualOverlays.append(overlay);}
  Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);auto timeline=Media::buildTimeline(p,profile);QVERIFY(timeline);QVERIFY(timelineFrame(*timeline,10).pixelColor(80,45).red()>240);const auto before=residentBytes();
  still.fill(Qt::blue);QVERIFY(still.save(path));QVERIFY(stamp(timestamp));const auto shared=timelineFrame(*timeline,11);const auto extra=residentBytes()-before;
  qInfo()<<"Additional resident bytes for seven references to the same oversized still:"<<extra;
  if(before>0)QVERIFY2(extra<3*still.sizeInBytes(),"Repeated active references must share one decoded oversized image");
  QVERIFY(shared.pixelColor(80,45).red()>240);QVERIFY(shared.pixelColor(80,45).blue()<20);
  auto otherProject=p;otherProject.visualOverlays.resize(1);auto otherTimeline=Media::buildTimeline(otherProject,profile);QVERIFY(otherTimeline);QVERIFY(timelineFrame(*otherTimeline,11).pixelColor(80,45).red()>240);
  // The first reference has ended; the other seven must still own the red pixels.
  QVERIFY(timelineFrame(*timeline,30).pixelColor(80,45).red()>240);
  still.fill(Qt::green);QVERIFY(still.save(path));QVERIFY(stamp(timestamp.addSecs(1)));QVERIFY(timelineFrame(*timeline,30).pixelColor(80,45).green()>240);
  QVERIFY(!timelineFrame(*timeline,31).isNull());still.fill(Qt::blue);QVERIFY(still.save(path));QVERIFY(stamp(timestamp.addSecs(1)));
  const auto nextInterval=timelineFrame(*timeline,11);QVERIFY(nextInterval.pixelColor(80,45).blue()>240);QVERIFY(nextInterval.pixelColor(80,45).green()<20);
 }
 void failedLazyStillDecodeRecordsExplicitRenderError(){
  QTemporaryDir dir;const auto path=dir.filePath("still.png");QImage still(40,40,QImage::Format_RGB32);still.fill(Qt::red);QVERIFY(still.save(path));
  Project p;p.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,1}};VisualOverlay overlay;overlay.path=path;overlay.durationFrames=60;p.visualOverlays={overlay};
  Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);auto timeline=Media::buildTimeline(p,profile);QVERIFY(timeline);
  QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("Unreadable image");file.close();
  std::vector<std::unique_ptr<Mlt::Frame>> frames;for(int at=10;at<18;++at){timeline->seek(at);frames.emplace_back(timeline->get_frame());QVERIFY(frames.back());}
  std::vector<std::future<void>> workers;for(auto &frame:frames)workers.emplace_back(std::async(std::launch::async,[f=frame.get()]{auto format=mlt_image_rgba;int w=160,h=90;uint8_t *pixels=nullptr;mlt_frame_get_image(f->get_frame(),&pixels,&format,&w,&h,0);}));
  for(auto &worker:workers)worker.get();
  QVERIFY2(!VisualRendering::renderError(*timeline).isEmpty(),"MLT may return background pixels; the explicit error must prevent silent overlay loss");QVERIFY(VisualRendering::renderError(*timeline).contains(path));
  const auto firstError=VisualRendering::renderError(*timeline);QVERIFY(still.save(path));QVERIFY(!timelineFrame(*timeline,11).isNull());QCOMPARE(VisualRendering::renderError(*timeline),firstError);
  auto plain=p;plain.visualOverlays.clear();auto ordinary=Media::buildTimeline(plain,profile);QVERIFY(ordinary);QVERIFY(VisualRendering::renderError(*ordinary).isEmpty());
 }
 void concurrentVideoOverlayFramesMatchSerialOutOfOrderRequests(){
  Project p;p.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,1}};VisualOverlay overlay;overlay.path=QFINDTESTDATA("fixtures/seek.mp4");overlay.durationFrames=60;overlay.transform.scale.value=.6;overlay.transform.x.setKey(0,.2);overlay.transform.x.setKey(59,.8);p.visualOverlays={overlay};
  const QVector<int> positions{40,3,55,12,30,0,59,18};QVector<QImage> expected;for(int at:positions)expected<<frameAt(p,at);
  Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);auto timeline=Media::buildTimeline(p,profile);QVERIFY(timeline);
  std::vector<std::unique_ptr<Mlt::Frame>> frames;for(int at:positions){timeline->seek(at);frames.emplace_back(timeline->get_frame());}
  std::vector<std::future<QImage>> workers;for(auto &frame:frames)workers.emplace_back(std::async(std::launch::async,[f=frame.get()]{auto format=mlt_image_rgba;int w=160,h=90;uint8_t *pixels=nullptr;if(mlt_frame_get_image(f->get_frame(),&pixels,&format,&w,&h,0)||!pixels)return QImage{};return QImage(pixels,w,h,w*4,QImage::Format_RGBA8888).copy();}));
  for(int i=0;i<positions.size();++i){QVERIFY(!expected[i].isNull());QCOMPARE(workers[i].get(),expected[i]);}
 }
 void queuedOverlayFramesRetainTheirEditSnapshot(){
  Project p;p.clips={{QFINDTESTDATA("fixtures/rotation.mp4"),0,1}};VisualOverlay overlay;overlay.path=QFINDTESTDATA("fixtures/seek.mp4");overlay.durationFrames=60;overlay.transform.scale.value=.6;p.visualOverlays={overlay};
  const auto expected=frameAt(p,10);Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);auto timeline=Media::buildTimeline(p,profile);QVERIFY(timeline);
  timeline->seek(10);std::unique_ptr<Mlt::Frame> queued(timeline->get_frame());QVERIFY(queued);
  p.visualOverlays[0].transform.opacity.value=0;VisualRendering::update(*timeline,p);
  auto format=mlt_image_rgba;int w=160,h=90;uint8_t *pixels=nullptr;QCOMPARE(mlt_frame_get_image(queued->get_frame(),&pixels,&format,&w,&h,0),0);QVERIFY(pixels);QCOMPARE(QImage(pixels,w,h,w*4,QImage::Format_RGBA8888),expected);
  auto plain=p;plain.visualOverlays.clear();QCOMPARE(timelineFrame(*timeline,10),frameAt(plain,10));
 }
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
