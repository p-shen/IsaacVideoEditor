#include "performance_report.h"
#include "editor_controller.h"
#include "preview.h"
#include "preview_item.h"
#include "performance.h"
#include "export_job.h"
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <limits>

namespace {
bool waitUntil(const std::function<bool()> &ready,int timeout=15000){
    QElapsedTimer clock;clock.start();
    while(!ready()&&clock.elapsed()<timeout){QCoreApplication::processEvents(QEventLoop::AllEvents,5);QThread::msleep(1);}
    return ready();
}
}
int runBenchmarkExport(const QString &projectPath,const QString &outputPath,bool softwareOnly,bool iphone){
    QString error;const auto project=loadProject(projectPath,&error);
    if(!project||outputPath.isEmpty()||QFileInfo::exists(outputPath)){qCritical()<<"Choose a valid project and a new benchmark output:"<<error;return 2;}
    const auto event=[](const QJsonObject &value){const auto data=QJsonDocument(value).toJson(QJsonDocument::Compact)+'\n';fwrite(data.constData(),1,size_t(data.size()),stdout);fflush(stdout);};
    ExportJob job(QCoreApplication::applicationFilePath());QEventLoop loop;bool success=false;QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{event({{"event","diagnostic"},{"message","Benchmark deadline reached; canceling export"}});job.cancel();});
    QObject::connect(&job,&ExportJob::diagnostic,&loop,[&](const QString &message){event({{"event","diagnostic"},{"message",message}});});
    QObject::connect(&job,&ExportJob::progressChanged,&loop,[&](int progress){event({{"event","progress"},{"value",progress}});});
    QObject::connect(&job,&ExportJob::finished,&loop,[&](bool ok,bool canceled,const QString &message){success=ok;error=message;event({{"event","result"},{"ok",ok},{"canceled",canceled},{"error",message}});loop.quit();});
    if(!job.start(*project,outputPath,&error,iphone,softwareOnly)){qCritical()<<error;return 2;}
    deadline.start(int(std::min<qint64>(std::numeric_limits<int>::max(),std::max<qint64>(90000,qint64(project->durationFrames())*400))));
    loop.exec();job.stopAndWait();if(!success)qCritical()<<error;return success?0:1;
}
int runPerformanceReport(const QString &projectPath,const QString &reportPath){
    if(reportPath.isEmpty()||QFileInfo::exists(reportPath)){qCritical()<<"Choose a new performance report path.";return 2;}
    QString error;const auto project=loadProject(projectPath,&error);
    if(!project||project->clips.isEmpty()){qCritical()<<"Cannot benchmark project:"<<error;return 2;}
    for(const auto &clip:project->clips)if(!QFileInfo(clip.path).isFile()){qCritical()<<"Benchmark source is offline:"<<clip.path;return 2;}
    QTemporaryDir temporary;if(!temporary.isValid())return 2;
    // Isolate benchmark preferences and generated copies from the user's editing cache.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    QCoreApplication::setApplicationName("IsaacVideoEditorBenchmark");
    // Measure decode/presentation independently of an audio device or its permissions.
    if(qEnvironmentVariableIsEmpty("SDL_AUDIODRIVER"))qputenv("SDL_AUDIODRIVER","dummy");
    EditorController editor;
    if(!editor.setCacheDirectory(temporary.filePath("previews")))return 2;
    QElapsedTimer clock;clock.start();
    if(!editor.loadFrom(projectPath)){qCritical()<<editor.notice();return 2;}
    auto *preview=editor.findChild<Preview*>();
    if(!preview||!waitUntil([&]{return !editor.loading()&&!preview->isSeeking();})||editor.frame().isNull()){
        qCritical()<<"Benchmark preparation failed:"<<editor.notice();return 2;}
    const auto loadMs=clock.elapsed();
    const bool visible=QCoreApplication::arguments().contains("--visible");
    QQuickWindow window;window.resize(1280,720);
    auto *viewer=new VideoPreview(window.contentItem());viewer->setSize({1280,720});viewer->setController(&editor);
    if(visible){window.setTitle("IsaacVideoEditor performance check");window.show();}
    const int last=editor.duration()-1;
    const QVector<int> positions{0,last/4,last/2,last*3/4,last};QJsonArray seeks;
    for(const auto &phase:{QString("cold"),QString("warm")})for(int frame:positions){
        clock.restart();editor.seek(frame);
        if(!waitUntil([&]{return !preview->isSeeking()&&!editor.loading();})){qCritical()<<"Seek benchmark timed out";return 2;}
        seeks.append(QJsonObject{{"phase",phase},{"frame",frame},{"ms",clock.nsecsElapsed()/1000000.0}});
    }
    editor.seek(0);if(!waitUntil([&]{return !preview->isSeeking()&&!editor.loading();}))return 2;
    const auto playbackBefore=preview->diagnostics();const auto uploadsBefore=viewer->textureUploads();QStringList playbackErrors;
    const auto errorConnection=QObject::connect(preview,&Preview::errorOccurred,&editor,[&](const QString &message){playbackErrors.append(message);});
    editor.togglePlayback();const bool playbackStarted=preview->isPlaying();clock.restart();
    while(clock.elapsed()<1500&&preview->isPlaying()){QCoreApplication::processEvents(QEventLoop::AllEvents,5);QThread::msleep(1);}
    const auto playbackMs=clock.elapsed();
    if(editor.playing())editor.togglePlayback();
    QObject::disconnect(errorConnection);const auto playbackAfter=preview->diagnostics();
    QJsonObject playback{{"started",playbackStarted},{"elapsedMs",playbackMs},{"audioDriver",qEnvironmentVariable("SDL_AUDIODRIVER")},
        {"textureUploads",qint64(viewer->textureUploads()-uploadsBefore)},{"errors",QJsonArray::fromStringList(playbackErrors)}};
    for(const auto *counter:{"receivedFrames","deliveredFrames","skippedFrames"})playback.insert(counter,qint64(playbackAfter[counter].toULongLong()-playbackBefore[counter].toULongLong()));
    const auto resources=Performance::resources();const auto memory=Performance::processMemory();
    const QJsonObject report{{"schemaVersion",1},{"appVersion",QCoreApplication::applicationVersion()},
        {"measurement",visible?"Visible preview and decoded seek delivery":"Headless decoded seek delivery; excludes display presentation"},
        {"project",QFileInfo(projectPath).absoluteFilePath()},{"loadMs",loadMs},{"seeks",seeks},
        {"seekPhases","Cold is the first sweep after loading; warm repeats the same positions. Initial window setup may affect the first visible seek."},{"playback",playback},
        {"editing",QJsonObject::fromVariantMap(editor.performanceDiagnostics())},
        {"hardware",QJsonObject{{"logicalCpus",resources.logicalCpus},{"totalMemoryBytes",qint64(resources.totalMemoryBytes)},{"availableMemoryBytes",qint64(resources.availableMemoryBytes)}}},
        {"process",QJsonObject{{"residentBytes",qint64(memory.residentBytes)},{"peakResidentBytes",qint64(memory.peakResidentBytes)}}},
        {"presentation",QJsonObject{{"visible",visible},{"graphicsApi",int(window.rendererInterface()->graphicsApi())},{"textureUploads",qint64(viewer->textureUploads())}}}};
    const auto json=QJsonDocument(report).toJson();
    QSaveFile output(reportPath);if(!output.open(QIODevice::WriteOnly)||output.write(json)!=json.size()||!output.commit()){
        qCritical()<<"Cannot write performance report:"<<output.errorString();return 2;}
    qInfo()<<"Performance report saved:"<<reportPath;return 0;
}
