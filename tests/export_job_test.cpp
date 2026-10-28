#include "export_job.h"
#include "mlt_engine.h"
#include <QGuiApplication>
#include <QFontDatabase>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdio>
#include <cstdlib>

class ExportJobTest : public QObject {
    Q_OBJECT
    Project sample() const {Project p;p.clips={{QDir::current().filePath("tests/fixtures/seek.mp4"),0,2}};return p;}
    QByteArray contents(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
private slots:
    void actualWorkerExportsAndLogsEncoder() {
        QTemporaryDir dir;ExportJob job(QCoreApplication::applicationFilePath());QSignalSpy done(&job,&ExportJob::finished);QSignalSpy progress(&job,&ExportJob::progressChanged);QString error;
        QVERIFY2(job.start(sample(),dir.filePath("video.mp4"),&error),qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,20000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][2].toString()));QVERIFY(!done[0][1].toBool());
        auto info=Media::mediaInfo(dir.filePath("video.mp4"));QVERIFY(info);QCOMPARE(qRound(info->durationSeconds*60),120);QVERIFY(!progress.isEmpty());QCOMPARE(progress.last()[0].toInt(),100);
        QVERIFY(contents(job.logPath()).contains("Encoder:"));QVERIFY(contents(job.logPath()).contains("Completed"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).size(),0);
    }
    void actualApplicationWorkerPreservesBundledFont() {
        QVERIFY(QFontDatabase::addApplicationFont(":/fonts/PlusJakartaSans.ttf")>=0);
        auto p=sample();p.canvasWidth=1280;p.canvasHeight=720;TextOverlay text;text.fontFamily="Plus Jakarta Sans";text.text="WIDE letters 1234\nAdventure with Isaac";text.fontSize=130;text.shadow=false;text.color="#ff00ff";p.textOverlays={text};
        const auto frame=[](const Project &project){
            Mlt::Profile profile;Media::configureOutputProfile(profile,1280,720);auto timeline=Media::buildTimeline(project,profile);if(!timeline)return QImage{};
            timeline->seek(20);std::unique_ptr<Mlt::Frame> f(timeline->get_frame());auto format=mlt_image_rgba;int w=1280,h=720;uint8_t *pixels=nullptr;
            if(!f||mlt_frame_get_image(f->get_frame(),&pixels,&format,&w,&h,0)||!pixels)return QImage{};
            return QImage(pixels,w,h,w*4,QImage::Format_RGBA8888).copy();
        };
        const auto expected=frame(p);QVERIFY(!expected.isNull());QTemporaryDir dir;const auto output=dir.filePath("font.mp4");ExportJob job(exportWorkerExecutable());QSignalSpy done(&job,&ExportJob::finished);QString error;
        QVERIFY(job.start(p,output,&error));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,20000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][2].toString()));
        Project encoded;encoded.clips={{output,0,2}};const auto actual=frame(encoded);QCOMPARE(actual.size(),expected.size());
        int shared=0,total=0;const auto magenta=[](QColor c){return c.red()>160&&c.blue()>150&&c.green()<110;};
        for(int y=0;y<expected.height();++y)for(int x=0;x<expected.width();++x){const bool a=magenta(actual.pixelColor(x,y)),b=magenta(expected.pixelColor(x,y));shared+=a&&b;total+=a||b;}
        QVERIFY(total>100);qInfo()<<"Bundled-font export glyph overlap:"<<double(shared)/total;QVERIFY2(double(shared)/total>.85,"Worker must render the same bundled font as the editor");
    }
    void cancelAfterWorkerResultPreservesDestination() {
        const auto previous=qgetenv("ISAAC_TEST_EXPORT_MODE");qputenv("ISAAC_TEST_EXPORT_MODE","wait-after-result");const auto restore=qScopeGuard([&]{qputenv("ISAAC_TEST_EXPORT_MODE",previous);});
        QTemporaryDir dir;const auto output=dir.filePath("existing.mp4");QFile old(output);QVERIFY(old.open(QIODevice::WriteOnly));old.write("KEEP");old.close();
        ExportJob job(QCoreApplication::applicationFilePath());QSignalSpy done(&job,&ExportJob::finished);QString error;QVERIFY(job.start(sample(),output,&error));
        QTRY_VERIFY_WITH_TIMEOUT(contents(job.logPath()).contains("\"event\":\"result\""),10000);QCOMPARE(done.count(),0);job.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,6000);QVERIFY(!done[0][0].toBool());QVERIFY(done[0][1].toBool());QCOMPARE(contents(output),QByteArray("KEEP"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).size(),0);
    }
    void cancelDuringWorkerRenderPreservesDestination() {
        QTemporaryDir dir;const auto output=dir.filePath("existing.mp4");QFile old(output);QVERIFY(old.open(QIODevice::WriteOnly));old.write("KEEP");old.close();
        auto p=sample();for(int i=0;i<9;++i)p.clips.append(p.clips.first());p.canvasWidth=1920;p.canvasHeight=1080;
        for(int i=1;i<p.clips.size();++i)p.clips[i].id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        ExportJob job(QCoreApplication::applicationFilePath());QSignalSpy done(&job,&ExportJob::finished);QString error;
        connect(&job,&ExportJob::progressChanged,&job,[&](int value){if(value>0&&value<100)job.cancel();});
        QVERIFY(job.start(p,output,&error));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,20000);QVERIFY(!done[0][0].toBool());QVERIFY(done[0][1].toBool());QCOMPARE(contents(output),QByteArray("KEEP"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).size(),0);
    }
    void cancelAfterPublicationKeepsSuccessfulExport() {
        QTemporaryDir dir;const auto output=dir.filePath("completed.mp4");ExportJob job(QCoreApplication::applicationFilePath());QSignalSpy done(&job,&ExportJob::finished);QString error;
        connect(&job,&ExportJob::progressChanged,&job,[&](int value){if(value==100)job.cancel();});
        QVERIFY(job.start(sample(),output,&error));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,20000);QVERIFY(done[0][0].toBool());QVERIFY(!done[0][1].toBool());QVERIFY(Media::mediaInfo(output).has_value());
    }
    void unavailableHardwareReasonIsLogged() {
#ifndef Q_OS_MACOS
        QSKIP("The automatic hardware probe targets macOS");
#else
        QTemporaryDir dir;const auto tools=dir.filePath("tools");QVERIFY(QDir().mkpath(tools));QFile probe(tools+"/ffmpeg");QVERIFY(probe.open(QIODevice::WriteOnly));probe.write("#!/bin/sh\nprintf 'hardware-probe-test unavailable\\n' >&2\nexit 7\n");probe.close();QVERIFY(probe.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
        const auto previous=qgetenv("PATH");qputenv("PATH",QFile::encodeName(tools)+':'+previous);const auto restore=qScopeGuard([&]{qputenv("PATH",previous);});
        ExportJob job(QCoreApplication::applicationFilePath());QSignalSpy done(&job,&ExportJob::finished);QString error;const auto output=dir.filePath("software.mp4");QVERIFY(job.start(sample(),output,&error));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][2].toString()));QVERIFY(contents(output).contains("x264 - core"));QVERIFY(contents(job.logPath()).contains("hardware-probe-test unavailable"));
#endif
    }
    void failedLaunchPreservesDestination() {
        QTemporaryDir dir;const auto output=dir.filePath("existing.mp4");QFile old(output);QVERIFY(old.open(QIODevice::WriteOnly));old.write("KEEP");old.close();
        ExportJob job(dir.filePath("missing-program"));QSignalSpy done(&job,&ExportJob::finished);QString error;QVERIFY(job.start(sample(),output,&error));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,5000);QVERIFY(!done[0][0].toBool());QVERIFY(!done[0][2].toString().isEmpty());QCOMPARE(contents(output),QByteArray("KEEP"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).size(),0);
    }
    void sourceCollisionIsRejected() {
        QTemporaryDir dir;const auto path=dir.filePath("source.mp4");QVERIFY(QFile::copy(sample().clips[0].path,path));auto p=sample();p.assets.append({path,"video",120,0,0,120});
        const auto original=contents(path);ExportJob job(QCoreApplication::applicationFilePath());QString error;QVERIFY(!job.start(p,path,&error));QVERIFY(!error.isEmpty());QCOMPARE(contents(path),original);
    }
    void cancelDuringStartupPreservesDestination() {
        QTemporaryDir dir;const auto output=dir.filePath("existing.mp4");QFile old(output);QVERIFY(old.open(QIODevice::WriteOnly));old.write("KEEP");old.close();
        ExportJob job(QCoreApplication::applicationFilePath());QSignalSpy done(&job,&ExportJob::finished);QString error;QVERIFY(job.start(sample(),output,&error));job.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,6000);QVERIFY(!done[0][0].toBool());QVERIFY(done[0][1].toBool());QCOMPARE(contents(output),QByteArray("KEEP"));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).size(),0);
    }
    void malformedOrMissingResultCannotPublish_data(){QTest::addColumn<QByteArray>("mode");QTest::newRow("missing")<<QByteArray("missing");QTest::newRow("malformed")<<QByteArray("malformed");QTest::newRow("flood")<<QByteArray("flood");}
    void malformedOrMissingResultCannotPublish() {
        QFETCH(QByteArray,mode);const auto previous=qgetenv("ISAAC_TEST_EXPORT_MODE");qputenv("ISAAC_TEST_EXPORT_MODE",mode);const auto restore=qScopeGuard([&]{qputenv("ISAAC_TEST_EXPORT_MODE",previous);});
        QTemporaryDir dir;const auto output=dir.filePath("existing.mp4");QFile old(output);QVERIFY(old.open(QIODevice::WriteOnly));old.write("KEEP");old.close();
        ExportJob job(QCoreApplication::applicationFilePath());QSignalSpy done(&job,&ExportJob::finished);QString error;QVERIFY(job.start(sample(),output,&error));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY(!done[0][0].toBool());QCOMPARE(contents(output),QByteArray("KEEP"));QVERIFY(QFileInfo(job.logPath()).size()<=512*1024);
    }
    void hardwareWorkerCrashRetriesOnceWithSoftware() {
        const auto previous=qgetenv("ISAAC_TEST_EXPORT_MODE");qputenv("ISAAC_TEST_EXPORT_MODE","crash-hardware");const auto restore=qScopeGuard([&]{qputenv("ISAAC_TEST_EXPORT_MODE",previous);});
        QTemporaryDir dir;ExportJob job(QCoreApplication::applicationFilePath());QSignalSpy done(&job,&ExportJob::finished);QString error;QVERIFY(job.start(sample(),dir.filePath("retried.mp4"),&error));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,20000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][2].toString()));
        QVERIFY(contents(dir.filePath("retried.mp4")).contains("x264 - core"));QVERIFY(contents(job.logPath()).contains("Retrying with software"));
    }
};
int main(int argc,char **argv){
    QGuiApplication app(argc,argv);app.setApplicationName("IsaacExportJobTest");
    const auto args=app.arguments();
    if(args.value(1)=="--export-worker"){
        const auto mode=qgetenv("ISAAC_TEST_EXPORT_MODE");
        if(mode=="crash-hardware"&&!args.contains("--software")){fputs("{\"event\":\"diagnostic\",\"message\":\"Encoder: h264_videotoolbox\"}\n",stdout);fflush(stdout);std::abort();}
        if(mode=="missing"||mode=="malformed"||mode=="flood"){
            QFile staged(args.value(3));if(!staged.open(QIODevice::WriteOnly))return 3;staged.write("INVALID VIDEO");staged.close();
            if(mode=="flood"){const QByteArray flood(700*1024,'x');fwrite(flood.constData(),1,flood.size(),stderr);}
            if(mode!="missing")fputs("malformed protocol\n{\"event\":\"result\",\"ok\":true}\n",stdout);
            return 0;
        }
        const int result=runExportWorker(args);
        if(mode=="wait-after-result"){QTimer::singleShot(30000,&app,&QCoreApplication::quit);return app.exec();}
        return result;
    }
    ExportJobTest test;return QTest::qExec(&test,argc,argv);
}
#include "export_job_test.moc"
