#include "gpu_pipeline.h"
#include "mlt_engine.h"
#include "visual_renderer.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdio>

namespace {
QStringList cpuEquivalentArguments(QStringList args){
    for(const auto &option:{"-init_hw_device","-filter_hw_device","-hwaccel","-hwaccel_device","-hwaccel_output_format"})while(args.contains(option)){int i=args.indexOf(option);args.removeAt(i);args.removeAt(i);}
    const int graphIndex=args.indexOf("-filter_complex")+1;
    if(graphIndex>0)args[graphIndex].replace(",hwupload","").replace("scale_cuda=","scale=").replace(":interp_algo=bilinear:passthrough=0",":flags=bilinear").replace(":format=yuv420p","").replace("overlay_cuda=","overlay=");
    if(args.contains("h264_nvenc")){
        args[args.indexOf("h264_nvenc")]="libx264";args[args.indexOf("p4")]="ultrafast";
        for(const auto &option:{"-rc","-cq"}){int i=args.indexOf(option);args.removeAt(i);args.removeAt(i);}
    }
    return args;
}
}

class GpuPipelineTest:public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    QString ffmpeg,ffprobe,video;
    bool run(const QString &program,const QStringList &args,QByteArray *out=nullptr){QProcess p;p.start(program,args);if(!p.waitForFinished(15000)){p.kill();p.waitForFinished();return false;}if(out)*out=p.readAllStandardOutput();if(p.exitCode())qWarning().noquote()<<p.readAllStandardError();return p.exitStatus()==QProcess::NormalExit&&p.exitCode()==0;}
    QByteArray inspect(const QString &path){QByteArray out;run(ffprobe,{"-v","error","-select_streams","v:0","-show_streams","-show_entries","packet=pts","-of","json",path},&out);return out;}
    Project project(){Project p;p.canvasWidth=320;p.canvasHeight=192;p.clips={{video,0,1},{video,1,2}};return p;}
    bool script(const QString &path,const QByteArray &body){QFile file(path);if(!file.open(QIODevice::WriteOnly))return false;file.write(body);file.close();return file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);}
    QStringList cpuArguments(const GpuPipeline::Segment &segment,const QSize &size,const QString &output){
        return cpuEquivalentArguments(GpuPipeline::segmentArguments(segment,size,output));
    }
    QImage firstFrame(const QString &path,int position=0){QByteArray pixels;if(!run(ffmpeg,{"-v","error","-i",path,"-vf",QString("select=eq(n\\,%1)").arg(position),"-frames:v","1","-f","rawvideo","-pix_fmt","rgb24","pipe:1"},&pixels)||pixels.size()!=320*192*3)return {};return QImage(reinterpret_cast<const uchar*>(pixels.constData()),320,192,320*3,QImage::Format_RGB888).copy();}
    QImage mltFrame(const Project &p,int position){
        Media::initializeMlt();Mlt::Profile profile;Media::configureOutputProfile(profile,320,192);auto timeline=Media::buildTimeline(p,profile);if(!timeline)return {};
        timeline->seek(position);std::unique_ptr<Mlt::Frame> frame(timeline->get_frame());int w=320,h=192;auto format=mlt_image_rgba;uint8_t *pixels=nullptr;
        if(!frame||mlt_frame_get_image(frame->get_frame(),&pixels,&format,&w,&h,0)||!pixels)return {};
        return QImage(pixels,w,h,w*4,QImage::Format_RGBA8888).copy();
    }
private slots:
    void initTestCase(){
        ffmpeg=QStandardPaths::findExecutable("ffmpeg");ffprobe=QStandardPaths::findExecutable("ffprobe");if(ffmpeg.isEmpty()||ffprobe.isEmpty())QSKIP("FFmpeg tools are needed for GPU graph and fallback integration checks.");
        video=directory.filePath("source.mp4");
        QVERIFY(run(ffmpeg,{"-v","error","-f","lavfi","-i","color=red:s=160x96:r=60:d=2","-an","-c:v","libx264","-preset","ultrafast","-pix_fmt","yuv420p","-color_range","tv","-x264-params","colorprim=bt709:transfer=bt709:colormatrix=bt709","-y",video}));
    }
    void verifiesRealPacketCadenceAndColor(){
        QString reason;auto source=GpuPipeline::sourceFromProbe(inspect(video),&reason);QVERIFY2(source,qPrintable(reason));QCOMPARE(source->frames,120);QCOMPARE(source->width,160);
        auto root=QJsonDocument::fromJson(inspect(video)).object();auto streams=root["streams"].toArray();auto stream=streams[0].toObject();stream["color_transfer"]="smpte2084";streams[0]=stream;root["streams"]=streams;
        QVERIFY(!GpuPipeline::sourceFromProbe(QJsonDocument(root).toJson(),&reason));QVERIFY(reason.contains("color"));
        root=QJsonDocument::fromJson(inspect(video)).object();auto packets=root["packets"].toArray();auto packet=packets[30].toObject();packet["pts"]=packet["pts"].toInteger()+17;packets[30]=packet;root["packets"]=packets;
        QVERIFY(!GpuPipeline::sourceFromProbe(QJsonDocument(root).toJson(),&reason));QVERIFY(reason.contains("timestamps"));
    }
    void splitsAtClipAndOverlayBoundariesInLaneOrder(){
        auto p=project();VisualOverlay top;top.path="top.png";top.lane=2;top.startFrame=30;top.durationFrames=60;
        VisualOverlay bottom;bottom.path=video;bottom.lane=0;bottom.startFrame=30;bottom.durationFrames=60;bottom.transform.scale.value=.5;
        p.visualOverlays={top,bottom};QHash<QString,GpuPipeline::Source> sources{{video,{160,96,120,false}},{"top.png",{10,10,1,true}}};
        const auto result=GpuPipeline::plan(p,{320,192},sources);QVERIFY2(result.supported(),qPrintable(result.reason));QCOMPARE(result.segments.size(),4);
        QCOMPARE(result.segments[0].frames,30);QCOMPARE(result.segments[1].layers.size(),3);QCOMPARE(result.segments[1].layers[1].path,video);QCOMPARE(result.segments[1].layers[2].path,QString("top.png"));
        QCOMPARE(result.segments[2].layers[0].inFrame,60);QCOMPARE(result.segments[2].layers[1].inFrame,30);QCOMPARE(result.segments[2].layers[1].rectangle,QRect(80,48,160,96));
        QCOMPARE(result.segments[3].layers.size(),1);
    }
    void rejectsUnsupportedEffectsWithoutChangingTheirOutput(){
        auto p=project();p.crossfadeFrames=10;QVERIFY(GpuPipeline::projectSupportReason(p).contains("Transitions"));
        p=project();p.clips[0].rotationDegrees=90;QVERIFY(!GpuPipeline::projectSupportReason(p).isEmpty());
        p=project();p.textOverlays.append(TextOverlay{});QVERIFY(GpuPipeline::projectSupportReason(p).contains("Text"));
        p=project();p.clips[0].transform.x.setKey(10,.6);QVERIFY(!GpuPipeline::projectSupportReason(p).isEmpty());
        p=project();p.clips[0].transform.scale.value=.51;const auto plan=GpuPipeline::plan(p,{320,192},{{video,{160,96,120,false}}});QVERIFY(!plan.supported());QVERIFY(plan.reason.contains("geometry"));
        p=project();p.clips[1].outSeconds=2.1;QVERIFY(!GpuPipeline::plan(p,{320,192},{{video,{160,96,120,false}}}).supported());
    }
    void generatedGraphPerformsRealTrimScaleAndOverlay(){
        // Exercise graph timing/geometry with FFmpeg's CPU equivalents on hosts without CUDA.
        auto p=project();p.clips[0].transform.scale.value=.5;auto plan=GpuPipeline::plan(p,{320,192},{{video,{160,96,120,false}}});QVERIFY(plan.supported());
        plan.segments[0].layers[0].inFrame=20;plan.segments[0].frames=7;
        auto args=GpuPipeline::segmentArguments(plan.segments[0],plan.size,directory.filePath("reference.mp4"));
        const auto gpu=args.join(' ');QVERIFY(gpu.contains("-hwaccel cuda"));QVERIFY(gpu.contains("scale_cuda"));QVERIFY(gpu.contains("overlay_cuda"));QVERIFY(gpu.contains("start_frame=20:end_frame=27"));
        args=cpuArguments(plan.segments[0],plan.size,directory.filePath("reference.mp4"));
        QVERIFY(run(ffmpeg,args));QByteArray frame;QVERIFY(run(ffmpeg,{"-v","error","-i",directory.filePath("reference.mp4"),"-frames:v","1","-f","rawvideo","-pix_fmt","rgb24","pipe:1"},&frame));
        QCOMPARE(frame.size(),320*192*3);const auto red=[&](int x,int y){const int offset=(y*320+x)*3;return uchar(frame[offset])>220&&uchar(frame[offset+1])<30&&uchar(frame[offset+2])<30;};
        QVERIFY(red(160,96));QVERIFY(!red(10,10));
        const auto packets=QJsonDocument::fromJson(inspect(directory.filePath("reference.mp4"))).object()["packets"].toArray();QCOMPARE(packets.size(),7);
    }
    void transparentStillCompositionMatchesMltGeometryAndColor(){
        const auto still=directory.filePath("alpha.png");QImage image(32,32,QImage::Format_ARGB32);image.fill(QColor(0,40,255,128));QVERIFY(image.save(still));
        auto p=project();p.clips[0].transform.scale.value=.5;VisualOverlay overlay;overlay.path=still;overlay.durationFrames=20;overlay.transform.scale.value=.25;overlay.transform.x.value=.75;p.visualOverlays={overlay};
        auto prepared=GpuPipeline::plan(p,{320,192},{{video,{160,96,120,false}},{still,{32,32,1,true}}});QVERIFY2(prepared.supported(),qPrintable(prepared.reason));
        QImage painted(320,192,QImage::Format_ARGB32_Premultiplied);painted.fill(Qt::transparent);VisualRendering::paint(painted,image,overlay.transform,0);
        const auto asset=directory.filePath("prepared-alpha.png");QVERIFY(painted.save(asset));prepared.segments[0].layers[1].path=asset;
        const auto output=directory.filePath("alpha-reference.mp4");QVERIFY(run(ffmpeg,cpuArguments(prepared.segments[0],prepared.size,output)));
        const auto actual=firstFrame(output),expected=mltFrame(p,0);QVERIFY(!actual.isNull());QCOMPARE(actual.size(),expected.size());
        double difference=0;for(int y=0;y<192;++y)for(int x=0;x<320;++x){const auto a=actual.pixelColor(x,y),b=expected.pixelColor(x,y);difference+=std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue());}
        difference/=320*192*3;qInfo()<<"CPU analogue vs MLT alpha/transform mean RGB difference:"<<difference;QVERIFY2(difference<5,qPrintable(QString::number(difference)));
    }
    void patternedTrimAndTransformMatchMlt(){
        const auto pattern=directory.filePath("pattern.mp4");QVERIFY(run(ffmpeg,{"-v","error","-f","lavfi","-i","testsrc2=s=160x96:r=60:d=1","-an","-c:v","libx264","-preset","ultrafast","-pix_fmt","yuv420p","-color_range","tv","-x264-params","colorprim=bt709:transfer=bt709:colormatrix=bt709","-y",pattern}));
        auto p=project();p.clips={{pattern,.2,.7}};p.clips[0].transform.scale.value=.5;p.clips[0].transform.x.value=.6;
        const auto prepared=GpuPipeline::plan(p,{320,192},{{pattern,{160,96,60,false}}});QVERIFY(prepared.supported());
        const auto output=directory.filePath("pattern-reference.mp4");QVERIFY(run(ffmpeg,cpuArguments(prepared.segments[0],prepared.size,output)));
        const auto actual=firstFrame(output),expected=mltFrame(p,0);QVERIFY(!actual.isNull());QCOMPARE(actual.size(),expected.size());double difference=0;
        for(int y=0;y<192;++y)for(int x=0;x<320;++x){const auto a=actual.pixelColor(x,y),b=expected.pixelColor(x,y);difference+=std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue());}
        difference/=320*192*3;qInfo()<<"Patterned source trim/transform mean RGB difference:"<<difference;QVERIFY2(difference<5,qPrintable(QString::number(difference)));
    }
    void audioStageMatchesNormalMltExportAndMuxTiming(){
        const auto voice=directory.filePath("voice.wav"),music=directory.filePath("music.wav");
        QVERIFY(run(ffmpeg,{"-v","error","-f","lavfi","-i","sine=frequency=440:sample_rate=48000:duration=1.2","-y",voice}));
        QVERIFY(run(ffmpeg,{"-v","error","-f","lavfi","-i","sine=frequency=220:sample_rate=48000:duration=0.3","-y",music}));
        auto p=project();p.voicePath=voice;p.voiceStartSeconds=.25;p.voiceAudio.gainDb=-3;p.voiceAudio.fadeInFrames=9;p.voiceAudio.fadeOutFrames=12;p.musicPath=music;p.musicDb=-12;p.musicLoopToEnd=true;p.musicFadeInFrames=6;p.musicFadeOutFrames=20;p.clips[0].audio.muted=true;
        QString error;const auto audio=directory.filePath("audio.m4a"),reference=directory.filePath("normal-export.mp4");
        QVERIFY2(GpuPipeline::renderAudio(p,{320,192},audio,{}, {},&error),qPrintable(error));
        QVERIFY2(Media::exportMp4(p,reference,{},&error,{}, {},true),qPrintable(error));
        const auto decode=[&](const QString &path){QByteArray samples;run(ffmpeg,{"-v","error","-i",path,"-vn","-f","f32le","-ac","2","-ar","48000","pipe:1"},&samples);return samples;};
        const auto expected=decode(reference),actual=decode(audio);QVERIFY(!actual.isEmpty());QCOMPARE(actual.size(),expected.size());
        double signal=0,noise=0;for(int i=0;i<actual.size()/int(sizeof(float));++i){float a,b;memcpy(&a,actual.constData()+i*sizeof(float),sizeof(float));memcpy(&b,expected.constData()+i*sizeof(float),sizeof(float));signal+=double(b)*b;noise+=double(a-b)*(a-b);}
        qInfo()<<"MLT audio-stage normalized error:"<<noise/std::max(.0001,signal);QVERIFY(noise/std::max(.0001,signal)<.001);
        const auto muxed=directory.filePath("muxed.mp4");QVERIFY2(GpuPipeline::muxAudio(reference,audio,muxed,120,{},&error),qPrintable(error));QCOMPARE(decode(muxed),actual);
        const auto info=Media::mediaInfo(muxed);QVERIFY(info);QCOMPARE(qRound(info->durationSeconds*60),120);
        auto silent=project();silent.clips={{video,0,.1}};const auto silence=directory.filePath("silent.m4a");
        QVERIFY2(GpuPipeline::renderAudio(silent,{320,192},silence,{}, {},&error),qPrintable(error));const auto silentSamples=decode(silence);QVERIFY(!silentSamples.isEmpty());
        for(int i=0;i<silentSamples.size()/int(sizeof(float));++i){float sample;memcpy(&sample,silentSamples.constData()+i*sizeof(float),sizeof(float));QVERIFY(std::abs(sample)<.00001);}
    }
    void missingDeviceCannotPublishOrDamageOutput(){
#ifndef Q_OS_UNIX
        QSKIP("Process wrapper uses a POSIX shell.");
#else
        const auto tools=directory.filePath("tools");QVERIFY(QDir().mkpath(tools));
        QVERIFY(script(tools+"/ffmpeg","#!/bin/sh\ncase \" $* \" in\n*' -filters '*) echo 'scale_cuda overlay_cuda hwupload_cuda';;\n*' -encoders '*) echo 'h264_nvenc';;\n*' -hwaccels '*) echo 'cuda';;\n*) echo 'GPU device initialization failed' >&2; exit 1;;\nesac\n"));
        const auto previous=qgetenv("PATH");qputenv("PATH",QFile::encodeName(tools)+':'+previous);const auto restore=qScopeGuard([&]{qputenv("PATH",previous);});
        const auto output=directory.filePath("existing.mp4");QFile f(output);QVERIFY(f.open(QIODevice::WriteOnly));f.write("KEEP");f.close();QString error;QStringList diagnostics;
        QCOMPARE(GpuPipeline::renderVideo(project(),{320,192},output,{}, {},[&](const QString &s){diagnostics.append(s);},false,&error),GpuPipeline::Result::Unavailable);
        QVERIFY(error.contains("initialization failed"));QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),QByteArray("KEEP"));QVERIFY(diagnostics.join('\n').contains("GPU processing fallback:"));
#endif
    }
    void deviceLossAfterProbePreservesDestinationAndCleansStaging(){
#ifndef Q_OS_UNIX
        QSKIP("Process wrapper uses a POSIX shell.");
#else
        const auto stub=directory.filePath("probe-stub.mp4");QVERIFY(run(ffmpeg,{"-v","error","-i",video,"-vf","scale=320:192","-frames:v","4","-an","-c:v","libx264","-pix_fmt","yuv420p","-y",stub}));
        const auto tools=directory.filePath("loss-tools");QVERIFY(QDir().mkpath(tools));
        const auto wrapper=QByteArray("#!/bin/sh\ncase \" $* \" in\n*' -filters '*) echo 'scale_cuda overlay_cuda hwupload_cuda'; exit 0;;\n*' -encoders '*) echo 'h264_nvenc'; exit 0;;\n*' -hwaccels '*) echo 'cuda'; exit 0;;\nesac\nfor last do :; done\ncase \"$last\" in\n*/probe.mp4) cp \"")+QFile::encodeName(stub)+"\" \"$last\";;\n*) printf 'partial' > \"$last\"; echo 'simulated device loss after probe' >&2; exit 9;;\nesac\n";
        QVERIFY(script(tools+"/ffmpeg",wrapper));const auto previous=qgetenv("PATH");qputenv("PATH",QFile::encodeName(tools)+':'+previous);const auto restore=qScopeGuard([&]{qputenv("PATH",previous);});
        const auto output=directory.filePath("device-loss.mp4");QFile file(output);QVERIFY(file.open(QIODevice::WriteOnly));file.write("KEEP");file.close();QString error;QStringList diagnostics;
        QCOMPARE(GpuPipeline::renderVideo(project(),{320,192},output,{}, {},[&](const QString &s){diagnostics.append(s);},false,&error),GpuPipeline::Result::Failed);
        QVERIFY2(error.contains("device loss"),qPrintable(error));QVERIFY(diagnostics.join('\n').contains("GPU processing probe succeeded"));QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("KEEP"));
        QVERIFY(QDir(directory.path()).entryList({".isaac-gpu-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
#endif
    }
    void cancellationStopsAnInFlightGpuProbe(){
#ifndef Q_OS_UNIX
        QSKIP("Process wrapper uses a POSIX shell.");
#else
        const auto tools=directory.filePath("cancel-tools");QVERIFY(QDir().mkpath(tools));
        QVERIFY(script(tools+"/ffmpeg","#!/bin/sh\ncase \" $* \" in\n*' -filters '*) echo 'scale_cuda overlay_cuda hwupload_cuda';;\n*' -encoders '*) echo 'h264_nvenc';;\n*' -hwaccels '*) echo 'cuda';;\n*) exec /bin/sleep 30;;\nesac\n"));
        const auto previous=qgetenv("PATH");qputenv("PATH",QFile::encodeName(tools)+':'+previous);const auto restore=qScopeGuard([&]{qputenv("PATH",previous);});
        bool probing=false;QElapsedTimer timer;QString error;
        QCOMPARE(GpuPipeline::renderVideo(project(),{320,192},directory.filePath("canceled.mp4"),[&]{return probing&&timer.elapsed()>200;},{},[&](const QString &s){if(s.startsWith("GPU processing probe: ")){probing=true;timer.start();}},false,&error),GpuPipeline::Result::Canceled);
        QVERIFY(probing);QVERIFY(timer.elapsed()<2000);QVERIFY(!QFileInfo::exists(directory.filePath("canceled.mp4")));
#endif
    }
    void capabilitiesSeparateCompilationFromRuntime(){const auto caps=GpuPipeline::capabilities();QCOMPARE(caps["backend"].toString(),QString("ffmpeg-cuda"));QVERIFY(caps["compiled"].isBool());QCOMPARE(caps["runtimeTested"].toBool(),false);}
    void completeSegmentTimingAndAlphaWithCpuEquivalentDeviceOperations(){
#ifndef Q_OS_UNIX
        QSKIP("The CPU-equivalent process wrapper uses a POSIX executable link.");
#else
        const auto tools=directory.filePath("cpu-equivalent-tools");QVERIFY(QDir().mkpath(tools));QVERIFY(QFile::link(QCoreApplication::applicationFilePath(),tools+"/ffmpeg"));
        const auto previous=qgetenv("PATH"),previousEquivalent=qgetenv("ISAAC_TEST_CPU_EQUIVALENT_FFMPEG");
        qputenv("PATH",QFile::encodeName(tools)+':'+previous);qputenv("ISAAC_TEST_CPU_EQUIVALENT_FFMPEG",QFile::encodeName(ffmpeg));
        const auto restore=qScopeGuard([&]{qputenv("PATH",previous);if(previousEquivalent.isNull())qunsetenv("ISAAC_TEST_CPU_EQUIVALENT_FFMPEG");else qputenv("ISAAC_TEST_CPU_EQUIVALENT_FFMPEG",previousEquivalent);});
        const auto still=directory.filePath("end-to-end-alpha.png");QImage image(32,32,QImage::Format_ARGB32);image.fill(QColor(0,40,255,128));QVERIFY(image.save(still));
        auto p=project();p.clips[0].transform.scale.value=.5;VisualOverlay overlay;overlay.path=video;overlay.startFrame=20;overlay.durationFrames=50;overlay.transform.scale.value=.25;overlay.transform.x.value=.75;
        VisualOverlay alpha;alpha.path=still;alpha.startFrame=40;alpha.durationFrames=60;alpha.lane=2;alpha.transform.scale.value=.25;alpha.transform.x.value=.25;p.visualOverlays={overlay,alpha};
        QString error;const auto output=directory.filePath("cpu-equivalent-complete.mp4");QVERIFY2(GpuPipeline::renderVideo(p,{320,192},output,{}, {},{},false,&error)==GpuPipeline::Result::Completed,qPrintable(error));
        for(int frame:{0,19,20,39,40,59,60,69,70,99,100,119}){const auto actual=firstFrame(output,frame),expected=mltFrame(p,frame);QVERIFY(!actual.isNull());QCOMPARE(actual.size(),expected.size());double difference=0;
            for(int y=0;y<192;++y)for(int x=0;x<320;++x){const auto a=actual.pixelColor(x,y),b=expected.pixelColor(x,y);difference+=std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue());}
            difference/=320*192*3;QVERIFY2(difference<5,qPrintable(QString("CPU equivalent frame %1 mean RGB difference %2").arg(frame).arg(difference)));}
        const auto packets=QJsonDocument::fromJson(inspect(output)).object()["packets"].toArray();QCOMPARE(packets.size(),120);
#endif
    }
    void actualCudaAcceptance_data(){QTest::addColumn<bool>("iphone");QTest::newRow("native")<<false;QTest::newRow("iphone")<<true;}
    void actualCudaAcceptance(){
        QFETCH(bool,iphone);
        if(!qEnvironmentVariableIsSet("ISAAC_RUN_CUDA_ACCEPTANCE"))QSKIP("Set ISAAC_RUN_CUDA_ACCEPTANCE=1 on an NVIDIA workstation; this is not a hardware success.");
        const auto still=directory.filePath("acceptance-alpha.png");QImage image(32,32,QImage::Format_ARGB32);image.fill(QColor(0,40,255,128));QVERIFY(image.save(still));
        auto p=project();p.clips[0].transform.scale.value=.5;VisualOverlay overlay;overlay.path=video;overlay.startFrame=20;overlay.durationFrames=50;overlay.transform.scale.value=.25;overlay.transform.x.value=.75;
        VisualOverlay alpha;alpha.path=still;alpha.startFrame=40;alpha.durationFrames=60;alpha.lane=2;alpha.transform.scale.value=.25;alpha.transform.x.value=.25;p.visualOverlays={overlay,alpha};
        QString error;QStringList diagnostics;const auto output=directory.filePath("cuda.mp4");
        QVERIFY2(GpuPipeline::renderVideo(p,{320,192},output,{}, {},[&](const QString &s){diagnostics.append(s);},iphone,&error)==GpuPipeline::Result::Completed,qPrintable(error));
        QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(diagnostics.join('\n').contains("GPU processing probe succeeded"));
        const auto info=Media::mediaInfo(output);QVERIFY(info);QCOMPARE(qRound(info->durationSeconds*60),120);
        for(int frame:{0,19,20,39,40,59,60,69,70,99,100,119}){const auto actual=firstFrame(output,frame),expected=mltFrame(p,frame);QVERIFY(!actual.isNull());QCOMPARE(actual.size(),expected.size());double difference=0;
            for(int y=0;y<192;++y)for(int x=0;x<320;++x){const auto a=actual.pixelColor(x,y),b=expected.pixelColor(x,y);difference+=std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue());}
            difference/=320*192*3;QVERIFY2(difference<5,qPrintable(QString("Frame %1 mean RGB difference %2").arg(frame).arg(difference)));}
    }
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    const auto equivalent=qgetenv("ISAAC_TEST_CPU_EQUIVALENT_FFMPEG");
    if(!equivalent.isEmpty()){
        const auto args=app.arguments().mid(1);
        if(args.contains("-filters")||args.contains("-encoders")||args.contains("-hwaccels")){fputs("scale_cuda overlay_cuda hwupload h264_nvenc cuda\n",stdout);return 0;}
        QProcess process;process.setProcessChannelMode(QProcess::ForwardedChannels);process.start(QString::fromUtf8(equivalent),cpuEquivalentArguments(args));if(!process.waitForFinished(30000)){process.kill();process.waitForFinished();return 1;}return process.exitStatus()==QProcess::NormalExit?process.exitCode():1;
    }
    GpuPipelineTest test;return QTest::qExec(&test,argc,argv);
}
#include "gpu_pipeline_test.moc"
