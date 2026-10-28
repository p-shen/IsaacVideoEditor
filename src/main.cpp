#include "editor_controller.h"
#include "export_job.h"
#include "qml_registration.h"
#include "mlt_engine.h"
#include "theme.h"
#include "runtime_paths.h"
#include "preview.h"
#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QEventLoop>
#include <QIcon>
#include <QImageReader>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSslSocket>
#include <QTimer>
#include <mlt++/Mlt.h>

namespace {
int packageSmokeTest(const QString &fixture) {
    if (!QSslSocket::supportsSsl()) {
        qCritical() << "The packaged HTTPS backend is unavailable; update downloads would fail";
        return 2;
    }
    if (!QFileInfo(fixture).isFile()) {
        qCritical() << "Smoke test requires an existing video fixture:" << fixture;
        return 2;
    }
    const auto runtime = bundledRuntimePaths(QCoreApplication::applicationDirPath());
    for (const auto &tool : {"ffmpeg", "ffprobe"}) {
        const auto executable = QStandardPaths::findExecutable(tool);
        if (executable.isEmpty() || (!runtime.executables.isEmpty() &&
            QDir(QFileInfo(executable).absolutePath()) != QDir(runtime.executables))) {
            qCritical() << "Missing bundled tool:" << tool;
            return 2;
        }
        QProcess process;
        process.start(executable, {"-version"});
        if (!process.waitForFinished(15000) || process.exitStatus() != QProcess::NormalExit || process.exitCode()) {
            qCritical() << "Cannot run" << tool << process.readAllStandardError();
            return 2;
        }
    }
    if (!QImageReader(":/qml/icons/play.svg").canRead()) {
        qCritical() << "Qt SVG image plugin is unavailable";
        return 2;
    }
    QString error;
    const auto info = Media::mediaInfo(fixture, &error);
    if (!info || info->durationSeconds < .25) {
        qCritical() << "Cannot read media:" << error;
        return 2;
    }
    Project project;
    project.clips.append(Clip{fixture, 0, .25});
    project.canvasWidth = 320;
    project.canvasHeight = 180;
    Mlt::Profile profile;
    Media::configureOutputProfile(profile, 320, 180);
    auto timeline = Media::buildTimeline(project, profile, &error);
    std::unique_ptr<Mlt::Frame> frame(timeline ? timeline->get_frame() : nullptr);
    int width = 320, height = 180;
    auto format = mlt_image_rgba;
    if (!frame || !frame->is_valid() || !frame->get_image(format, width, height)) {
        qCritical() << "Cannot decode a preview frame:" << error;
        return 2;
    }
    {
        Preview preview(profile);
        QEventLoop playback;
        QTimer deadline;
        deadline.setSingleShot(true);
        QString playbackError;
        QObject::connect(&preview, &Preview::errorOccurred, &playback, [&](const QString &message) {
            playbackError = message;
            playback.quit();
        });
        QObject::connect(&preview, &Preview::positionChanged, &playback, [&](int position) {
            if (position > 0) playback.quit();
        });
        QObject::connect(&deadline, &QTimer::timeout, &playback, &QEventLoop::quit);
        if (!preview.setProducer(timeline.get())) {
            qCritical() << "Cannot initialize playback:" << playbackError;
            return 2;
        }
        deadline.start(5000);
        preview.play();
        if (preview.isPlaying()) playback.exec();
        preview.pause();
        if (!playbackError.isEmpty() || preview.position() == 0) {
            qCritical() << "Playback did not advance:" << playbackError;
            return 2;
        }
    }
    QTemporaryDir temporary;
    const auto exportWithWorker=[&]{
        ExportJob job(QCoreApplication::applicationFilePath());QEventLoop wait;QTimer deadline;deadline.setSingleShot(true);bool completed=false;
        QObject::connect(&job,&ExportJob::finished,&wait,[&](bool ok,bool,const QString &message){completed=ok;error=message;wait.quit();});
        QObject::connect(&deadline,&QTimer::timeout,&wait,[&]{error="Export worker timed out.";job.cancel();wait.quit();});
        if(!job.start(project,temporary.filePath("smoke.mp4"),&error))return false;
        deadline.start(30000);wait.exec();job.stopAndWait();return completed;
    };
    if (!temporary.isValid() || !exportWithWorker()) {
        qCritical() << "Cannot export media with worker:" << error;
        return 2;
    }
    if (!exportWithWorker()) {
        qCritical() << "Cannot replace an existing export:" << error;
        return 2;
    }
    qInfo() << "Package smoke test passed: QML, SVG, FFmpeg, media decode, playback and MP4 export";
    return 0;
}
}

int main(int argc,char *argv[]) {
    const bool exportWorker=argc>1&&QByteArray(argv[1])=="--export-worker";
    if(exportWorker)qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication app(argc,argv);
    app.setApplicationName("IsaacVideoEditor");
    app.setApplicationVersion(QStringLiteral(ISAAC_APP_VERSION));
    app.setOrganizationName("IsaacVideoEditor");
    if(exportWorker){
        applyAppTheme(app);
        Media::initializeMlt();const int result=runExportWorker(app.arguments());Mlt::Factory::close();return result;
    }
    QQuickStyle::setStyle("Basic");
    applyAppTheme(app);
    app.setWindowIcon(QIcon(":/isaac-logo.png"));
    Media::initializeMlt();
    registerEditorQmlTypes();
    int result=1;
    {
        EditorController editor;
        editor.enableRecovery();
        bool qmlWarnings = false;
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine,
                         [&qmlWarnings](const QList<QQmlError> &) { qmlWarnings = true; });
        setEditorQmlContext(engine,editor);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        if(!engine.rootObjects().isEmpty()) {
            const auto args=app.arguments();
            const int smoke = args.indexOf("--smoke-test");
            if (smoke >= 0) {
                app.processEvents();
                result = qmlWarnings ? 2 : packageSmokeTest(args.value(smoke + 1));
            } else {
                const int updateStarted=args.indexOf("--update-started");
                if(updateStarted>=0) QTimer::singleShot(0,&app,[args,updateStarted,&qmlWarnings]{
                    if(qmlWarnings)return;
                    const QFileInfo requested(args.value(updateStarted+1));
                    const auto folder=QFileInfo(requested.absolutePath()).canonicalFilePath();
                    const auto temporary=QFileInfo(QDir::tempPath()).canonicalFilePath();
                    if(requested.fileName()!="started" || !QFileInfo(folder).fileName().startsWith("isaac-update-") ||
                       (!folder.startsWith(temporary+'/') && !folder.startsWith("/private/tmp/")))return;
                    QFile ready(QDir(folder).filePath("ready"));
                    if(!ready.open(QIODevice::ReadOnly) || ready.read(128)!="IsaacVideoEditor update 1\n")return;
                    QFile started(QDir(folder).filePath("started"));
                    if(started.open(QIODevice::WriteOnly|QIODevice::NewOnly))started.write("IsaacVideoEditor started 1\n");
                });
                if(args.contains("--update-failed")) QTimer::singleShot(0,&app,[]{
                    QMessageBox::warning(nullptr,"Update was not installed","The update could not be installed. Your previous version has been reopened. Please try the update again.");
                });
                if(args.size()>1&&!args[1].startsWith('-')) editor.loadFrom(args[1]);
                result=app.exec();
            }
        }
    }
    Mlt::Factory::close();
    return result;
}
