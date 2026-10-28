#include "project.h"
#include "mlt_engine.h"
#include "text_renderer.h"
#include "preview_item.h"
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QTextLayout>
#include <algorithm>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {
// Frozen uncached painter: changing the cache must preserve its per-primitive blending.
struct ReferenceLayout {
    QTextLayout text;
    QRectF rect;
    qreal padding=0,scale=1;
    ReferenceLayout(const TextOverlay &t,const QSize &canvas) {
        if(canvas.isEmpty() || t.text.isEmpty())return;
        scale=canvas.height()/1080.0;
        const QSizeF logicalCanvas(canvas.width()/scale,1080);
        QFont font(t.fontFamily);font.setPixelSize(t.fontSize);font.setBold(t.bold);
        QString content=t.text;content.replace('\n',QChar::LineSeparator);
        text.setText(content);text.setFont(font);
        QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        option.setAlignment(t.style=="box"?Qt::AlignLeft:Qt::AlignHCenter);text.setTextOption(option);
        text.beginLayout();qreal y=0,w=0;
        while(true){auto line=text.createLine();if(!line.isValid())break;line.setLineWidth(logicalCanvas.width()*.86);line.setPosition({0,y});y+=line.height();w=std::max(w,line.naturalTextWidth());}
        text.endLayout();
        text.beginLayout();y=0;
        while(true){auto line=text.createLine();if(!line.isValid())break;line.setLineWidth(w+1);line.setPosition({0,y});y+=line.height();}
        text.endLayout();
        padding=18;
        const QSizeF size(std::min(logicalCanvas.width(),w+1+2*padding),std::min(logicalCanvas.height(),y+2*padding));
        const qreal x=std::clamp(t.x*logicalCanvas.width()-size.width()/2,0.0,logicalCanvas.width()-size.width());
        const qreal top=std::clamp(t.y*logicalCanvas.height()-size.height()/2,0.0,logicalCanvas.height()-size.height());
        rect={QPointF(x,top),size};
    }
};
void referencePaint(QImage &image,const QVector<TextOverlay> &texts,int frame) {
    QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::TextAntialiasing);
    for(const auto &t:texts){
        if(frame<t.startFrame || frame>=t.startFrame+t.durationFrames || t.text.isEmpty())continue;
        ReferenceLayout layout(t,image.size());if(layout.rect.isEmpty())continue;
        p.save();p.scale(layout.scale,layout.scale);p.setClipRect(layout.rect);
        if(t.fade&&t.durationFrames>2){const double ramp=std::min(27.0,(t.durationFrames-1)/2.0);p.setOpacity(std::clamp(std::min(frame-t.startFrame,t.startFrame+t.durationFrames-1-frame)/ramp,0.0,1.0));}
        if(t.style=="box"){p.setPen(Qt::NoPen);p.setBrush(QColor(10,24,23,220));p.drawRoundedRect(layout.rect,8,8);}
        const QPointF origin=layout.rect.topLeft()+QPointF(layout.padding,layout.padding);
        if(t.shadow){p.setPen(QColor(0,0,0,210));layout.text.draw(&p,origin+QPointF(2,3));}
        p.setPen(QColor(t.color));layout.text.draw(&p,origin);p.restore();
    }
}

qint64 residentBytes() {
#ifdef Q_OS_MACOS
    mach_task_basic_info_data_t info{};mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
    return task_info(mach_task_self(),MACH_TASK_BASIC_INFO,reinterpret_cast<task_info_t>(&info),&count)==KERN_SUCCESS?qint64(info.resident_size):0;
#elif defined(Q_OS_LINUX)
    QFile file("/proc/self/statm");if(!file.open(QIODevice::ReadOnly))return 0;
    return file.readAll().split(' ').value(1).toLongLong()*sysconf(_SC_PAGESIZE);
#else
    return 0;
#endif
}
QJsonObject titleJson() {
    return {{"text", "Adventure Park Day\nwith Isaac and Daddy"}, {"fontFamily", "Arial"},
            {"style", "clean"}, {"color", "#ffffff"}, {"fontSize", 84},
            {"bold", true}, {"shadow", true}, {"fade", true},
            {"startFrame", 60}, {"durationFrames", 120}, {"x", .5}, {"y", .4}};
}
QString writeProject(QTemporaryDir &dir, const QJsonObject &title) {
    const auto path = dir.filePath("title.isaac.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QJsonDocument(QJsonObject{{"version", 5}, {"clips", QJsonArray{}},
               {"textOverlays", QJsonArray{title}}}).toJson());
    return path;
}
QImage frameAt(const Project &p,int frame,int width=640,int height=360) {
    Mlt::Profile profile;Media::configureOutputProfile(profile,width,height);
    auto timeline=Media::buildTimeline(p,profile);if(!timeline)return {};
    timeline->seek(frame);std::unique_ptr<Mlt::Frame> f(timeline->get_frame());
    if(!f)return {};auto format=mlt_image_rgba;uint8_t *pixels=nullptr;
    if(mlt_frame_get_image(f->get_frame(),&pixels,&format,&width,&height,0) || !pixels)return {};
    return QImage(pixels,width,height,width*4,QImage::Format_RGBA8888).copy();
}
Project footage(){Project p;p.clips={{QDir::current().filePath("tests/fixtures/rotation.mp4"),0,.5}};return p;}
int magentaPixels(const QImage &image){int count=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);count+=c.red()>160 && c.blue()>150 && c.green()<110;}return count;}
}
class TextOverlayTest : public QObject {
    Q_OBJECT
private slots:
    void repeatedCaptionsReuseExpensiveTextWork() {
        TextOverlay text;text.text=QString::fromUtf8("Adventure مرحبا 字幕 and Isaac ").repeated(55);text.fontSize=40;text.shadow=false;
        QImage image(640,360,QImage::Format_RGBA8888);
        const auto measure=[&](bool distinct,int sample){
            QElapsedTimer timer;timer.start();
            for(int i=0;i<80;++i){auto cue=text;if(distinct)cue.text+=QString::number(sample*80+i);image.fill(Qt::black);TextRendering::paint(image,{cue},30);}
            return timer.nsecsElapsed();
        };
        image.fill(Qt::black);TextRendering::paint(image,{text},30);
        QVector<qint64> repeated,distinct;
        for(int sample=0;sample<3;++sample){distinct<<measure(true,sample);repeated<<measure(false,sample);}
        std::sort(repeated.begin(),repeated.end());std::sort(distinct.begin(),distinct.end());
        qInfo()<<"Caption render ns, repeated:"<<repeated[1]<<"distinct:"<<distinct[1];
        QVERIFY2(repeated[1]<distinct[1]*.8,"Repeated captions must avoid most repeated text layout work");
    }
    void textCacheRetainsOnlyBoundedRecentCaptions() {
        TextOverlay text;text.text=QString::fromUtf8("Adventure مرحبا 字幕 ").repeated(90);text.fontSize=40;text.shadow=false;
        QImage image(320,180,QImage::Format_RGBA8888);image.fill(Qt::black);TextRendering::paint(image,{text},30);
        const qint64 before=residentBytes();if(!before)QSKIP("Resident memory measurement unavailable");
        qint64 growth=0;
        for(int i=0;i<2000;++i){auto cue=text;cue.text+=QString::number(i);image.fill(Qt::black);TextRendering::paint(image,{cue},30);if(i%100==0)growth=std::max(growth,residentBytes()-before);}
        growth=std::max(growth,residentBytes()-before);qInfo()<<"Distinct caption peak resident memory growth:"<<growth;
        QVERIFY2(growth<32*1024*1024,"Rendering many cues must not retain every text layout");
        QImage expected(image.size(),image.format());expected.fill(Qt::black);image.fill(Qt::black);
        referencePaint(expected,{text},30);TextRendering::paint(image,{text},30);QCOMPARE(image,expected);
    }
    void cachedTextMatchesUncachedPainterAcrossFadesAndCanvases() {
        for(const QSize size:{QSize(1920,1080),QSize(853,479),QSize(640,360)}){
            for(const QString style:{QString("clean"),QString("box")}){
                TextOverlay first;first.text=QString::fromUtf8("Adventure مرحبا 字幕\nwith Isaac and Daddy");first.style=style;first.fontSize=45;first.x=.04;first.y=.95;first.fade=true;first.durationFrames=60;
                TextOverlay second=first;second.color="#ff40a0";second.x=.12;second.y=.9;second.fontFamily=QFontDatabase::families().first();second.bold=false;
                for(int frame:{0,1,13,27,40,58,59}){
                    QImage actual(size,QImage::Format_RGBA8888);actual.fill(QColor(35,70,110));auto expected=actual;
                    referencePaint(expected,{first,second},frame);TextRendering::paint(actual,{first,second},frame);
                    QCOMPARE(actual,expected);
                }
            }
        }
    }
    void attachedTextUpdatesInvalidateCachedAppearance() {
        auto project=footage();TextOverlay text;text.text="Cache before edit";text.shadow=false;project.textOverlays={text};
        Mlt::Profile profile;Media::configureOutputProfile(profile,640,360);auto timeline=Media::buildTimeline(project,profile);QVERIFY(timeline);
        const QImage background=frameAt(footage(),10);QVERIFY(!background.isNull());
        const auto check=[&]{
            TextRendering::update(*timeline,{text});timeline->seek(10);std::unique_ptr<Mlt::Frame> frame(timeline->get_frame());
            auto format=mlt_image_rgba;uint8_t *pixels=nullptr;int width=640,height=360;
            if(!frame||mlt_frame_get_image(frame->get_frame(),&pixels,&format,&width,&height,0)||!pixels)return QImage{};
            return QImage(pixels,width,height,width*4,QImage::Format_RGBA8888).copy();
        };
        const auto verify=[&]{auto expected=background;referencePaint(expected,{text},10);QCOMPARE(check(),expected);};
        verify();
        text.text=QString::fromUtf8("Edited مرحبا\nnew line");verify();
        text.fontFamily=QFontDatabase::families().first();verify();
        text.fontSize=58;verify();text.bold=false;verify();text.style="box";verify();
        text.color="#ff00ff";verify();text.shadow=true;verify();text.x=.25;verify();text.y=.7;verify();
        text.fade=true;verify();
        text.startFrame=11;QCOMPARE(check(),background);
        TextRendering::update(*timeline,{});timeline->seek(10);std::unique_ptr<Mlt::Frame> frame(timeline->get_frame());
        auto format=mlt_image_rgba;uint8_t *pixels=nullptr;int width=640,height=360;QVERIFY(frame);QCOMPARE(mlt_frame_get_image(frame->get_frame(),&pixels,&format,&width,&height,0),0);
        QCOMPARE(QImage(pixels,width,height,width*4,QImage::Format_RGBA8888),background);
    }
    void textSurvivesProjectRoundTrip() {
        QTemporaryDir dir; QString error;
        const auto path=writeProject(dir,titleJson());
        const auto loaded=loadProject(path,&error);
        QVERIFY2(loaded.has_value(),qPrintable(error));
        const auto output=dir.filePath("saved.isaac.json");
        QVERIFY2(saveProject(*loaded,output,&error),qPrintable(error));
        QFile file(output); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto saved=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(saved.value("textOverlays").toArray().size(),1);
        QCOMPARE(saved.value("textOverlays").toArray()[0].toObject(),titleJson());
    }
    void malformedTextIsRejected() {
        for(const auto &key: {"startFrame","durationFrames","fontSize","x","y","bold","fade","color","style"}) {
            QTemporaryDir dir; auto title=titleJson(); title[key]=QJsonObject{}; QString error;
            QVERIFY2(!loadProject(writeProject(dir,title),&error),key);
            QVERIFY(!error.isEmpty());
        }
    }
    void mltFramesRespectExclusiveTextTiming() {
        auto p=footage();TextOverlay t;t.text="Isaac";t.color="#ff00ff";t.shadow=false;t.startFrame=5;t.durationFrames=20;p.textOverlays={t};
        const auto before=frameAt(p,4),during=frameAt(p,10),after=frameAt(p,25);
        QVERIFY(!before.isNull()&&!during.isNull()&&!after.isNull());
        QCOMPARE(magentaPixels(before),0);QVERIFY(magentaPixels(during)>50);QCOMPARE(magentaPixels(after),0);
    }
    void fadeAppliesToTheCompleteOverlay() {
        auto p=footage();TextOverlay t;t.text="Isaac";t.color="#ff00ff";t.shadow=false;t.fade=true;t.startFrame=0;t.durationFrames=30;p.textOverlays={t};
        QCOMPARE(magentaPixels(frameAt(p,0)),0);
        QVERIFY(magentaPixels(frameAt(p,15))>50);
        QVERIFY(magentaPixels(frameAt(p,29))<magentaPixels(frameAt(p,15)));
    }
    void singleFrameTextRemainsVisibleWithFadeEnabled() {
        auto p=footage();TextOverlay t;t.text="Isaac";t.color="#ff00ff";t.shadow=false;t.fade=true;t.startFrame=10;t.durationFrames=1;p.textOverlays={t};
        QCOMPARE(magentaPixels(frameAt(p,9)),0);QVERIFY(magentaPixels(frameAt(p,10))>50);QCOMPARE(magentaPixels(frameAt(p,11)),0);
    }
    void exportedVideoContainsText() {
        auto p=footage();TextOverlay t;t.text="Isaac";t.color="#ff00ff";t.fontSize=150;t.shadow=false;t.startFrame=5;t.durationFrames=20;p.textOverlays={t};
        QTemporaryDir dir;QString error;const auto output=dir.filePath("text.mp4");
        QVERIFY2(Media::exportMp4(p,output,{},&error),qPrintable(error));
        Project exported;exported.clips={{output,0,.5}};
        QCOMPARE(magentaPixels(frameAt(exported,3)),0);
        QVERIFY(magentaPixels(frameAt(exported,15))>30);
        QCOMPARE(magentaPixels(frameAt(exported,27)),0);
    }
    void captionsBurnIndependentlyAndSurviveMp4Export() {
        auto p=footage();TextOverlay title;title.text="TITLE";title.color="#00ffff";title.y=.2;title.shadow=false;p.textOverlays={title};
        TextOverlay cue;cue.text=QString::fromUtf8("冒険の日 — مرحبا\nIsaac and Daddy");cue.color="#ff00ff";cue.fontSize=80;cue.y=.75;cue.shadow=false;cue.durationFrames=30;p.captions={cue};
        QVERIFY(magentaPixels(frameAt(p,10))>30);
        QTemporaryDir dir;QString error;const auto path=dir.filePath("captions.mp4");QVERIFY2(Media::exportMp4(p,path,{},&error),qPrintable(error));
        Project output;output.clips={{path,0,.5}};QVERIFY(magentaPixels(frameAt(output,10))>30);
        p.burnCaptions=false;QCOMPARE(magentaPixels(frameAt(p,10)),0);
        const auto titleOnly=frameAt(p,10);int cyan=0;for(int y=0;y<titleOnly.height();++y)for(int x=0;x<titleOnly.width();++x){const auto c=titleOnly.pixelColor(x,y);cyan+=c.blue()>160&&c.green()>160&&c.red()<100;}QVERIFY(cyan>30);
    }
    void attachedCaptionsMatchSourceOriginAndScaledPreviewFrames() {
        Project original;original.clips={{QDir::current().filePath("tests/fixtures/seek.mp4"),.5,1.5}};TextOverlay cue;cue.text=QString::fromUtf8("字幕 مرحبا");cue.color="#ff00ff";cue.shadow=false;cue.fontSize=80;cue.startFrame=30;cue.durationFrames=20;original.captions={cue};QVERIFY(original.attachText(0,true,0));
        auto proxy=original;proxy.clips[0].inSeconds-=.25;proxy.clips[0].outSeconds-=.25;proxy.clips[0].sourceFrameOffset=15;
        QCOMPARE(proxy.textStartFrame(0,true),30);QCOMPARE(magentaPixels(frameAt(proxy,29)),0);QVERIFY(magentaPixels(frameAt(proxy,30))>30);QCOMPARE(magentaPixels(frameAt(proxy,50)),0);
        const auto bounds=[](const QImage &image){QRect r;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);if(c.red()>160&&c.blue()>150&&c.green()<110)r=r.united(QRect(x,y,1,1));}return r;};
        const auto full=bounds(frameAt(original,35,1280,720)),small=bounds(frameAt(proxy,35,640,360));QVERIFY(!full.isEmpty()&&!small.isEmpty());QVERIFY(std::abs(full.x()-small.x()*2)<=3);QVERIFY(std::abs(full.y()-small.y()*2)<=3);QVERIFY(std::abs(full.width()-small.width()*2)<=4);QVERIFY(std::abs(full.height()-small.height()*2)<=4);
    }
    void multilineLayoutScalesWithCanvas() {
        TextOverlay t;t.text="Adventure Park Day";const auto one=TextRendering::bounds(t,{1920,1080});
        t.text="Adventure Park Day\nwith Isaac and Daddy";const auto full=TextRendering::bounds(t,{1920,1080}),half=TextRendering::bounds(t,{960,540});
        QVERIFY(full.height()>one.height()*1.5);
        QVERIFY(std::abs(full.width()-half.width()*2)<5);
        QVERIFY(std::abs(full.center().x()-960)<2);QVERIFY(std::abs(half.center().x()-480)<2);
    }
    void wrappingUsesIdenticalLayoutAtProxySizes() {
        TextOverlay t;t.fontSize=45;t.text="Adventure Adventure Adventure Adventure Adventure Adventure Adventure";
        const auto full=TextRendering::bounds(t,{1920,1080});
        for(const auto size:{QSize(960,540),QSize(640,360),QSize(1280,720)}){
            const auto proxy=TextRendering::bounds(t,size);const auto scale=size.height()/1080.0;
            QVERIFY(std::abs(full.height()*scale-proxy.height())<.01);
            QVERIFY(std::abs(full.width()*scale-proxy.width())<.01);
            QVERIFY(std::abs(full.x()*scale-proxy.x())<.01);
            QVERIFY(std::abs(full.y()*scale-proxy.y())<.01);
        }
    }
    void laterOverlaysPaintAboveEarlierOnes() {
        TextOverlay first;first.text="Isaac";first.color="#ff0000";first.shadow=false;
        TextOverlay second=first;second.color="#0000ff";
        QImage image(640,360,QImage::Format_RGBA8888);image.fill(Qt::black);
        TextRendering::paint(image,{first,second},10);
        int blue=0,red=0;
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);blue+=c.blue()>180&&c.red()<50;red+=c.red()>180&&c.blue()<50;}
        QVERIFY(blue>50);QCOMPARE(red,0);
    }
    void captionViewerDragAndSafeGuidesAreEditingOnly() {
        EditorController e;QVERIFY(e.importFiles({QDir::current().filePath("tests/fixtures/rotation.mp4")}));e.addCaption();QTRY_VERIFY(!e.frame().isNull());
        QQuickWindow window;window.resize(800,450);auto *view=new VideoPreview(window.contentItem());view->setSize({800,450});view->setController(&e);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto center=view->captionRect(0).center().toPoint();QVERIFY(!view->captionRect(0).isEmpty());QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,center);QTest::mouseMove(&window,center+QPoint(40,-20));QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,center+QPoint(40,-20));QVERIFY(e.project().captions[0].x>.5);e.undo();QCOMPARE(e.project().captions[0].x,.5);
        const auto image=[&]{QImage result(800,450,QImage::Format_ARGB32);QPainter p(&result);view->paint(&p);return result;};
        e.setTextGuides(false);const auto plain=image();e.setTextGuides(true);QVERIFY(image()!=plain);
    }
    void viewerCancelAndUndoRestorePreviousSelection() {
        EditorController e;QVERIFY(e.importFiles({QDir::current().filePath("tests/fixtures/rotation.mp4")}));e.addText();e.selectClip(0);QTRY_VERIFY(!e.frame().isNull());
        QQuickWindow window;window.resize(800,450);auto *view=new VideoPreview(window.contentItem());view->setSize({800,450});view->setController(&e);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto center=TextRendering::bounds(e.project().textOverlays[0],{800,450}).center().toPoint();
        QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,center);QTest::mouseMove(&window,center+QPoint(40,20));QTest::keyClick(&window,Qt::Key_Escape);QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,center+QPoint(40,20));
        QCOMPARE(e.selectedText(),-1);QCOMPARE(e.selectedClip(),0);
        QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,center);QTest::mouseMove(&window,center+QPoint(40,20));QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,center+QPoint(40,20));
        e.undo();QCOMPARE(e.selectedText(),-1);QCOMPARE(e.selectedClip(),0);
    }
    void viewerDragCommitsOneUndoStepAndEscapeCancels() {
        EditorController e;QVERIFY(e.importFiles({QDir::current().filePath("tests/fixtures/rotation.mp4")}));e.addText();
        QTRY_VERIFY(!e.frame().isNull());
        QQuickWindow window;window.resize(800,450);auto *view=new VideoPreview(window.contentItem());view->setSize({800,450});view->setController(&e);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto center=TextRendering::bounds(e.project().textOverlays[0],{800,450}).center().toPoint();
        QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,center);QTest::mouseMove(&window,center+QPoint(80,45));QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,center+QPoint(80,45));
        QVERIFY(std::abs(e.currentText()["x"].toDouble()-.6)<.01);e.undo();QCOMPARE(e.currentText()["x"].toDouble(),.5);e.redo();
        const auto moved=TextRendering::bounds(e.project().textOverlays[0],{800,450}).center().toPoint();
        QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,moved);QTest::mouseMove(&window,moved+QPoint(40,20));QTest::keyClick(&window,Qt::Key_Escape);QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,moved+QPoint(40,20));
        QVERIFY(std::abs(e.currentText()["x"].toDouble()-.6)<.01);
    }
};
QTEST_MAIN(TextOverlayTest)
#include "text_overlay_test.moc"
