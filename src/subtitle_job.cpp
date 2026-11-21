#include "subtitle_job.h"
#include "captions.h"
#include "gpu_pipeline.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringConverter>
#include <QThread>
#include <QUuid>
#include <cmath>
#include <cstdio>
#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
QString normalized(const QString &path){QFileInfo f(path);return f.exists()?f.canonicalFilePath():f.absoluteFilePath();}
QPair<qint64,qint64> identity(const QString &path){const QFileInfo f(path);return f.isFile()?qMakePair(f.size(),f.lastModified().toMSecsSinceEpoch()):qMakePair(qint64(-1),qint64(-1));}
void event(const QJsonObject &value){const auto data=QJsonDocument(value).toJson(QJsonDocument::Compact)+'\n';fwrite(data.constData(),1,data.size(),stdout);fflush(stdout);}
QString simplified(const QString &text){
#ifdef Q_OS_MACOS
    auto original=CFStringCreateWithCharacters(nullptr,reinterpret_cast<const UniChar*>(text.utf16()),text.size());
    auto result=CFStringCreateMutableCopy(nullptr,0,original);CFRelease(original);
    const bool ok=CFStringTransform(result,nullptr,CFSTR("Traditional-Simplified"),false);
    QString output(CFStringGetLength(result),Qt::Uninitialized);CFStringGetCharacters(result,CFRangeMake(0,output.size()),reinterpret_cast<UniChar*>(output.data()));CFRelease(result);return ok?output:QString();
#elif defined(Q_OS_WIN)
    const auto input=reinterpret_cast<LPCWSTR>(text.utf16());
    const int length=LCMapStringEx(L"zh-CN",LCMAP_SIMPLIFIED_CHINESE,input,int(text.size()),nullptr,0,nullptr,nullptr,0);
    if(!length)return {};QString output(length,Qt::Uninitialized);
    return LCMapStringEx(L"zh-CN",LCMAP_SIMPLIFIED_CHINESE,input,int(text.size()),reinterpret_cast<LPWSTR>(output.data()),length,nullptr,nullptr,0)?output:QString();
#else
    return text;
#endif
}
}
std::optional<QByteArray> Subtitles::whisperSrt(const QByteArray &json,int frames,QStringList *warnings,QString *error){
    const auto fail=[&](const QString &message)->std::optional<QByteArray>{if(error)*error=message;return {};};
    if(frames<=0||frames>86400*60||json.size()>128*1024*1024)return fail("Invalid transcription size or duration.");
    QStringDecoder decoder(QStringDecoder::Utf8);decoder(json);if(decoder.hasError())return fail("The transcription contains invalid UTF-8.");
    QJsonParseError parseError;const auto document=QJsonDocument::fromJson(json,&parseError);
    if(parseError.error!=QJsonParseError::NoError||!document.isObject()||!document.object()["transcription"].isArray())return fail("The transcription worker returned invalid JSON.");
    if(document.object()["params"].toObject()["translate"].toBool())return fail("The worker translated the recording instead of transcribing it.");
    const auto segments=document.object()["transcription"].toArray();if(segments.size()>10000)return fail("A project supports up to 10000 captions.");
    Project project;int previous=-1,previousEnd=0;QStringList brief;
    for(const auto &entry:segments){
        const auto value=entry.toObject();const auto offsets=value["offsets"].toObject();
        if(!value["text"].isString()||!offsets["from"].isDouble()||!offsets["to"].isDouble())return fail("A transcription segment is incomplete.");
        const double start=offsets["from"].toDouble(),end=offsets["to"].toDouble();
        if(!std::isfinite(start)||!std::isfinite(end)||start<0||start>=double(frames)*1000/60||end<=start||end>double(frames)*1000/60+120)return fail("A transcription timestamp is outside the recording.");
        const auto text=simplified(value["text"].toString().trimmed());if(text.isEmpty())continue;
        if(text.size()>2000)return fail("A caption exceeds 2000 characters.");
        TextOverlay cue;cue.text=text;cue.fontSize=57;cue.y=.85;cue.startFrame=std::min(frames-1,qRound(start*60/1000));
        const int last=std::min(frames,std::max(cue.startFrame+1,qRound(end*60/1000)));cue.durationFrames=last-cue.startFrame;
        if(cue.startFrame<previous)return fail("Transcription segments are out of order.");
        if(cue.startFrame<previousEnd&&warnings)warnings->append(QString("Caption %1 overlaps the previous caption.").arg(project.captions.size()+1));
        if(cue.durationFrames<21)brief.append(QString::number(project.captions.size()+1));
        previous=cue.startFrame;previousEnd=last;project.captions.append(cue);
    }
    if(project.captions.isEmpty())return fail("No text was recognized. Existing output was preserved.");
    if(!brief.isEmpty()&&warnings)warnings->append("Very short captions need timing review: "+brief.join(", "));
    const auto bytes=Subtitles::writeSrt(project);QString problem;const auto restored=Subtitles::parseSrt(bytes,&problem);
    if(!restored||restored->size()!=project.captions.size())return fail("Generated subtitles did not pass validation: "+problem);
    for(int i=0;i<restored->size();++i)if((*restored)[i].text!=project.captions[i].text||(*restored)[i].startFrame!=project.captions[i].startFrame||(*restored)[i].durationFrames!=project.captions[i].durationFrames)return fail("Generated subtitles did not preserve their text and frame timing.");
    return bytes;
}
QString subtitleRuntimeDirectory(){
#ifdef Q_OS_MACOS
    return QDir(QCoreApplication::applicationDirPath()).filePath("../Resources/subtitles");
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath("subtitles");
#endif
}
QString subtitleEngineExecutable(bool cpuOnly){
    const auto override=qEnvironmentVariable("ISAAC_WHISPER_EXECUTABLE");if(!override.isEmpty())return QFileInfo(override).absoluteFilePath();
#ifdef Q_OS_WIN
    const QString name="whisper-cli.exe";
#else
    const QString name="whisper-cli";
#endif
    const QDir runtime(subtitleRuntimeDirectory());
    if(cpuOnly&&QFileInfo(runtime.filePath("cpu/"+name)).isExecutable())return runtime.filePath("cpu/"+name);
    return runtime.filePath(name);
}
int runSubtitleAudioWorker(const QStringList &args){
    if(args.size()!=5||args[1]!="--subtitle-audio-worker")return 2;
    QString error;auto project=loadProject(args[2],&error);int last=-1;
    const bool ok=project&&GpuPipeline::renderAudio(*project,{320,180},args[3],[&]{return QFileInfo::exists(args[4]);},[&](int progress){if(progress!=last){last=progress;event({{"event","progress"},{"value",progress}});}},&error);
    event({{"event","result"},{"ok",ok},{"error",error}});return ok?0:1;
}
SubtitleJob::SubtitleJob(const QString &appWorker,const QString &whisper,QObject *parent):QObject(parent),appWorker_(appWorker),whisper_(whisper){
    process_.setParent(this);cancelDeadline_.setSingleShot(true);cancelDeadline_.setInterval(2000);
    connect(&cancelDeadline_,&QTimer::timeout,this,[this]{if(process_.state()!=QProcess::NotRunning)process_.kill();});
    connect(&process_,&QProcess::readyReadStandardOutput,this,&SubtitleJob::readOutput);
    connect(&process_,&QProcess::readyReadStandardError,this,&SubtitleJob::readErrors);
    connect(&process_,&QProcess::finished,this,&SubtitleJob::processFinished);
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart)complete(false,"Could not start the subtitle worker: "+process_.errorString());});
}
SubtitleJob::~SubtitleJob(){disconnect(&process_,nullptr,this,nullptr);stopAndWait();}
bool SubtitleJob::start(const Project &snapshot,const QString &destination,const QString &modelPath,bool cpuOnly,QString *error){
    const auto fail=[&](const QString &message){if(error)*error=message;return false;};
    if(running_||destination.isEmpty()||snapshot.clips.isEmpty())return fail("Add video clips and choose an SRT destination.");
    const auto problem=snapshot.validate();if(!problem.isEmpty())return fail(problem);
    QStringList sources{snapshot.voicePath,snapshot.musicPath,modelPath};for(const auto &c:snapshot.clips)sources.append(c.path);for(const auto &o:snapshot.visualOverlays)sources.append(o.path);
    const auto target=normalized(destination);
    for(const auto &asset:snapshot.assets)if(target.compare(normalized(asset.path),Qt::CaseInsensitive)==0)return fail("Choose a different file from your source media.");
    sources_.clear();for(const auto &path:sources)if(!path.isEmpty()){
        const auto name=normalized(path);if(target.compare(name,Qt::CaseInsensitive)==0)return fail("Choose a different file from your source media or model.");
        const auto id=identity(name);if(id.first<0)return fail("A required source or subtitle model is missing: "+path);sources_.insert(name,id);
    }
    if(!QFileInfo(whisper_).isExecutable()||!QFileInfo(appWorker_).isExecutable())return fail("The local subtitle engine is missing. Use an application build with the Whisper engine included.");
    if(QFileInfo(destination).isSymLink())return fail("Choose an SRT destination that is not a symbolic link.");
    destination_=QFileInfo(destination).absoluteFilePath();model_=QFileInfo(modelPath).absoluteFilePath();frames_=snapshot.durationFrames();cpuOnly_=cpuOnly;
    staging_=std::make_unique<QTemporaryDir>(QFileInfo(destination_).absoluteDir().filePath(".isaac-subtitles-XXXXXX"));
    if(!staging_->isValid()||!saveProject(snapshot,staging_->filePath("snapshot.isaac.json"),error)){staging_.reset();return fail("Could not prepare subtitles in the output folder.");}
    const auto logs=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/subtitle-logs";QDir().mkpath(logs);log_.setFileName(logs+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".log");(void)log_.open(QIODevice::WriteOnly|QIODevice::NewOnly);
    running_=true;canceled_=false;audio_=true;resultReceived_=resultOk_=protocolFailed_=gpuConfirmed_=false;output_.clear();errors_.clear();workerError_.clear();warnings_.clear();device_=cpuOnly?"CPU":"Checking GPU";stage_="Preparing audio";progress_=-1;
    auto env=QProcessEnvironment::systemEnvironment();env.insert("QT_QPA_PLATFORM","offscreen");process_.setProcessEnvironment(env);
    process_.start(appWorker_,{"--subtitle-audio-worker",staging_->filePath("snapshot.isaac.json"),staging_->filePath("audio.wav"),staging_->filePath("cancel")});emit changed();return true;
}
bool SubtitleJob::capturedSourcesUnchanged() const {for(auto i=sources_.cbegin();i!=sources_.cend();++i)if(identity(i.key())!=i.value())return false;return true;}
void SubtitleJob::writeLog(const QByteArray &bytes){if(log_.isOpen()&&log_.size()<512*1024){log_.write(bytes.left(512*1024-log_.size()));log_.flush();}}
void SubtitleJob::readOutput(){
    const auto bytes=process_.readAllStandardOutput();writeLog(bytes);if(!audio_)return;output_+=bytes;
    while(output_.contains('\n')){
        const int end=output_.indexOf('\n');const auto line=output_.left(end);output_.remove(0,end+1);const auto doc=QJsonDocument::fromJson(line);const auto object=doc.object();const auto type=object["event"].toString();
        if(line.size()>65536||!doc.isObject()||resultReceived_){protocolFailed_=true;continue;}
        if(type=="progress"&&object["value"].isDouble()&&object["value"].toDouble()>=0&&object["value"].toDouble()<=100){progress_=std::min(99,object["value"].toInt());emit changed();}
        else if(type=="result"&&object["ok"].isBool()){resultReceived_=true;resultOk_=object["ok"].toBool();workerError_=object["error"].toString();}
        else protocolFailed_=true;
    }
    if(output_.size()>65536){protocolFailed_=true;output_.clear();process_.kill();}
}
void SubtitleJob::readErrors(){
    const auto bytes=process_.readAllStandardError();writeLog(bytes);if(audio_)return;errors_+=bytes;
    while(errors_.contains('\n')){
        const int end=errors_.indexOf('\n');const auto line=QString::fromUtf8(errors_.left(end));errors_.remove(0,end+1);
        const auto progress=QRegularExpression("progress =\\s*(\\d+)%").match(line);if(progress.hasMatch()){progress_=std::clamp(progress.captured(1).toInt(),0,99);emit changed();}
        const auto gpu=QRegularExpression("whisper_backend_init_gpu: using (.+) backend").match(line);
        if(gpu.hasMatch()&&!cpuOnly_){gpuConfirmed_=true;const auto name=gpu.captured(1);device_=name.startsWith("CUDA")?"NVIDIA GPU":name.startsWith("MTL")||name=="Metal"?"Apple GPU":"GPU";emit changed();}
        if(!cpuOnly_&&(line.contains("whisper_backend_init_gpu: no GPU found")||line.contains("whisper_backend_init_gpu: failed to initialize"))){workerError_="The GPU subtitle engine is unavailable. Select Use CPU and generate again, or check the NVIDIA driver/runtime.";gpuConfirmed_=false;process_.terminate();cancelDeadline_.start();}
    }
    if(errors_.size()>65536)errors_.clear();
}
void SubtitleJob::transcribe(){
    audio_=false;output_.clear();errors_.clear();stage_="Transcribing";progress_=-1;emit changed();
    if(canceled_){complete(false,"Subtitle generation canceled.");return;}
    QStringList args{"-m",model_,"-f",staging_->filePath("audio.wav"),"-l","auto","-oj","-of",staging_->filePath("transcript"),"-pp","-t",QString::number(std::clamp(QThread::idealThreadCount(),1,4))};
    if(cpuOnly_)args.append("-ng");process_.setWorkingDirectory(QFileInfo(whisper_).absolutePath());process_.start(whisper_,args);
}
void SubtitleJob::processFinished(int code,QProcess::ExitStatus status){
    readOutput();readErrors();cancelDeadline_.stop();if(!running_)return;
    if(canceled_){complete(false,"Subtitle generation canceled. Existing output was preserved.");return;}
    if(status!=QProcess::NormalExit||code!=0||!workerError_.isEmpty()||protocolFailed_){complete(false,workerError_.isEmpty()?"The subtitle worker failed. Existing output was preserved. See the subtitle log.":workerError_);return;}
    if(audio_){
        if(!output_.trimmed().isEmpty()||!resultReceived_||!resultOk_||QFileInfo(staging_->filePath("audio.wav")).size()<=44){complete(false,"Audio preparation did not produce a valid result.");return;}
        if(!capturedSourcesUnchanged()){complete(false,"Source media changed during subtitle generation. Generate again.");return;}
        transcribe();return;
    }
    if(!cpuOnly_&&!gpuConfirmed_){complete(false,"GPU processing could not be confirmed. Select Use CPU and generate again.");return;}
    stage_="Saving subtitles";progress_=-1;emit changed();if(canceled_){complete(false,"Subtitle generation canceled.");return;}
    QFile json(staging_->filePath("transcript.json"));QString error;
    if(!json.open(QIODevice::ReadOnly)||json.size()>128*1024*1024){complete(false,"The worker did not produce a readable transcription.");return;}
    const auto result=Subtitles::whisperSrt(json.readAll(),frames_,&warnings_,&error);
    if(!result){complete(false,error);return;}
    if(!capturedSourcesUnchanged()){complete(false,"Source media changed during subtitle generation. Existing output was preserved.");return;}
    QSaveFile file(destination_);file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)||file.write(*result)!=result->size()||!file.commit()){complete(false,"Could not save the SRT: "+file.errorString());return;}
    complete(true);
}
void SubtitleJob::complete(bool success,const QString &error){
    if(!running_)return;running_=false;cancelDeadline_.stop();stage_=success?"Completed":canceled_?"Canceled":"Failed";progress_=success?100:-1;writeLog((stage_+": "+error+'\n').toUtf8());log_.close();staging_.reset();emit changed();emit finished(success,!success&&canceled_,error);
}
void SubtitleJob::cancel(){
    if(!running_||canceled_)return;canceled_=true;stage_="Canceling";emit changed();
    if(staging_){QFile marker(staging_->filePath("cancel"));if(marker.open(QIODevice::WriteOnly))marker.close();}
    if(process_.state()!=QProcess::NotRunning){process_.terminate();cancelDeadline_.start();}
}
void SubtitleJob::stopAndWait(){cancel();if(process_.state()!=QProcess::NotRunning&&!process_.waitForFinished(1500)){process_.kill();process_.waitForFinished(1000);}if(running_)complete(false,"Subtitle generation canceled.");}
