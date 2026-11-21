#include "editor_controller.h"
#include "subtitle_models.h"
#include "qml_registration.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class SubtitleControllerTest final:public QObject {
    Q_OBJECT
    static void put(const QString &p,const QByteArray &b){QFile f(p);QVERIFY(f.open(QIODevice::WriteOnly));f.write(b);}
    QList<SubtitleModelSpec> catalog() const {return {{"large-v3","Large",5,QCryptographicHash::hash("model",QCryptographicHash::Sha256).toHex(),QUrl("https://huggingface.co/unused")}};}
    QString footage() const{return QDir::current().filePath("tests/fixtures/seek.mp4");}
private slots:
    void initTestCase(){QQuickStyle::setStyle("Basic");registerEditorQmlTypes();if(qEnvironmentVariableIntValue("ISAAC_RUN_SUBTITLE_ACCEPTANCE")!=1)qputenv("ISAAC_WHISPER_EXECUTABLE",QCoreApplication::applicationFilePath().toUtf8());QSettings().setValue("subtitles/cpuOnly",true);}
    void cleanupTestCase(){qunsetenv("ISAAC_WHISPER_EXECUTABLE");QSettings().remove("subtitles/cpuOnly");}
    void generationIsSeparateAndImportIsOneUndo(){
        QTemporaryDir dir;put(dir.filePath("ggml-large-v3.bin"),"model");SubtitleModels models(nullptr,dir.path(),nullptr,catalog());QTRY_VERIFY(models.ready());
        EditorController e(nullptr,Performance::budget(),&models);QVERIFY(e.importFiles({footage()}));QVERIFY(e.saveTo(dir.filePath("saved.isaac.json")));const auto history=e.undoText();
        const auto srt=dir.filePath("captions.srt");QVERIFY(e.generateSrtTo(srt));QVERIFY(e.generatingSubtitles());QVERIFY(!e.exportTo(dir.filePath("busy.mp4")));
        QTRY_VERIFY_WITH_TIMEOUT(!e.generatingSubtitles(),15000);QCOMPARE(e.subtitleState(),QString("Completed"));QVERIFY(!e.dirty());QCOMPARE(e.undoText(),history);QVERIFY(e.project().captions.isEmpty());QVERIFY(e.canImportGeneratedSubtitles());
        QVERIFY(e.importGeneratedSubtitles(false));QCOMPARE(e.project().captions.size(),1);QVERIFY(!e.canImportGeneratedSubtitles());e.undo();QVERIFY(e.project().captions.isEmpty());QVERIFY(!e.dirty());
    }
    void changedRevisionCannotAutoImportAndDuplicateAppendIsExplicit(){
        QTemporaryDir dir;put(dir.filePath("ggml-large-v3.bin"),"model");SubtitleModels models(nullptr,dir.path(),nullptr,catalog());QTRY_VERIFY(models.ready());
        EditorController e(nullptr,Performance::budget(),&models);QVERIFY(e.importFiles({footage()}));QVERIFY(e.generateSrtTo(dir.filePath("first.srt")));e.addCaption();QTRY_VERIFY_WITH_TIMEOUT(!e.generatingSubtitles(),15000);QVERIFY(!e.canImportGeneratedSubtitles());QVERIFY(!e.importGeneratedSubtitles(true));QCOMPARE(e.project().captions.size(),1);
        QVERIFY(e.generateSrtTo(dir.filePath("second.srt")));QTRY_VERIFY_WITH_TIMEOUT(!e.generatingSubtitles(),15000);QVERIFY(e.canImportGeneratedSubtitles());QVERIFY(!e.importGeneratedSubtitles(false));QVERIFY(e.importGeneratedSubtitles(true));QCOMPARE(e.project().captions.size(),2);e.undo();QCOMPARE(e.project().captions.size(),1);
    }
    void staleImportNotificationObservesTheNewProjectRevision(){
        QTemporaryDir dir;put(dir.filePath("ggml-large-v3.bin"),"model");SubtitleModels models(nullptr,dir.path(),nullptr,catalog());QTRY_VERIFY(models.ready());EditorController e(nullptr,Performance::budget(),&models);QVERIFY(e.importFiles({footage()}));QVERIFY(e.generateSrtTo(dir.filePath("captions.srt")));QTRY_VERIFY_WITH_TIMEOUT(!e.generatingSubtitles(),15000);QVERIFY(e.canImportGeneratedSubtitles());
        std::optional<bool> offered;connect(&e,&EditorController::subtitleChanged,&e,[&]{offered=e.canImportGeneratedSubtitles();});e.addCaption();QVERIFY(offered.has_value());QVERIFY(!*offered);
    }
    void keyboardModelChoiceAndSaveCancelWorkWithMacTabPolicy(){
        QTemporaryDir dir;put(dir.filePath("ggml-large-v3.bin"),"model");put(dir.filePath("ggml-large-v3-turbo.bin"),"model");auto specs=catalog();auto turbo=specs[0];turbo.id="large-v3-turbo";turbo.label="Turbo";specs.append(turbo);
        SubtitleModels models(nullptr,dir.path(),nullptr,specs);QTRY_VERIFY(models.ready());EditorController e(nullptr,Performance::budget(),&models);QVERIFY(e.importFiles({footage()}));QVERIFY(e.saveTo(dir.filePath("saved.isaac.json")));
        QQmlApplicationEngine engine;setEditorQmlContext(engine,e);QSignalSpy warnings(&engine,&QQmlEngine::warnings);
        const auto url=QUrl::fromLocalFile(QDir::current().filePath("qml")).toString();
        engine.loadData(QString("import QtQuick\nimport QtQuick.Controls\nimport \"%1\" as UI\nApplicationWindow { visible: true; width: 440; height: 850; UI.SubtitleGenerator { anchors.fill: parent; controller: editor } }").arg(url).toUtf8());QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *selector=window->findChild<QQuickItem*>("subtitleModelSelector"),*cpu=window->findChild<QQuickItem*>("subtitleCpuCheckBox"),*generate=window->findChild<QQuickItem*>("generateSrtButton");QVERIFY(selector&&cpu&&generate);
        const auto before=qGuiApp->styleHints()->tabFocusBehavior();qGuiApp->styleHints()->setTabFocusBehavior(Qt::TabFocusTextControls);const auto restore=qScopeGuard([&]{qGuiApp->styleHints()->setTabFocusBehavior(before);});
        models.setSelectedModel("large-v3");QTRY_VERIFY(models.ready());selector->forceActiveFocus();QTest::keyClick(window,Qt::Key_Down);QTRY_COMPARE(models.selectedModel(),QString("large-v3-turbo"));QTRY_VERIFY(models.ready());
        selector->forceActiveFocus();QTest::keyClick(window,Qt::Key_Tab);QTRY_VERIFY(cpu->hasActiveFocus());QTest::keyClick(window,Qt::Key_Tab);QTRY_VERIFY(generate->hasActiveFocus());
        bool canceled=false;QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);const auto dialogs=qScopeGuard([]{QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,false);});
        QTimer cancelDialog;cancelDialog.setInterval(10);connect(&cancelDialog,&QTimer::timeout,&e,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QFileDialog*>(widget);dialog&&dialog->isVisible()){canceled=true;cancelDialog.stop();dialog->reject();}});cancelDialog.start();
        QTest::keyClick(window,Qt::Key_Space);QVERIFY(canceled);QVERIFY(!e.generatingSubtitles());QVERIFY(!e.dirty());QCOMPARE(warnings.count(),0);
    }
    void realInstalledEngineGeneration(){
        if(qEnvironmentVariableIntValue("ISAAC_RUN_SUBTITLE_ACCEPTANCE")!=1)QSKIP("Opt-in real model acceptance; normal tests use deterministic workers.");
        const auto folder=qEnvironmentVariable("ISAAC_SUBTITLE_MODEL_DIR"),source=qEnvironmentVariable("ISAAC_SUBTITLE_SOURCE"),output=qEnvironmentVariable("ISAAC_SUBTITLE_OUTPUT");QVERIFY(!folder.isEmpty()&&!source.isEmpty()&&!output.isEmpty());QVERIFY(QDir().mkpath(output));
        SubtitleModels models(nullptr,folder);QTRY_VERIFY_WITH_TIMEOUT(models.ready(),60000);
        EditorController e(nullptr,Performance::budget(),&models);e.setSubtitleCpuOnly(qEnvironmentVariableIntValue("ISAAC_SUBTITLE_TEST_CPU")==1);QVERIFY(e.importFiles({source}));QVERIFY(e.saveTo(QDir(output).filePath("captured.isaac.json")));
        QString previous;connect(&e,&EditorController::subtitleChanged,&e,[&]{if(e.subtitleState()!=previous){previous=e.subtitleState();qInfo().noquote()<<e.subtitleModel()<<previous<<e.subtitleDevice();}});
        for(const auto &id:{QString("large-v3"),QString("large-v3-turbo")}){
            models.setSelectedModel(id);QTRY_VERIFY_WITH_TIMEOUT(models.ready(),60000);QElapsedTimer timer;timer.start();
            QVERIFY(e.generateSrtTo(QDir(output).filePath(id+".srt")));QTRY_VERIFY_WITH_TIMEOUT(!e.generatingSubtitles(),300000);
            QVERIFY2(e.subtitleState()=="Completed",qPrintable(e.notice()));qInfo()<<id<<"elapsed seconds"<<timer.elapsed()/1000.0<<"device"<<e.subtitleDevice();QVERIFY(!e.dirty());QVERIFY(e.project().captions.isEmpty());
        }
        QVERIFY(e.importGeneratedSubtitles(false));QVERIFY(!e.project().captions.isEmpty());e.undo();QVERIFY(e.project().captions.isEmpty());QVERIFY(!e.dirty());
    }
};
int main(int argc,char **argv){
    qputenv("QT_QUICK_BACKEND","software");
    QApplication app(argc,argv);app.setOrganizationName("IsaacSubtitleTests");app.setApplicationName("Controller");
    if(app.arguments().contains("-m")){
        QFile out(app.arguments().value(app.arguments().indexOf("-of")+1)+".json");if(!out.open(QIODevice::WriteOnly))return 1;
        out.write(R"({"transcription":[{"offsets":{"from":0,"to":1000},"text":"Hello 中文"}]})");return 0;
    }
    SubtitleControllerTest test;return QTest::qExec(&test,argc,argv);
}
#include "subtitle_controller_test.moc"
