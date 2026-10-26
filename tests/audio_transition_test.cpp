#include "editor_controller.h"
#include "mlt_engine.h"
#include "quick_timeline.h"
#include "preview.h"
#include <QQuickWindow>
#include <QSignalSpy>
#include <QProcess>
#include <QStandardPaths>
#include <cmath>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class AudioTransitionTest : public QObject {
    Q_OBJECT
    QString fixture() const { return QDir::current().filePath("tests/fixtures/seek.mp4"); }
private slots:
    void offlineAudioKeepsDurationWithoutLibraryEntry() {
        QTemporaryDir dir;const auto audio=dir.filePath("narration.wav"),replacement=dir.filePath("moved.wav"),shortAudio=dir.filePath("short.wav");
        QString ffmpeg=QStandardPaths::findExecutable("ffmpeg");if(ffmpeg.isEmpty())ffmpeg="/opt/homebrew/bin/ffmpeg";
        QProcess create;create.start(ffmpeg,{"-v","error","-f","lavfi","-i","sine=duration=1.5:sample_rate=48000","-c:a","pcm_s16le","-y",audio});
        QVERIFY(create.waitForFinished(10000));QCOMPARE(create.exitCode(),0);
        create.start(ffmpeg,{"-v","error","-i",audio,"-t","0.5","-y",shortAudio});QVERIFY(create.waitForFinished(10000));QCOMPARE(create.exitCode(),0);
        EditorController e;QVERIFY(e.importFiles({fixture()}));e.setVoicePath(audio);e.setVoiceStart(.25);e.setMusicPath(audio);e.updateAudio(2,{{"loopToEnd",false},{"fadeOutFrames",30}});
        for(const auto &asset:e.project().assets)QVERIFY(asset.path!=audio);
        const auto project=dir.filePath("audio.isaac.json");QVERIFY(e.saveTo(project));
        const auto saved=loadProject(project);QVERIFY(saved);QCOMPARE(saved->voiceDurationFrames,90);QCOMPARE(saved->musicDurationFrames,90);
        QFile data(project);QVERIFY(data.open(QIODevice::ReadOnly));auto json=QJsonDocument::fromJson(data.readAll()).object();data.close();
        const auto writeJson=[&]{if(!data.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;const auto written=data.write(QJsonDocument(json).toJson());data.close();return written>0;};
        for(const auto &bad:{QJsonValue(-1),QJsonValue("90"),QJsonValue(90.5),QJsonValue(86400*60+1)}){json["voiceDurationFrames"]=bad;QVERIFY(writeJson());QVERIFY(!loadProject(project));}
        json.remove("voiceDurationFrames");json.remove("musicDurationFrames");QVERIFY(writeJson());
        EditorController legacy;QVERIFY(legacy.loadFrom(project));QCOMPARE(legacy.project().voiceDurationFrames,90);QCOMPARE(legacy.project().musicDurationFrames,90);QVERIFY(!legacy.dirty());QVERIFY(legacy.saveTo(project));
        QVERIFY(QFile::rename(audio,replacement));
        EditorController offline;QVERIFY(offline.loadFrom(project));QCOMPARE(offline.voiceDuration(),1.5);
        offline.selectAudio(1);QCOMPARE(offline.currentAudio()["playableFrames"].toInt(),90);QCOMPARE(offline.project().voiceStartSeconds,.25);
        offline.selectAudio(2);QCOMPARE(offline.currentAudio()["playableFrames"].toInt(),90);QCOMPARE(offline.currentAudio()["fadeOutFrames"].toInt(),30);
        QuickTimeline timeline;offline.attachTimeline(&timeline);QVERIFY(!timeline.audioRect(1).isEmpty());QVERIFY(!timeline.audioRect(2).isEmpty());
        QVERIFY(!offline.relinkMedia(audio,shortAudio));QVERIFY(!offline.dirty());
        QVERIFY(offline.relinkMedia(audio,replacement));offline.undo();QCOMPARE(offline.project().voicePath,audio);QCOMPARE(offline.voiceDuration(),1.5);
        offline.setVoicePath({});offline.undo();QCOMPARE(offline.voiceDuration(),1.5);
    }
    void fadesClampAfterTrimsAndSplitHasNoNewAudioFade() {
        EditorController e;QVERIFY(e.importFiles({fixture()}));e.selectClip(0);e.updateAudio(0,{{"fadeInFrames",90},{"fadeOutFrames",90}});
        QVERIFY(e.trimClip(0,0,.5));QCOMPARE(e.project().clips[0].audio.fadeInFrames,30);QCOMPARE(e.project().clips[0].audio.fadeOutFrames,30);
        e.undo();e.seek(60);e.split();QCOMPARE(e.project().clips.size(),2);QCOMPARE(e.project().clips[0].audio.fadeOutFrames,0);QCOMPARE(e.project().clips[1].audio.fadeInFrames,0);
    }
    void controlsRoundtripClampAndSoloStayOutOfSavedMix() {
        EditorController e;QVERIFY(e.importFiles({fixture(),fixture()}));e.setVoicePath(fixture());e.setMusicPath(fixture());
        QVERIFY(QMetaObject::invokeMethod(&e,"updateAudio",Qt::DirectConnection,Q_ARG(int,1),Q_ARG(QVariantMap,QVariantMap({{"gainDb",-7.5},{"muted",true},{"fadeInFrames",999},{"fadeOutFrames",18}}))));
        QCOMPARE(e.project().voiceAudio.gainDb,-7.5);QVERIFY(e.project().voiceAudio.muted);QVERIFY(e.project().voiceAudio.fadeInFrames<=120);
        e.selectClip(0);e.updateAudio(0,{{"gainDb",-3.},{"muted",true}});e.updateAudio(2,{{"loopToEnd",false},{"fadeOutFrames",15}});
        QTemporaryDir dir;const auto file=dir.filePath("roundtrip.json");QVERIFY(e.saveTo(file));
        const auto a=e.project().voiceAudio;auto loaded=loadProject(file);QVERIFY(loaded);QCOMPARE(loaded->voiceAudio,a);QVERIFY(loaded->clips[0].audio.muted);QVERIFY(!loaded->musicLoopToEnd);QCOMPARE(loaded->musicFadeOutFrames,15);
        e.setSolo(1);QCOMPARE(e.soloLane(),1);QVERIFY(!e.dirty());QCOMPARE(e.project().voiceAudio,a);QVERIFY(e.saveTo(file));
        loaded=loadProject(file);QVERIFY(loaded);QCOMPARE(loaded->voiceAudio,a);QVERIFY(loaded->clips[0].audio.muted);
        e.setSolo(1);QCOMPARE(e.soloLane(),-1);QVERIFY(!e.dirty());
        e.updateAudio(1,{{"gainDb",-7.5}});QVERIFY(!e.dirty());
        e.beginAudioGesture();e.updateAudio(1,{{"gainDb",-20.}});e.updateAudio(1,{{"fadeInFrames",-1}});e.endAudioGesture(false);
        QCOMPARE(e.project().voiceAudio,a);QVERIFY(!e.dirty());
    }
    void liveMasterMeterReportsAndResetsClipping() {
        Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);
        EditorController e;QVERIFY(e.importFiles({fixture()}));
        auto *preview=e.findChild<Preview *>();QVERIFY(preview);
        QVERIFY(QMetaObject::invokeMethod(preview,"audioPeak",Qt::DirectConnection,Q_ARG(double,1.2)));
        QVERIFY(e.masterClipped());QVERIFY(e.masterPeak()>0);e.resetClipping();QVERIFY(!e.masterClipped());
        QVERIFY(QMetaObject::invokeMethod(preview,"audioPeak",Qt::DirectConnection,Q_ARG(double,.25)));
        QVERIFY(e.masterPeak()<-11&&e.masterPeak()>-13);QVERIFY(!e.masterClipped());
    }
    void renderedNeighborTransitionsPreserveOnePlayableFrame() {
        QTemporaryDir dir;QString ffmpeg=QStandardPaths::findExecutable("ffmpeg");if(ffmpeg.isEmpty())ffmpeg="/opt/homebrew/bin/ffmpeg";
        for(const QString color:{QString("red"),QString("blue")}){QProcess create;create.start(ffmpeg,{"-v","error","-f","lavfi","-i","color=c="+color+":s=160x90:r=60:d=1","-c:v","libx264","-an","-y",dir.filePath(color+".mp4")});QVERIFY(create.waitForFinished(10000));QCOMPARE(create.exitCode(),0);}
        Project p;p.clips={{dir.filePath("red.mp4"),0,1},{dir.filePath("blue.mp4"),0,1},{dir.filePath("red.mp4"),0,1}};
        QVERIFY(p.setTransition(1,"dissolve",30));QVERIFY(p.setTransition(2,"dissolve",50));
        QCOMPARE(p.transitionFramesBefore(1),30);QCOMPARE(p.transitionFramesBefore(2),29);QCOMPARE(p.durationFrames(),121);
        Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);QString error;auto timeline=Media::buildTimeline(p,profile,&error);QVERIFY2(timeline,qPrintable(error));QCOMPARE(timeline->get_playtime(),121);
        auto colorAt=[&](Mlt::Tractor &t,int at){t.seek(at);std::unique_ptr<Mlt::Frame> frame(t.get_frame());auto format=mlt_image_rgba;int w=160,h=90;uint8_t *pixels=nullptr;if(mlt_frame_get_image(frame->get_frame(),&pixels,&format,&w,&h,0)||!pixels)return QColor();return QImage(pixels,w,h,w*4,QImage::Format_RGBA8888).pixelColor(w/2,h/2);};
        auto a=colorAt(*timeline,45),b=colorAt(*timeline,75);QVERIFY(a.red()>50&&a.blue()>50);QVERIFY(b.red()>50&&b.blue()>50);QVERIFY(colorAt(*timeline,120).red()>200);
        const auto output=dir.filePath("cuts.mp4");QVERIFY2(Media::exportMp4(p,output,{},&error),qPrintable(error));
        Project exported;exported.clips={{output,0,121/60.0}};auto rendered=Media::buildTimeline(exported,profile,&error);QVERIFY2(rendered,qPrintable(error));QCOMPARE(rendered->get_playtime(),121);
        auto c=colorAt(*rendered,45);QVERIFY(std::abs(c.red()-a.red())<15&&std::abs(c.blue()-a.blue())<15);
        QVERIFY(p.setTransition(1,"cut",0));QCOMPARE(p.transitionFramesBefore(1),0);QCOMPARE(p.transitionFramesBefore(2),29);
    }
    void renderedAudioHasGainMuteFadesAndLoop() {
        QTemporaryDir dir;const auto media=dir.filePath("tone.mov");const auto wav=dir.filePath("voice.wav");
        QString ffmpeg=QStandardPaths::findExecutable("ffmpeg");if(ffmpeg.isEmpty())ffmpeg="/opt/homebrew/bin/ffmpeg";
        QProcess create;create.start(ffmpeg,{"-v","error","-f","lavfi","-i","color=c=red:s=160x90:r=60:d=3","-f","lavfi","-i","sine=frequency=600:sample_rate=48000:duration=3","-af","volume=4","-c:v","libx264","-c:a","pcm_s16le","-y",media});
        QVERIFY(create.waitForFinished(20000));QVERIFY2(create.exitCode()==0,create.readAllStandardError());
        create.start(ffmpeg,{"-v","error","-i",media,"-vn","-t","1","-c:a","pcm_s16le","-y",wav});QVERIFY(create.waitForFinished(10000));QCOMPARE(create.exitCode(),0);
        Mlt::Profile profile;Media::configureOutputProfile(profile,160,90);
        auto peak=[&](const Project &p,int at){QString error;auto t=Media::buildTimeline(p,profile,&error);if(!t)return -1.;t->set_speed(1);t->seek(at);std::unique_ptr<Mlt::Frame> f(t->get_frame());mlt_audio_format format=mlt_audio_f32le;int rate=48000,channels=2,samples=800;auto *data=static_cast<float*>(f->get_audio(format,rate,channels,samples));if(!data||format!=mlt_audio_f32le)return -2.;double value=0;for(int i=0;i<samples*channels;++i)value=std::max(value,double(std::abs(data[i])));return value;};
        Project p;p.clips={{media,0,3}};const double baseline=peak(p,30);QVERIFY(baseline>.3&&baseline<.6);
        p.clips[0].audio.gainDb=-6;QVERIFY(std::abs(peak(p,30)/baseline-.5012)<.03);
        p.clips[0].audio.muted=true;QVERIFY(peak(p,30)<.00001);
        p.clips[0].audio={};p.clips[0].audio.fadeInFrames=60;p.clips[0].audio.fadeOutFrames=60;
        QVERIFY(peak(p,0)<baseline*.03);QVERIFY(peak(p,30)>baseline*.4&&peak(p,30)<baseline*.6);QVERIFY(peak(p,179)<baseline*.03);
        p.clips[0].audio.muted=true;p.voicePath=wav;p.voiceStartSeconds=1;p.voiceAudio.gainDb=-6;
        QVERIFY(peak(p,30)<.00001);QVERIFY(peak(p,90)>baseline*.45&&peak(p,90)<baseline*.55);QVERIFY(peak(p,150)<.00001);
        p.voicePath.clear();p.musicPath=wav;p.musicDb=0;p.musicLoopToEnd=true;QVERIFY(peak(p,150)>baseline*.9);
        p.musicLoopToEnd=false;QVERIFY(peak(p,150)<.00001);
        p.musicLoopToEnd=true;p.musicFadeOutFrames=60;QVERIFY(peak(p,179)<baseline*.03);
        QString error;const auto output=dir.filePath("mix.mp4");QVERIFY2(Media::exportMp4(p,output,{},&error),qPrintable(error));
        Project exported;exported.clips={{output,0,3}};QVERIFY(std::abs(peak(exported,90)-peak(p,90))<baseline*.08);QVERIFY(peak(exported,179)<baseline*.08);
        EditorController e;QVERIFY(e.importFiles({media}));e.selectClip(0);e.updateAudio(0,{{"gainDb",12.}});e.togglePlayback();
        QTRY_VERIFY_WITH_TIMEOUT(e.masterClipped(),3000);e.togglePlayback();e.resetClipping();QVERIFY(!e.masterClipped());

    }
    void directAudioAndTransitionGesturesEmitPreviewAndCancel() {
        QQuickWindow window;window.resize(1000,420);QuickTimeline view(window.contentItem());view.setSize({1000,420});
        Project p;p.clips={{"a.mp4",0,4},{"b.mp4",0,4}};p.voicePath="voice.wav";p.musicPath="music.wav";p.crossfadeFrames=30;
        view.setProject(p,{4,4},3);window.show();QTest::qWait(20);
        QRectF audio;QVERIFY(QMetaObject::invokeMethod(&view,"audioRect",Qt::DirectConnection,Q_RETURN_ARG(QRectF,audio),Q_ARG(int,1)));
        QSignalSpy starts(&view,SIGNAL(audioGestureStarted()));QSignalSpy moves(&view,SIGNAL(audioPreviewRequested(int,QVariantMap)));QSignalSpy ends(&view,SIGNAL(audioGestureFinished(bool)));
        const auto center=audio.center().toPoint()+QPoint(0,8);
        QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,center);QTest::mouseMove(&window,center+QPoint(70,0),1);
        QVERIFY(moves.size()>0);QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,center+QPoint(70,0));
        QCOMPARE(starts.size(),1);QCOMPARE(ends.size(),1);QCOMPARE(ends[0][0].toBool(),false);QVERIFY(moves.last()[1].toMap()["startFrame"].toInt()>0);
        QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,center);QTest::mouseMove(&window,center+QPoint(90,0),1);QTest::keyClick(&window,Qt::Key_Escape);
        QCOMPARE(ends.size(),2);QCOMPARE(ends[1][0].toBool(),true);
        QRectF transition;QVERIFY(QMetaObject::invokeMethod(&view,"transitionRect",Qt::DirectConnection,Q_RETURN_ARG(QRectF,transition),Q_ARG(int,1)));
        QSignalSpy cutMoves(&view,SIGNAL(transitionPreviewRequested(int,int)));
        const auto edge=QPoint(qRound(transition.right()-2),qRound(transition.center().y()));
        QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,edge);QTest::mouseMove(&window,edge+QPoint(30,0),1);
        QVERIFY(cutMoves.size()>0);QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,edge+QPoint(30,0));
        QVERIFY(cutMoves.last()[1].toInt()>30);
    }
    void localCutsDoNotShortenDistantCuts() {
        QTemporaryDir dir; const auto file=dir.filePath("local.json");
        QJsonArray clips;
        for(double seconds:{3.,1.,3.,3.,3.}) clips.append(QJsonObject{{"path","a.mp4"},{"in",0},{"out",seconds}});
        auto c=clips[1].toObject();c["transitionFrames"]=20;c["transitionType"]="dissolve";clips[1]=c;
        c=clips[2].toObject();c["transitionFrames"]=20;c["transitionType"]="dissolve";clips[2]=c;
        c=clips[3].toObject();c["transitionFrames"]=48;c["transitionType"]="dissolve";clips[3]=c;
        c=clips[4].toObject();c["transitionFrames"]=48;c["transitionType"]="dissolve";clips[4]=c;
        QFile f(file);QVERIFY(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(QJsonObject{{"version",6},{"clips",clips}}).toJson());f.close();
        QString error;auto p=loadProject(file,&error);QVERIFY2(p,qPrintable(error));
        QCOMPARE(p->transitionFramesBefore(1),20);QCOMPARE(p->transitionFramesBefore(2),20);
        QCOMPARE(p->transitionFramesBefore(4),48);QCOMPARE(p->durationFrames(),644);
        QVERIFY(p->splitClip(3,80));QCOMPARE(p->transitionFramesBefore(4),0);QCOMPARE(p->durationFrames(),644);
        QVERIFY(saveProject(*p,file,&error));auto roundtrip=loadProject(file,&error);QVERIFY(roundtrip);QCOMPARE(roundtrip->durationFrames(),644);
    }
    void audioGestureIsTemporaryAndCommitsOnce() {
        EditorController e;QVERIFY(e.importFiles({fixture()}));e.setVoicePath(fixture());
        const auto before=e.project().voiceStartSeconds;
        QVERIFY(QMetaObject::invokeMethod(&e,"beginAudioGesture",Qt::DirectConnection));
        QVariantMap fields{{"gainDb",-9.0},{"fadeInFrames",12},{"startFrame",30}};
        QVERIFY(QMetaObject::invokeMethod(&e,"updateAudio",Qt::DirectConnection,Q_ARG(int,1),Q_ARG(QVariantMap,fields)));
        QCOMPARE(e.project().voiceStartSeconds,before);
        QVERIFY(QMetaObject::invokeMethod(&e,"endAudioGesture",Qt::DirectConnection,Q_ARG(bool,false)));
        QCOMPARE(e.project().voiceStartSeconds,.5);e.undo();QCOMPARE(e.project().voiceStartSeconds,before);
        e.redo();QVERIFY(QMetaObject::invokeMethod(&e,"beginAudioGesture",Qt::DirectConnection));
        fields["startFrame"]=60;QVERIFY(QMetaObject::invokeMethod(&e,"updateAudio",Qt::DirectConnection,Q_ARG(int,1),Q_ARG(QVariantMap,fields)));
        QVERIFY(QMetaObject::invokeMethod(&e,"endAudioGesture",Qt::DirectConnection,Q_ARG(bool,true)));
        QCOMPARE(e.project().voiceStartSeconds,.5);
    }
    void fullyClampedTransitionRequestCreatesNoHistory() {
        EditorController e;QVERIFY(e.importFiles({fixture(),fixture()}));QVERIFY(e.trimClip(0,0,.5));QVERIFY(e.trimClip(1,0,.5));e.setCrossfade(.5);
        QTemporaryDir dir;QVERIFY(e.saveTo(dir.filePath("clean.json")));QCOMPARE(e.project().transitionFramesBefore(1),29);
        e.setTransition(1,"dissolve",600);QCOMPARE(e.project().transitionFramesBefore(1),29);QVERIFY(!e.dirty());
    }
    void transitionPreviewTargetsOnlyTheCurrentCutAndRevertsToStart() {
        EditorController e;QVERIFY(e.importFiles({fixture(),fixture(),fixture()}));e.setCrossfade(.5);
        e.beginAudioGesture();e.setTransition(1,"dissolve",12);e.setTransition(2,"dissolve",18);e.endAudioGesture(false);
        QCOMPARE(e.project().transitionFramesBefore(1),30);QCOMPARE(e.project().transitionFramesBefore(2),18);
        e.undo();QCOMPARE(e.project().transitionFramesBefore(2),30);
        e.beginAudioGesture();e.setTransition(1,"dissolve",12);e.setTransition(1,"dissolve",30);e.endAudioGesture(false);
        QCOMPARE(e.project().transitionFramesBefore(1),30);QCOMPARE(e.undoText(),QString("Crossfade duration"));
    }
    void transitionCommandAndCanceledDragKeepHistory() {
        EditorController e;QVERIFY(e.importFiles({fixture(),fixture(),fixture()}));e.setCrossfade(.5);
        QVERIFY(QMetaObject::invokeMethod(&e,"setTransition",Qt::DirectConnection,Q_ARG(int,1),Q_ARG(QString,QString("dissolve")),Q_ARG(int,12)));
        QCOMPARE(e.project().transitionFramesBefore(1),12);QCOMPARE(e.project().transitionFramesBefore(2),30);
        e.undo();QCOMPARE(e.project().transitionFramesBefore(1),30);e.redo();
        QVERIFY(QMetaObject::invokeMethod(&e,"beginAudioGesture",Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(&e,"setTransition",Qt::DirectConnection,Q_ARG(int,1),Q_ARG(QString,QString("cut")),Q_ARG(int,0)));
        QVERIFY(QMetaObject::invokeMethod(&e,"endAudioGesture",Qt::DirectConnection,Q_ARG(bool,true)));
        QCOMPARE(e.project().transitionFramesBefore(1),12);
    }
};
QTEST_MAIN(AudioTransitionTest)
#include "audio_transition_test.moc"
