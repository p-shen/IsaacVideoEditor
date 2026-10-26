#include "editor_controller.h"
#include "qml_registration.h"
#include "mlt_engine.h"
#include "theme.h"
#include "runtime_paths.h"
#include "preview.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QEventLoop>
#include <QIcon>
#include <QImageReader>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <mlt++/Mlt.h>

namespace {
int packageSmokeTest(const QString &fixture) {
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
    if (!temporary.isValid() || !Media::exportMp4(project, temporary.filePath("smoke.mp4"), {}, &error)) {
        qCritical() << "Cannot export media:" << error;
        return 2;
    }
    if (!Media::exportMp4(project, temporary.filePath("smoke.mp4"), {}, &error)) {
        qCritical() << "Cannot replace an existing export:" << error;
        return 2;
    }
    qInfo() << "Package smoke test passed: QML, SVG, FFmpeg, media decode, playback and MP4 export";
    return 0;
}
}

int main(int argc,char *argv[]) {
    QApplication app(argc,argv);
    app.setApplicationName("IsaacVideoEditor");
    app.setOrganizationName("IsaacVideoEditor");
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
                if(args.size()>1&&!args[1].startsWith('-')) editor.loadFrom(args[1]);
                result=app.exec();
            }
        }
    }
    Mlt::Factory::close();
    return result;
}
