#include "project.h"
#include "export_job.h"
#include <QProcess>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtTest>

class PerformanceReportTest final : public QObject {
    Q_OBJECT
private slots:
    void exportsThroughTheSupervisedSoftwareCommand() {
        QTemporaryDir dir;Project project;project.clips={{QFINDTESTDATA("fixtures/seek.mp4"),0,.2}};
        const auto input=dir.filePath("input.isaac.json"),output=dir.filePath("output.mp4");QVERIFY(saveProject(project,input));
        QProcess process;process.start(exportWorkerExecutable(),{"--benchmark-export",input,output,"--software"});
        if(!process.waitForFinished(20000)){process.kill();process.waitForFinished();QFAIL("Benchmark export did not finish");}
        QCOMPARE(process.exitStatus(),QProcess::NormalExit);QVERIFY2(process.exitCode()==0,process.readAllStandardError().constData());
        QVERIFY(QFileInfo(output).size()>0);const auto messages=process.readAllStandardOutput();
        QVERIFY(messages.contains("Export metrics: "));QVERIFY(messages.contains("libx264"));
    }
    void reportsRealColdAndWarmSeeks() {
        QTemporaryDir dir;Project project;project.clips={{QFINDTESTDATA("fixtures/seek.mp4"),0,2}};
        const auto input=dir.filePath("input.isaac.json"),output=dir.filePath("report.json");QVERIFY(saveProject(project,input));
        QProcess process;auto environment=QProcessEnvironment::systemEnvironment();environment.insert("QT_QPA_PLATFORM","offscreen");environment.remove("SDL_AUDIODRIVER");process.setProcessEnvironment(environment);
        process.start(exportWorkerExecutable(),{"--performance-report",input,output});
        if(!process.waitForFinished(20000)){process.kill();process.waitForFinished();QFAIL("Performance report command did not finish");}
        QCOMPARE(process.exitStatus(),QProcess::NormalExit);QVERIFY2(process.exitCode()==0,process.readAllStandardError().constData());
        QFile file(output);QVERIFY(file.open(QIODevice::ReadOnly));const auto report=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(report["schemaVersion"].toInt(),1);QVERIFY(report["seeks"].toArray().size()>=10);
        const auto preview=report["editing"].toObject()["preview"].toObject();
        QVERIFY(preview["cacheHits"].toDouble()>0);QVERIFY(preview["seekCount"].toDouble()>=10);
        QVERIFY(report["process"].toObject()["peakResidentBytes"].toDouble()>0);
        const auto playback=report["playback"].toObject();QVERIFY(playback["started"].toBool());
        QVERIFY(playback["receivedFrames"].toDouble()>0);QVERIFY(playback["errors"].toArray().isEmpty());
    }
    void reportsPlaybackStartupFailureExplicitly() {
        QTemporaryDir dir;Project project;project.clips={{QFINDTESTDATA("fixtures/seek.mp4"),0,.2}};
        const auto input=dir.filePath("input.isaac.json"),output=dir.filePath("report.json");QVERIFY(saveProject(project,input));
        QProcess process;auto environment=QProcessEnvironment::systemEnvironment();environment.insert("SDL_AUDIODRIVER","isaac_missing_audio_driver");process.setProcessEnvironment(environment);
        process.start(exportWorkerExecutable(),{"--performance-report",input,output});QVERIFY(process.waitForFinished(20000));QCOMPARE(process.exitCode(),0);
        QFile file(output);QVERIFY(file.open(QIODevice::ReadOnly));const auto report=QJsonDocument::fromJson(file.readAll()).object();
        const auto playback=report["playback"].toObject();QVERIFY(!playback["started"].toBool());QVERIFY(!playback["errors"].toArray().isEmpty());
    }
};
QTEST_GUILESS_MAIN(PerformanceReportTest)
#include "performance_report_test.moc"
