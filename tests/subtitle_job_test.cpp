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
    void publishesOnlyValidatedOutput_data(){QTest::addColumn<QByteArray>("mode");QTest::addColumn<bool>("ok");QTest::newRow("success")<<QByteArray("success")<<true;QTest::newRow("bad-json")<<QByteArray("bad-json")<<false;QTest::newRow("no-text")<<QByteArray("no-text")<<false;QTest::newRow("crash")<<QByteArray("crash")<<false;}
    void publishesOnlyValidatedOutput(){
        QFETCH(QByteArray,mode);QFETCH(bool,ok);qputenv("ISAAC_SUBTITLE_FIXTURE",mode);const auto restore=qScopeGuard([]{qunsetenv("ISAAC_SUBTITLE_FIXTURE");});
        QTemporaryDir dir;const auto model=dir.filePath("模型.bin"),out=dir.filePath("字幕.srt");put(model,"model");put(out,"KEEP");
        SubtitleJob job(QCoreApplication::applicationFilePath(),QCoreApplication::applicationFilePath());QSignalSpy done(&job,&SubtitleJob::finished);QString error;
        QVERIFY2(job.start(sample(),out,model,true,&error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,15000);QCOMPARE(done[0][0].toBool(),ok);
        if(ok)QVERIFY(Subtitles::parseSrt(read(out)));else QCOMPARE(read(out),QByteArray("KEEP"));
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
};
int main(int argc,char **argv){
    QApplication app(argc,argv);Media::initializeMlt();
    if(app.arguments().value(1)=="--subtitle-audio-worker")return runSubtitleAudioWorker(app.arguments());
    if(app.arguments().contains("-m")){
        const auto mode=qgetenv("ISAAC_SUBTITLE_FIXTURE");if(mode=="wait"){QTimer::singleShot(30000,&app,&QCoreApplication::quit);return app.exec();}if(mode=="crash")return 4;
        QFile f(app.arguments().value(app.arguments().indexOf("-of")+1)+".json");if(!f.open(QIODevice::WriteOnly))return 3;
        f.write(mode=="bad-json"?QByteArray("bad"):mode=="no-text"?QByteArray("{\"transcription\":[]}"):fixtureJson());return 0;
    }
    SubtitleJobTest test;return QTest::qExec(&test,argc,argv);
}
#include "subtitle_job_test.moc"
