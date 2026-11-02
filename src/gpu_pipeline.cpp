#include "gpu_pipeline.h"
#include "visual_renderer.h"
#include "source_timing.h"
#include "mlt_engine.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>

namespace GpuPipeline {
namespace {
QString executable(const QString &name) {
    const auto path=QStandardPaths::findExecutable(name);
    return path.isEmpty()?QStandardPaths::findExecutable(name,{"/opt/homebrew/bin","/usr/local/bin"}):path;
}
bool run(const QString &program,const QStringList &arguments,QByteArray *output,QString *error,
         const Cancel &canceled={},int timeout=0,const std::function<void(int)> &progress={}) {
    QProcess process;process.start(program,arguments);
    if(!process.waitForStarted(3000)){if(error)*error=process.errorString();return false;}
    QElapsedTimer timer;timer.start();QByteArray errors,buffer;
    while(process.state()!=QProcess::NotRunning){
        const bool stopped=canceled&&canceled();
        if(stopped||(timeout>0&&timer.elapsed()>timeout)){
            process.kill();process.waitForFinished(1000);
            if(error)*error=stopped?"Export canceled.":"FFmpeg GPU operation timed out.";
            return false;
        }
        process.waitForReadyRead(50);errors+=process.readAllStandardError();errors=errors.right(8192);
        const auto data=process.readAllStandardOutput();
        if(output){output->append(data);if(output->size()>32*1024*1024){process.kill();process.waitForFinished(1000);if(error)*error="Source timing metadata exceeds the GPU inspection limit.";return false;}}
        if(progress){buffer+=data;while(buffer.contains('\n')){const int end=buffer.indexOf('\n');const auto line=buffer.left(end);buffer.remove(0,end+1);if(line.startsWith("frame="))progress(line.mid(6).trimmed().toInt());}if(buffer.size()>8192)buffer.clear();}
    }
    errors+=process.readAllStandardError();if(output)output->append(process.readAllStandardOutput());
    if(process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0)return true;
    if(error)*error=QString::fromUtf8(errors.right(8192)).trimmed();return false;
}
bool staticTransform(const VisualTransform &t){
    return t.x.keys.isEmpty()&&t.y.keys.isEmpty()&&t.scale.keys.isEmpty()&&t.rotation.keys.isEmpty()&&t.opacity.keys.isEmpty()
        &&t.rotation.value==0&&t.opacity.value==1&&(t.framing=="fit"||t.framing=="fill");
}
std::optional<QRect> rectangle(const QSize &canvas,const Source &source,const VisualTransform &t){
    const double factor=(t.framing=="fill"?std::max(double(canvas.width())/source.width,double(canvas.height())/source.height)
        :std::min(double(canvas.width())/source.width,double(canvas.height())/source.height))*t.scale.value;
    const double w=source.width*factor,h=source.height*factor,x=t.x.value*canvas.width()-w/2,y=t.y.value*canvas.height()-h/2;
    const auto even=[](double v){return std::isfinite(v)&&std::abs(v-std::round(v/2)*2)<.00001&&std::abs(v)<32768;};
    if(!even(w)||!even(h)||!even(x)||!even(y)||w<2||h<2)return {};
    return QRect(qRound(x),qRound(y),qRound(w),qRound(h));
}
QString number(int frames){return QString::number(double(frames)/Project::fps,'f',9);}
void stage(const Diagnostic &diagnostic,const QString &name,const QString &backend,qint64 ms){
    if(diagnostic)diagnostic("Export stage: "+QString::fromUtf8(QJsonDocument(QJsonObject{{"stage",name},{"backend",backend},{"ms",ms}}).toJson(QJsonDocument::Compact)));
}
bool validVideo(const QString &path,const QSize &size,int frames,QString *error,const Cancel &canceled){
    QByteArray json;
    if(!run(executable("ffprobe"),{"-v","error","-select_streams","v:0","-show_entries","stream=nb_frames,width,height,pix_fmt,r_frame_rate","-of","json",path},&json,error,canceled,10000))return false;
    const auto streams=QJsonDocument::fromJson(json).object()["streams"].toArray();
    if(streams.size()!=1)return false;const auto s=streams.first().toObject();
    const bool ok=s["width"].toInt()==size.width()&&s["height"].toInt()==size.height()&&s["nb_frames"].toString().toInt()==frames&&s["pix_fmt"].toString()=="yuv420p"&&s["r_frame_rate"].toString()=="60/1";
    if(!ok&&error)*error="GPU output failed frame-count, size, cadence or pixel-format validation.";return ok;
}
}
QJsonObject capabilities(){
    const auto ffmpeg=executable("ffmpeg");QByteArray filters,encoders,accels;QString error;
    bool compiled=!ffmpeg.isEmpty()&&!executable("ffprobe").isEmpty()
        &&run(ffmpeg,{"-hide_banner","-filters"},&filters,&error,{},3000)
        &&run(ffmpeg,{"-hide_banner","-encoders"},&encoders,&error,{},3000)
        &&run(ffmpeg,{"-hide_banner","-hwaccels"},&accels,&error,{},3000)
        &&filters.contains("scale_cuda")&&filters.contains("overlay_cuda")&&filters.contains("hwupload")&&encoders.contains("h264_nvenc")&&accels.contains("cuda");
    return {{"backend","ffmpeg-cuda"},{"ffmpeg",ffmpeg},{"compiled",compiled},{"runtimeTested",false},
        {"decode","NVDEC"},{"scale","scale_cuda"},{"compose","overlay_cuda"},{"encode","h264_nvenc"},
        {"reason",compiled?QString{}:QString("FFmpeg CUDA decoding, scale_cuda, overlay_cuda and NVENC are not all available.")}};
}
QString projectSupportReason(const Project &project){
    const auto problem=project.validate();if(!problem.isEmpty())return problem;
    if(project.clips.isEmpty())return "No video clips.";
    if(!project.renderedTexts().isEmpty())return "Text and caption composition uses the MLT renderer.";
    for(int i=0;i<project.clips.size();++i){const auto &clip=project.clips[i];
        if(project.transitionFramesBefore(i)>0)return "Transitions use the MLT renderer.";
        if(clip.rotationDegrees||clip.sourceFrameOffset||!staticTransform(clip.transform))return "Rotation, animated transforms, opacity and cropping use the MLT renderer.";
    }
    for(int i=0;i<project.visualOverlays.size();++i){const auto &o=project.visualOverlays[i];
        if(o.lane>=0&&o.lane<project.visualLaneVisible.size()&&project.visualLaneVisible[o.lane]&&project.overlayFrames(i)>0
            &&(o.sourceFrameOffset||!staticTransform(o.transform)))return "Animated overlay transforms, opacity and cropping use the MLT renderer.";
    }
    return {};
}
std::optional<Source> sourceFromProbe(const QByteArray &json,QString *reason){
    const auto reject=[&](const QString &message)->std::optional<Source>{if(reason)*reason=message;return {};};
    const auto root=QJsonDocument::fromJson(json).object();const auto streams=root["streams"].toArray(),packets=root["packets"].toArray();
    if(streams.size()!=1||packets.size()<1)return reject("Cannot verify source video timing.");
    const auto s=streams.first().toObject();
    if(s["width"].toInt()<2||s["height"].toInt()<2||s["width"].toInt()%2||s["height"].toInt()%2)return reject("GPU processing requires even video dimensions.");
    if(s["r_frame_rate"].toString()!="60/1"||s["avg_frame_rate"].toString()!="60/1")return reject("Non-60fps or variable-rate video uses the MLT timing renderer.");
    if(s["sample_aspect_ratio"].toString()!="1:1"||s["field_order"].toString()!="progressive")return reject("Anamorphic or interlaced video uses the MLT renderer.");
    if(s["codec_name"].toString()!="h264"&&s["codec_name"].toString()!="hevc")return reject("This source codec is not eligible for the NVDEC pipeline.");
    if(s["pix_fmt"].toString()!="yuv420p"||s["color_space"].toString()!="bt709"||s["color_transfer"].toString()!="bt709"||s["color_primaries"].toString()!="bt709"||s["color_range"].toString()!="tv")return reject("HDR, non-709 or untagged video retains the established MLT color conversion.");
    if(s["tags"].toObject().contains("rotate"))return reject("Camera rotation uses the MLT orientation renderer.");
    for(const auto &side:s["side_data_list"].toArray())if(side.toObject().contains("rotation"))return reject("Camera rotation uses the MLT orientation renderer.");
    if(!SourceTiming::verifiedCfr60(json))return reject("Irregular or nonzero-origin source timestamps use the MLT timing renderer.");
    return Source{s["width"].toInt(),s["height"].toInt(),int(packets.size()),false};
}
Plan plan(const Project &project,const QSize &size,const QHash<QString,Source> &sources){
    Plan result;result.size=size;result.reason=projectSupportReason(project);if(!result.reason.isEmpty())return result;
    if(size.width()<2||size.height()<2||size.width()%2||size.height()%2){result.reason="Invalid GPU output dimensions.";return result;}
    QVector<int> boundaries{0,project.durationFrames()};for(int i=0;i<project.clips.size();++i)boundaries.append(project.clipStartFrame(i));
    for(int i=0;i<project.visualOverlays.size();++i){const auto &o=project.visualOverlays[i];if(o.lane<0||o.lane>=project.visualLaneVisible.size()||!project.visualLaneVisible[o.lane])continue;
        boundaries.append(std::clamp(project.overlayStartFrame(i),0,project.durationFrames()));boundaries.append(std::clamp(project.overlayStartFrame(i)+project.overlayFrames(i),0,project.durationFrames()));}
    std::sort(boundaries.begin(),boundaries.end());boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end());
    for(int b=0;b+1<boundaries.size();++b){Segment segment{boundaries[b],boundaries[b+1]-boundaries[b],{}};
        const auto add=[&](const QString &path,int in,const VisualTransform &transform){
            const auto source=sources.constFind(path);if(source==sources.cend()){result.reason="Missing source inspection: "+path;return false;}
            if(!source->still&&(in<0||qint64(in)+segment.frames>source->frames)){result.reason="GPU source trim exceeds verified frames.";return false;}
            auto rect=source->still?std::optional<QRect>(QRect(QPoint{},size)):rectangle(size,*source,transform);
            if(!rect){result.reason="Fractional or odd-pixel composition geometry uses the MLT renderer.";return false;}
            segment.layers.append({path,in,*rect,source->still,transform});return true;
        };
        for(int i=0;i<project.clips.size();++i){const int start=project.clipStartFrame(i);if(segment.start>=start&&segment.start<start+project.clipFrames(project.clips[i])){
            const auto &c=project.clips[i];if(!add(c.path,qRound(c.inSeconds*Project::fps)+segment.start-start,c.transform))return result;break;}}
        for(int lane=0;lane<project.visualLaneVisible.size();++lane)if(project.visualLaneVisible[lane])for(int i=0;i<project.visualOverlays.size();++i){const auto &o=project.visualOverlays[i];const int start=project.overlayStartFrame(i);
            if(o.lane==lane&&segment.start>=start&&segment.start<start+project.overlayFrames(i)&&!add(o.path,o.sourceInFrame+segment.start-start,o.transform))return result;}
        result.segments.append(segment);
    }
    return result;
}
QStringList segmentArguments(const Segment &segment,const QSize &size,const QString &output,bool iphone,int frameLimit){
    const int frames=frameLimit>0?std::min(segment.frames,frameLimit):segment.frames;
    QStringList args{"-hide_banner","-v","error","-nostdin","-y","-init_hw_device","cuda=isaac:0","-filter_hw_device","isaac"};
    args<<"-f"<<"lavfi"<<"-i"<<QString("color=c=black:s=%1x%2:r=60:d=%3").arg(size.width()).arg(size.height()).arg(number(frames));
    for(const auto &layer:segment.layers){
        if(layer.still)args<<"-loop"<<"1"<<"-framerate"<<"60";
        else args<<"-hwaccel"<<"cuda"<<"-hwaccel_device"<<"isaac"<<"-hwaccel_output_format"<<"cuda"<<"-noautorotate";
        args<<"-i"<<layer.path;
    }
    // Generic hwupload reuses the named CUDA device, sharing one context with NVDEC.
    QStringList graph;graph<<"[0:v]format=yuv420p,setparams=range=limited:color_primaries=bt709:color_trc=bt709:colorspace=bt709,hwupload[base0]";
    for(int i=0;i<segment.layers.size();++i){const auto &layer=segment.layers[i];
        const QString processing=layer.still?QString("scale=in_range=full:out_range=tv:out_color_matrix=bt709,format=yuva420p,hwupload"):QString("trim=start_frame=%1:end_frame=%2,setpts=PTS-STARTPTS,scale_cuda=w=%3:h=%4:format=yuv420p:interp_algo=bilinear:passthrough=0").arg(layer.inFrame).arg(layer.inFrame+frames).arg(layer.rectangle.width()).arg(layer.rectangle.height());
        graph<<QString("[%1:v]%2[layer%3]").arg(i+1).arg(processing).arg(i);
        graph<<QString("[base%1][layer%1]overlay_cuda=x=%2:y=%3:shortest=1[base%4]").arg(i).arg(layer.rectangle.x()).arg(layer.rectangle.y()).arg(i+1);
    }
    const qint64 blocks=qint64((size.width()+15)/16)*((size.height()+15)/16);const bool hd=blocks<=8704&&blocks*60<=522240;
    const qint64 maximum=iphone?(hd?16000000:40000000):150000000;
    const qint64 bitrate=std::clamp(qint64(size.width())*size.height()*60*75/1000,qint64(2000000),maximum);
    args<<"-filter_complex"<<graph.join(';')<<"-map"<<QString("[base%1]").arg(segment.layers.size())<<"-an"<<"-frames:v"<<QString::number(frames)<<"-fps_mode"<<"passthrough"<<"-c:v"<<"h264_nvenc"<<"-preset"<<"p4"<<"-rc"<<"vbr"<<"-cq"<<"20"<<"-b:v"<<QString::number(bitrate);
    if(iphone)args<<"-profile:v"<<"high"<<"-level:v"<<(hd?"4.2":"5.2")<<"-maxrate"<<QString::number(maximum)<<"-bufsize"<<QString::number(maximum*2);
    args<<"-color_range"<<"tv"<<"-colorspace"<<"bt709"<<"-color_primaries"<<"bt709"<<"-color_trc"<<"bt709"<<"-video_track_timescale"<<"15360"<<"-progress"<<"pipe:1"<<"-nostats"<<output;
    return args;
}
Result renderVideo(const Project &project,const QSize &size,const QString &output,Cancel canceled,std::function<void(int)> progress,Diagnostic diagnostic,bool iphone,QString *reason){
    QElapsedTimer timer;timer.start();
    const auto fail=[&](Result result,const QString &message){if(reason)*reason=message;if(diagnostic)diagnostic("GPU processing fallback: "+message);return result;};
    const auto emitInfo=[&](const QString &message){if(diagnostic)diagnostic(message);};
    if(canceled&&canceled())return fail(Result::Canceled,"Export canceled.");
    const auto unsupported=projectSupportReason(project);if(!unsupported.isEmpty())return fail(Result::Unavailable,unsupported);
    auto caps=capabilities();emitInfo("GPU capabilities: "+QString::fromUtf8(QJsonDocument(caps).toJson(QJsonDocument::Compact)));
    if(!caps["compiled"].toBool())return fail(Result::Unavailable,caps["reason"].toString());
    QHash<QString,Source> sources;QStringList paths;for(const auto &c:project.clips)paths.append(c.path);
    for(int i=0;i<project.visualOverlays.size();++i){const auto &o=project.visualOverlays[i];if(o.lane>=0&&o.lane<project.visualLaneVisible.size()&&project.visualLaneVisible[o.lane]&&project.overlayFrames(i)>0)paths.append(o.path);}
    paths.removeDuplicates();QHash<QString,QPair<qint64,qint64>> identities;QString error;
    for(const auto &path:paths){if(canceled&&canceled())return fail(Result::Canceled,"Export canceled.");
        if(!QFileInfo(output).canonicalFilePath().isEmpty()&&QFileInfo(output).canonicalFilePath()==QFileInfo(path).canonicalFilePath())return fail(Result::Failed,"GPU output cannot replace source media.");
        const QFileInfo identity(path);identities.insert(path,{identity.size(),identity.lastModified().toMSecsSinceEpoch()});
        QImageReader image(path);image.setAutoTransform(true);if(image.canRead()){if(std::any_of(project.clips.cbegin(),project.clips.cend(),[&](const Clip &c){return c.path==path;}))return fail(Result::Unavailable,"Still timeline clips retain the MLT renderer.");const auto dimensions=image.size();sources.insert(path,{dimensions.width(),dimensions.height(),1,true});continue;}
        QByteArray json;if(!run(executable("ffprobe"),{"-v","error","-select_streams","v:0","-show_streams","-show_entries","packet=pts","-of","json",path},&json,&error,canceled,15000))return fail(canceled&&canceled()?Result::Canceled:Result::Unavailable,error);
        auto source=sourceFromProbe(json,&error);if(!source)return fail(Result::Unavailable,error);sources.insert(path,*source);
    }
    auto prepared=plan(project,size,sources);if(!prepared.supported())return fail(Result::Unavailable,prepared.reason);
    emitInfo("GPU plan: "+QString::fromUtf8(QJsonDocument(QJsonObject{{"eligible",true},{"segments",prepared.segments.size()},{"frames",project.durationFrames()}}).toJson(QJsonDocument::Compact)));
    QTemporaryDir staging(QFileInfo(output).absoluteDir().filePath(".isaac-gpu-XXXXXX"));if(!staging.isValid())return fail(Result::Failed,"Cannot create GPU staging folder.");
    int stillIndex=0;for(auto &segment:prepared.segments)for(auto &layer:segment.layers)if(layer.still){
        QImageReader reader(layer.path);reader.setAutoTransform(true);const auto image=reader.read();if(image.isNull())return fail(Result::Failed,"Cannot decode overlay image.");
        QImage canvas(size,QImage::Format_ARGB32_Premultiplied);canvas.fill(Qt::transparent);VisualRendering::paint(canvas,image,layer.transform,0);
        layer.path=staging.filePath(QString("still-%1.png").arg(stillIndex++));if(!canvas.save(layer.path))return fail(Result::Failed,"Cannot prepare transparent overlay.");
    }
    stage(diagnostic,"inspection","ffmpeg-cuda",timer.restart());
    emitInfo("GPU processing probe: NVDEC + scale_cuda + overlay_cuda + h264_nvenc");
    const auto probe=staging.filePath("probe.mp4");
    if(!run(executable("ffmpeg"),segmentArguments(prepared.segments.first(),size,probe,iphone,4),nullptr,&error,canceled,8000)
        ||!validVideo(probe,size,std::min(4,prepared.segments.first().frames),&error,canceled)){
        const bool stopped=canceled&&canceled();caps["runtimeTested"]=!stopped;caps["runtimeAvailable"]=stopped?QJsonValue{}:QJsonValue(false);caps["reason"]=error;emitInfo("GPU capabilities: "+QString::fromUtf8(QJsonDocument(caps).toJson(QJsonDocument::Compact)));
        return fail(stopped?Result::Canceled:Result::Unavailable,error);
    }
    stage(diagnostic,"probe","ffmpeg-cuda",timer.restart());
    caps["runtimeTested"]=true;caps["runtimeAvailable"]=true;emitInfo("GPU capabilities: "+QString::fromUtf8(QJsonDocument(caps).toJson(QJsonDocument::Compact)));
    emitInfo("GPU processing probe succeeded: ffmpeg-cuda");emitInfo("Encoder: h264_nvenc");emitInfo("Processing: NVDEC decoding; CUDA scaling and composition; NVENC encoding; MLT audio");
    QFile manifest(staging.filePath("segments.txt"));if(!manifest.open(QIODevice::WriteOnly))return fail(Result::Failed,"Cannot prepare GPU segment list.");
    for(int i=0;i<prepared.segments.size();++i){const auto &segment=prepared.segments[i];const QString name=QString("segment-%1.mp4").arg(i),path=staging.filePath(name);
        if(!run(executable("ffmpeg"),segmentArguments(segment,size,path,iphone),nullptr,&error,canceled,0,[&](int frame){if(progress)progress(std::min(95,(segment.start+frame)*95/project.durationFrames()));})||!validVideo(path,size,segment.frames,&error,canceled))return fail(canceled&&canceled()?Result::Canceled:Result::Failed,error);
        manifest.write(("file '"+name+"'\n").toUtf8());
    }
    manifest.close();
    stage(diagnostic,"video","ffmpeg-cuda",timer.restart());
    for(auto it=identities.cbegin();it!=identities.cend();++it){const QFileInfo now(it.key());if(now.size()!=it->first||now.lastModified().toMSecsSinceEpoch()!=it->second)return fail(Result::Failed,"Source media changed during GPU export.");}
    if(!run(executable("ffmpeg"),{"-hide_banner","-v","error","-nostdin","-y","-f","concat","-safe","1","-i",manifest.fileName(),"-map","0:v:0","-c:v","copy","-movflags","+faststart",output},nullptr,&error,canceled)
        ||!validVideo(output,size,project.durationFrames(),&error,canceled))return fail(canceled&&canceled()?Result::Canceled:Result::Failed,error);
    stage(diagnostic,"concat","ffmpeg-cuda",timer.elapsed());
    return Result::Completed;
}
bool renderAudio(const Project &project,const QSize &size,const QString &output,Cancel canceled,std::function<void(int)> progress,QString *error,Diagnostic diagnostic){
    const auto stopped=[&]{if(canceled&&canceled()){if(error)*error="Export canceled.";return true;}return false;};
    if(stopped())return false;
    Media::initializeMlt();
    Mlt::Profile profile;Media::configureOutputProfile(profile,size.width(),size.height());
    QElapsedTimer timer;timer.start();auto timeline=Media::buildTimeline(project,profile,error);if(!timeline||stopped())return false;
    stage(diagnostic,"timeline","mlt",timer.restart());
    const auto path=QFile::encodeName(output);Mlt::Consumer consumer(profile,"avformat",path.constData());
    consumer.set("f","mp4");consumer.set("acodec","aac");consumer.set("vn",1);consumer.set("video_off",1);
    consumer.set("real_time",-1);consumer.set("buffer",1);consumer.set("prefill",1);
    if(!consumer.is_valid()||consumer.connect(*timeline)!=0||consumer.start()!=0){if(error)*error="Could not start the MLT audio renderer.";return false;}
    while(!consumer.is_stopped()){
        if(stopped()){consumer.stop();return false;}
        if(progress)progress(std::clamp(consumer.position()*100/std::max(1,project.durationFrames()),0,99));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    consumer.stop();
    if(stopped()||consumer.get_int("error")||QFileInfo(output).size()<=0){if(error&&error->isEmpty())*error="MLT audio rendering failed.";return false;}
    stage(diagnostic,"audio","mlt",timer.elapsed());
    return true;
}
bool muxAudio(const QString &video,const QString &audio,const QString &output,int frames,Cancel canceled,QString *error){
    return run(executable("ffmpeg"),{"-hide_banner","-v","error","-nostdin","-y","-i",video,"-i",audio,"-map","0:v:0","-map","1:a:0","-c","copy","-t",number(frames),"-movflags","+faststart",output},nullptr,error,canceled);
}
}
