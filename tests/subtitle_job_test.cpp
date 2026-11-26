#include "subtitle_job.h"
#include "captions.h"
#include "gpu_pipeline.h"
#include "mlt_engine.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <QtEndian>
#include <cstdio>

static QByteArray fixtureJson(){return R"({"transcription":[{"offsets":{"from":0,"to":1000},"text":"學習 English <b> & 😀"}],"params":{"translate":false}})";}
static QByteArray timedJson(int start=0){
    const auto token=[&](const QString &text,int from,int to,int id){return QJsonObject{{"text",text},{"id",id},{"offsets",QJsonObject{{"from",start+from},{"to",start+to}}}};};
    return QJsonDocument(QJsonObject{{"transcription",QJsonArray{QJsonObject{{"text","Hello 中文!"},{"offsets",QJsonObject{{"from",start},{"to",start+2000}}},{"tokens",QJsonArray{token("[_BEG_]",0,0,50365),token("Hello ",750,1000,1),token("中文",1200,1500,2),token("!",1500,1500,3),token("[_TT_100]",2000,2000,50465)}}}}}}).toJson();
}
static QByteArray repeatedJson(int count,int start=1000,const QString &text="我们要去看鸭子",int gap=0){
    QJsonArray segments;
    for(int i=0;i<count;++i){const int from=start+i*(2000+gap);segments.append(QJsonObject{{"offsets",QJsonObject{{"from",from},{"to",from+2000}}},{"text",text}});}
    return QJsonDocument(QJsonObject{{"transcription",segments}}).toJson();
}
class SubtitleJobTest final : public QObject {
    Q_OBJECT
    Project sample() const {Project p;p.clips={{QDir::current().filePath("tests/fixtures/seek.mp4"),0,2}};return p;}
    static QByteArray read(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
    static void put(const QString &path,const QByteArray &bytes){QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(bytes),bytes.size());}
private slots:
    void parsesRealOffsetsAndPreservesText(){
        QString error;QStringList warnings;const auto bytes=Subtitles::whisperSrt(fixtureJson(),120,&warnings,&error);QVERIFY2(bytes,qPrintable(error));const auto cues=Subtitles::parseSrt(*bytes);QVERIFY(cues);QCOMPARE(cues->size(),1);QCOMPARE(cues->first().durationFrames,60);
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        QCOMPARE(cues->first().text,QString("学习 English <b> & 😀"));
#else
        QCOMPARE(cues->first().text,QString("學習 English <b> & 😀"));
#endif
        QVERIFY(!Subtitles::whisperSrt("{\"transcription\":[{\"offsets\":{\"from\":0,\"to\":9000},\"text\":\"past end\"}]}",120,&warnings,&error));
        QVERIFY(!Subtitles::whisperSrt("{\"transcription\":[]}",120,&warnings,&error));
        QVERIFY(!Subtitles::whisperSrt("{\"transcription\":[{\"offsets\":{\"from\":1,\"to\":0},\"text\":\"bad\"}]}",120,&warnings,&error));
    }
    void segmentStartingAfterAudioCannotBeMovedBackIntoTheRecording(){
        QString error;const auto result=Subtitles::whisperSrt(R"({"transcription":[{"offsets":{"from":2050,"to":2100},"text":"outside"}]})",120,nullptr,&error);QVERIFY(!result);QVERIFY(!error.isEmpty());
    }
    void timestampsBeyondFiveMinutesRemainOnTheOriginalClock(){
        QString error;auto result=Subtitles::whisperSrt(R"({"transcription":[{"offsets":{"from":350000,"to":351000},"text":"Later speech"}]})",360*60,nullptr,&error);QVERIFY2(result,qPrintable(error));auto cues=Subtitles::parseSrt(*result);QVERIFY(cues);QCOMPARE(cues->first().startFrame,350*60);QCOMPARE(cues->first().durationFrames,60);
    }
    void wordBoundsPreserveTextAndTheOriginalClock(){
        QString error;QStringList warnings;const auto result=Subtitles::whisperSrt(timedJson(350000),360*60,&warnings,&error);QVERIFY2(result,qPrintable(error));const auto cues=Subtitles::parseSrt(*result);QVERIFY(cues);QCOMPARE(cues->size(),1);QCOMPARE(cues->first().text,QString("Hello 中文!"));QCOMPARE(cues->first().startFrame,21045);QCOMPARE(cues->first().durationFrames,45);QVERIFY(warnings.isEmpty());
    }
    void supplementaryHanAtTheStartKeepsItsSpokenTime(){
        const QByteArray json=R"({"transcription":[{"text":"𠀀 hello","offsets":{"from":0,"to":2000},"tokens":[{"id":1,"text":"𠀀","offsets":{"from":100,"to":500}},{"id":2,"text":" hello","offsets":{"from":1000,"to":1500}}]}]})";
        QString error;const auto result=Subtitles::whisperSrt(json,120,nullptr,&error);QVERIFY2(result,qPrintable(error));const auto cues=Subtitles::parseSrt(*result);QVERIFY(cues);QCOMPARE(cues->first().startFrame,6);QCOMPARE(cues->first().durationFrames,84);QCOMPARE(cues->first().text,QString::fromUtf8("𠀀 hello"));
    }
    void incompleteWordTimingFallsBackWithoutLosingText_data(){
        QTest::addColumn<QByteArray>("json");
        for(const auto &mode:{QString("missing"),QString("partial-text"),QString("zero-width"),QString("outside"),QString("out-of-order"),QString("invalid-number")}){
            auto root=QJsonDocument::fromJson(timedJson()).object();auto segment=root["transcription"].toArray()[0].toObject();auto tokens=segment["tokens"].toArray();
            if(mode=="missing")segment.remove("tokens");
            else if(mode=="partial-text"){tokens.removeAt(1);segment["tokens"]=tokens;}
            else {auto token=tokens[2].toObject();auto offsets=token["offsets"].toObject();if(mode=="zero-width")offsets["to"]=1200;else if(mode=="outside")offsets["to"]=2100;else if(mode=="out-of-order")offsets["from"]=900;else offsets["from"]="invalid";token["offsets"]=offsets;tokens[2]=token;segment["tokens"]=tokens;}
            root["transcription"]=QJsonArray{segment};QTest::newRow(qPrintable(mode))<<QJsonDocument(root).toJson();
        }
    }
    void incompleteWordTimingFallsBackWithoutLosingText(){
        QFETCH(QByteArray,json);QString error;QStringList warnings;const auto result=Subtitles::whisperSrt(json,120,&warnings,&error);QVERIFY2(result,qPrintable(error));const auto cues=Subtitles::parseSrt(*result);QVERIFY(cues);QCOMPARE(cues->first().text,QString("Hello 中文!"));QCOMPARE(cues->first().startFrame,0);QCOMPARE(cues->first().durationFrames,120);QVERIFY(!warnings.isEmpty());
    }
    void zeroDurationDecoderFragmentsDoNotRejectValidSpeech(){
        QString error;QStringList warnings;const auto result=Subtitles::whisperSrt(R"({"transcription":[{"offsets":{"from":198000,"to":199000},"text":"Before"},{"offsets":{"from":199000,"to":199000},"text":"Transient fragment"},{"offsets":{"from":200000,"to":201000},"text":"After"}]})",463*60,&warnings,&error);
        QVERIFY2(result,qPrintable(error));const auto cues=Subtitles::parseSrt(*result);QVERIFY(cues);QCOMPARE(cues->size(),2);QCOMPARE(cues->last().startFrame,200*60);QVERIFY(warnings.join(' ').contains("zero-duration"));
        QVERIFY(!Subtitles::whisperSrt(R"({"transcription":[{"offsets":{"from":1000,"to":1000},"text":"Only fragment"}]})",120,nullptr,&error));
    }
    void rejectsTheReportedLoopButRetainsOrdinaryRepetition(){
        QString error;QVERIFY(!Subtitles::whisperSrt(repeatedJson(74,311740),463*60,nullptr,&error));QVERIFY(error.contains("repeat",Qt::CaseInsensitive));
        auto root=QJsonDocument::fromJson(repeatedJson(8)).object();auto segments=root["transcription"].toArray();for(int i=0;i<segments.size();++i){auto segment=segments[i].toObject();const int from=1000+i*2000;segment["tokens"]=QJsonArray{QJsonObject{{"id",1},{"text","我们要去看鸭子"},{"offsets",QJsonObject{{"from",from+500},{"to",from+600}}}}};segments[i]=segment;}root["transcription"]=segments;QVERIFY(!Subtitles::whisperSrt(QJsonDocument(root).toJson(),20*60,nullptr,&error));
        for(const auto &json:{repeatedJson(3),repeatedJson(74,0,"拜拜"),repeatedJson(12,0,"我们要去看鸭子",2000)}){
            const auto result=Subtitles::whisperSrt(json,463*60,nullptr,&error);QVERIFY2(result,qPrintable(error));const auto cues=Subtitles::parseSrt(*result);QVERIFY(cues);QCOMPARE(cues->size(),QJsonDocument::fromJson(json).object()["transcription"].toArray().size());
        }
    }
    void repetitionRecoveryIsBoundedAndPreservesOutput_data(){
        QTest::addColumn<QByteArray>("mode");QTest::addColumn<bool>("ok");
        QTest::newRow("recovered")<<QByteArray("repeat-recovered")<<true;
        QTest::newRow("still-looping")<<QByteArray("repeat-stuck")<<false;
        QTest::newRow("retry-crash")<<QByteArray("repeat-crash")<<false;
        QTest::newRow("retry-no-output")<<QByteArray("repeat-no-output")<<false;
    }
    void repetitionRecoveryIsBoundedAndPreservesOutput(){
        QFETCH(QByteArray,mode);QFETCH(bool,ok);qputenv("ISAAC_SUBTITLE_FIXTURE",mode);const auto restore=qScopeGuard([]{qunsetenv("ISAAC_SUBTITLE_FIXTURE");});
        auto p=sample();p.crossfadeFrames=0;while(p.clips.size()<10){auto clip=p.clips.first();clip.id=QString::number(p.clips.size());p.clips.append(clip);}QTemporaryDir dir;put(dir.filePath("model"),"model");const auto out=dir.filePath("out.srt");put(out,"KEEP");
        SubtitleJob job(QCoreApplication::applicationFilePath(),QCoreApplication::applicationFilePath());QSignalSpy done(&job,&SubtitleJob::finished);QString error;QVERIFY2(job.start(p,out,dir.filePath("model"),true,&error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,15000);QCOMPARE(done[0][0].toBool(),ok);QCOMPARE(read(dir.filePath("calls.txt")),QByteArray("xx"));
        if(ok){const auto cues=Subtitles::parseSrt(read(out));QVERIFY(cues);QCOMPARE(cues->size(),2);QCOMPARE(cues->last().startFrame,18*60);QVERIFY(job.warnings().join(' ').contains("retri",Qt::CaseInsensitive));}
        else QCOMPARE(read(out),QByteArray("KEEP"));
    }
    void cancelDuringRepetitionRetryPreservesDestination(){
        qputenv("ISAAC_SUBTITLE_FIXTURE","repeat-wait");const auto restore=qScopeGuard([]{qunsetenv("ISAAC_SUBTITLE_FIXTURE");});auto p=sample();p.crossfadeFrames=0;while(p.clips.size()<10){auto clip=p.clips.first();clip.id=QString::number(p.clips.size());p.clips.append(clip);}QTemporaryDir dir;put(dir.filePath("model"),"model");const auto out=dir.filePath("out.srt");put(out,"KEEP");
        SubtitleJob job(QCoreApplication::applicationFilePath(),QCoreApplication::applicationFilePath());QSignalSpy done(&job,&SubtitleJob::finished);connect(&job,&SubtitleJob::changed,&job,[&]{if(job.stage()=="Retrying transcription")QTimer::singleShot(100,&job,&SubtitleJob::cancel);});QString error;QVERIFY(job.start(p,out,dir.filePath("model"),true,&error));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,15000);QVERIFY(!done[0][0].toBool());QVERIFY(done[0][1].toBool());QCOMPARE(read(out),QByteArray("KEEP"));
    }
    void changedModelIsRejectedBeforeTheRetryWorkerStarts(){
        qputenv("ISAAC_SUBTITLE_FIXTURE","repeat-recovered");const auto restore=qScopeGuard([]{qunsetenv("ISAAC_SUBTITLE_FIXTURE");});auto p=sample();p.crossfadeFrames=0;while(p.clips.size()<10){auto clip=p.clips.first();clip.id=QString::number(p.clips.size());p.clips.append(clip);}QTemporaryDir dir;const auto model=dir.filePath("model"),out=dir.filePath("out.srt");put(model,"model");put(out,"KEEP");
        SubtitleJob job(QCoreApplication::applicationFilePath(),QCoreApplication::applicationFilePath());QSignalSpy done(&job,&SubtitleJob::finished);connect(&job,&SubtitleJob::changed,&job,[&]{if(job.stage()=="Retrying transcription")put(model,"changed model");});QString error;QVERIFY(job.start(p,out,model,true,&error));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,15000);QVERIFY(!done[0][0].toBool());QCOMPARE(read(out),QByteArray("KEEP"));QCOMPARE(read(dir.filePath("calls.txt")),QByteArray("x"));
    }
    void publishesOnlyValidatedOutput_data(){QTest::addColumn<QByteArray>("mode");QTest::addColumn<bool>("ok");QTest::newRow("success")<<QByteArray("success")<<true;QTest::newRow("word-timing")<<QByteArray("word-timing")<<true;QTest::newRow("bad-json")<<QByteArray("bad-json")<<false;QTest::newRow("no-text")<<QByteArray("no-text")<<false;QTest::newRow("crash")<<QByteArray("crash")<<false;}
    void publishesOnlyValidatedOutput(){
        QFETCH(QByteArray,mode);QFETCH(bool,ok);qputenv("ISAAC_SUBTITLE_FIXTURE",mode);const auto restore=qScopeGuard([]{qunsetenv("ISAAC_SUBTITLE_FIXTURE");});
        QTemporaryDir dir;const auto model=dir.filePath("模型.bin"),out=dir.filePath("字幕.srt");put(model,"model");put(out,"KEEP");
        SubtitleJob job(QCoreApplication::applicationFilePath(),QCoreApplication::applicationFilePath());QSignalSpy done(&job,&SubtitleJob::finished);QString error;
        QVERIFY2(job.start(sample(),out,model,true,&error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,15000);QCOMPARE(done[0][0].toBool(),ok);
        if(ok){const auto cues=Subtitles::parseSrt(read(out));QVERIFY(cues);if(mode=="word-timing"){QCOMPARE(cues->first().startFrame,45);QCOMPARE(cues->first().durationFrames,45);}}else QCOMPARE(read(out),QByteArray("KEEP"));
    }
    void cancelDuringTranscriptionPreservesDestination(){
        qputenv("ISAAC_SUBTITLE_FIXTURE","wait");const auto restore=qScopeGuard([]{qunsetenv("ISAAC_SUBTITLE_FIXTURE");});
        QTemporaryDir dir;put(dir.filePath("model"),"model");put(dir.filePath("out.srt"),"KEEP");SubtitleJob job(QCoreApplication::applicationFilePath(),QCoreApplication::applicationFilePath());QSignalSpy done(&job,&SubtitleJob::finished);QString error;
        connect(&job,&SubtitleJob::changed,&job,[&]{if(job.stage()=="Transcribing")QTimer::singleShot(100,&job,&SubtitleJob::cancel);});
        QVERIFY(job.start(sample(),dir.filePath("out.srt"),dir.filePath("model"),true,&error));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY(!done[0][0].toBool());QVERIFY(done[0][1].toBool());QCOMPARE(read(dir.filePath("out.srt")),QByteArray("KEEP"));
    }
    void sourceCollisionAndMissingEngineFailSafely(){
        QTemporaryDir dir;put(dir.filePath("model"),"model");QString error;SubtitleJob job(QCoreApplication::applicationFilePath(),"/missing/whisper");
        QVERIFY(!job.start(sample(),sample().clips[0].path,dir.filePath("model"),true,&error));QVERIFY(!error.isEmpty());QVERIFY(!job.start(sample(),dir.filePath("out.srt"),dir.filePath("model"),true,&error));QVERIFY(!QFileInfo::exists(dir.filePath("out.srt")));
    }
    void waveformUsesTimelineMuteAndPcmClock(){
        QTemporaryDir dir;QProcess ff;ff.start("ffmpeg",{"-v","error","-f","lavfi","-i","color=s=160x96:r=60:d=2","-f","lavfi","-i","sine=frequency=700:duration=2","-c:v","libx264","-preset","ultrafast","-c:a","aac","-shortest",dir.filePath("tone.mp4")});QVERIFY(ff.waitForFinished(15000));QCOMPARE(ff.exitCode(),0);
        Project p;p.clips={{dir.filePath("tone.mp4"),0,1},{dir.filePath("tone.mp4"),1,2}};p.clips[1].audio.muted=true;QString error;const auto output=dir.filePath("audio.wav");QVERIFY2(GpuPipeline::renderAudio(p,{160,96},output,{}, {},&error),qPrintable(error));
        const auto samples=[&](const QString &path){ff.start("ffmpeg",{"-v","error","-i",path,"-ar","16000","-ac","1","-f","s16le","-acodec","pcm_s16le","pipe:1"});if(!ff.waitForFinished(10000))return QByteArray();return ff.readAllStandardOutput();};
        auto pcm=samples(output);QVERIFY(qAbs(pcm.size()-64000)<534);
        const auto energy=[&](int first,int last){double sum=0;for(int i=first;i<last;i+=2){qint16 v=qFromLittleEndian<qint16>(pcm.constData()+i);sum+=qAbs(int(v));}return sum/(last-first);};
        QVERIFY(energy(8000,24000)>100);QVERIFY(energy(40000,56000)<2);
        p.voicePath=dir.filePath("tone.mp4");p.voiceDurationFrames=120;p.voiceStartSeconds=1;p.voiceAudio.fadeInFrames=30;
        const auto voice=dir.filePath("voice.wav");QVERIFY2(GpuPipeline::renderAudio(p,{160,96},voice,{}, {},&error),qPrintable(error));pcm=samples(voice);QVERIFY(qAbs(pcm.size()-64000)<534);QVERIFY(energy(54000,60000)>100);QVERIFY(energy(33600,38000)<energy(54000,60000));
        p.voiceAudio.muted=true;p.clips[0].audio.muted=true;p.musicPath=dir.filePath("tone.mp4");p.musicDurationFrames=120;p.musicDb=-6;
        const auto music=dir.filePath("music.wav");QVERIFY2(GpuPipeline::renderAudio(p,{160,96},music,{}, {},&error),qPrintable(error));pcm=samples(music);QVERIFY(qAbs(pcm.size()-64000)<534);QVERIFY(energy(8000,24000)>100);QVERIFY(energy(40000,56000)>100);
        p.voicePath.clear();p.voiceDurationFrames=0;p.musicPath.clear();p.musicDurationFrames=0;p.clips[0].audio.muted=false;p.crossfadeFrames=30;
        const auto transition=dir.filePath("transition.wav");QVERIFY2(GpuPipeline::renderAudio(p,{160,96},transition,{}, {},&error),qPrintable(error));pcm=samples(transition);QCOMPARE(p.durationFrames(),90);QVERIFY(qAbs(pcm.size()-48000)<534);QVERIFY(energy(40000,44000)<2);
        const double waveformShape=energy(20000,26000)/std::max(1.0,energy(28800,31000));
        const auto reference=dir.filePath("transition.m4a");QVERIFY2(GpuPipeline::renderAudio(p,{160,96},reference,{}, {},&error),qPrintable(error));pcm=samples(reference);QVERIFY(pcm.size()>44000);
        const double exportShape=energy(20000,26000)/std::max(1.0,energy(28800,31000));QVERIFY(qAbs(waveformShape-exportShape)<.2);
    }
    void longAudioKeepsChangingAfterAQuietNoisyInterval(){
        QTemporaryDir dir;const auto source=dir.filePath("long.mkv");QProcess ff;
        const QString audio="aevalsrc=if(lt(t\\,310)\\,0.2*sin(2*PI*440*t)\\,if(lt(t\\,315)\\,0.002*(random(0)-0.5)\\,0.2*sin(2*PI*880*t))):s=16000:d=463.1";
        ff.start("ffmpeg",{"-v","error","-f","lavfi","-i","color=s=160x96:r=60:d=463.1","-f","lavfi","-i",audio,"-c:v","libx264","-preset","ultrafast","-c:a","pcm_s16le","-shortest",source});QVERIFY(ff.waitForFinished(60000));QVERIFY2(ff.exitCode()==0,ff.readAllStandardError().constData());
        Project p;p.clips={{source,0,463.1}};QString error;const auto wave=dir.filePath("prepared.wav");QVERIFY2(GpuPipeline::renderAudio(p,{160,96},wave,{}, {},&error),qPrintable(error));ff.start("ffmpeg",{"-v","error","-i",wave,"-ar","16000","-ac","1","-f","s16le","-acodec","pcm_s16le","pipe:1"});QVERIFY(ff.waitForFinished(10000));QCOMPARE(ff.exitCode(),0);const auto pcm=ff.readAllStandardOutput();QVERIFY(qAbs(pcm.size()-14819200)<534);
        const auto measurement=[&](int second){double energy=0;int crossings=0,previous=0;for(int i=second*16000;i<(second+1)*16000;++i){const auto sample=qFromLittleEndian<qint16>(pcm.constData()+i*2);energy+=qAbs(int(sample));if(i>second*16000&&sample>=0&&previous<0)++crossings;previous=sample;}return qMakePair(energy/16000,crossings);};
        const auto before=measurement(309),quiet=measurement(312),after=measurement(317),tail=measurement(462);QVERIFY(before.first>1000);QVERIFY(quiet.first<before.first/20);QVERIFY(after.first>1000);QVERIFY(tail.first>1000);QVERIFY(qAbs(before.second-440)<=2);QVERIFY(qAbs(after.second-880)<=2);QVERIFY(qAbs(tail.second-880)<=2);
    }
};
int main(int argc,char **argv){
    QApplication app(argc,argv);Media::initializeMlt();
    if(app.arguments().value(1)=="--subtitle-audio-worker")return runSubtitleAudioWorker(app.arguments());
    if(app.arguments().contains("-m")){
        const auto mode=qgetenv("ISAAC_SUBTITLE_FIXTURE");if(mode=="wait"){QTimer::singleShot(30000,&app,&QCoreApplication::quit);return app.exec();}if(mode=="crash")return 4;
        if(mode=="word-timing"&&!app.arguments().contains("-ojf"))return 7;
        if(mode.startsWith("repeat-")){
            const auto prefix=app.arguments().value(app.arguments().indexOf("-of")+1);QFile calls(QFileInfo(prefix).absoluteDir().filePath("../calls.txt"));if(!calls.open(QIODevice::WriteOnly|QIODevice::Append))return 6;calls.write("x");calls.close();
            const int context=app.arguments().indexOf("-mc");const bool retry=context>=0&&app.arguments().value(context+1)=="0";
            if(retry&&mode=="repeat-crash")return 4;
            if(retry&&mode=="repeat-no-output")return 0;
            if(retry&&mode=="repeat-wait"){QTimer::singleShot(30000,&app,&QCoreApplication::quit);return app.exec();}
            QFile f(app.arguments().value(app.arguments().indexOf("-of")+1)+".json");if(!f.open(QIODevice::WriteOnly))return 3;
            f.write(retry&&mode=="repeat-recovered"?QByteArray(R"({"transcription":[{"offsets":{"from":1000,"to":2000},"text":"Recovered dialogue"},{"offsets":{"from":18000,"to":19000},"text":"Later dialogue"}]})"):repeatedJson(8));return 0;
        }
        QFile f(app.arguments().value(app.arguments().indexOf("-of")+1)+".json");if(!f.open(QIODevice::WriteOnly))return 3;
        f.write(mode=="bad-json"?QByteArray("bad"):mode=="no-text"?QByteArray("{\"transcription\":[]}"):mode=="word-timing"?timedJson():fixtureJson());return 0;
    }
    SubtitleJobTest test;return QTest::qExec(&test,argc,argv);
}
#include "subtitle_job_test.moc"
