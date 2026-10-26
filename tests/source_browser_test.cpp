#include "editor_controller.h"
#include "mlt_engine.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

class SourceBrowserTest: public QObject {
    Q_OBJECT
    QString fixture() const { return QDir::current().filePath("tests/fixtures/seek.mp4"); }
private slots:
    void initTestCase() { qputenv("SDL_AUDIODRIVER","dummy"); Media::initializeMlt(); }
    void importOnlyAndIndependentInstances() {
        EditorController e;
        bool imported=false;
        QVERIFY(QMetaObject::invokeMethod(&e,"importMedia",Q_RETURN_ARG(bool,imported),Q_ARG(QStringList,QStringList{fixture()})));
        QVERIFY(imported); QCOMPARE(e.duration(),0); QCOMPARE(e.property("assets").toList().size(),1);
        e.selectSource(0); e.seekSource(12); e.markSourceIn(); e.seekSource(29); e.markSourceOut();
        QCOMPARE(e.currentSource()["rangeFrames"].toInt(),18); QVERIFY(e.appendSource());
        QCOMPARE(e.duration(),18); QCOMPARE(e.project().clips[0].inSeconds,.2);
        e.seekSource(60); e.markSourceIn(); e.seekSource(89); e.markSourceOut(); QVERIFY(e.appendSource());
        QCOMPARE(e.duration(),48); QCOMPARE(e.project().clips[0].outSeconds,.5); QCOMPARE(e.project().clips[1].inSeconds,1.0);
        QCOMPARE(e.assets()[0].toMap()["uses"].toInt(),2);
        e.selectClip(0); e.removeSelection(); QCOMPARE(e.assets().size(),1);
    }
    void rangeCursorRoundtripAndUndo() {
        EditorController e; QVERIFY(e.importMedia({fixture()})); e.selectSource(0);
        e.seekSource(30); e.markSourceIn(); e.seekSource(47); e.markSourceOut();
        e.seekSource(40); QTemporaryDir dir; QVERIFY(e.saveTo(dir.filePath("library.json")));
        EditorController restored; QVERIFY(restored.loadFrom(dir.filePath("library.json"))); restored.selectSource(0);
        QCOMPARE(restored.sourcePosition(),40); QCOMPARE(restored.currentSource()["inFrame"].toInt(),30);
        QCOMPARE(restored.currentSource()["outFrame"].toInt(),48);
        e.clearSourceRange(); QCOMPARE(e.currentSource()["rangeFrames"].toInt(),120);
        e.undo(); QCOMPARE(e.currentSource()["rangeFrames"].toInt(),18);
        e.seekSource(0); e.markSourceOut(); QVERIFY(!e.currentSource()["validRange"].toBool()); QVERIFY(!e.appendSource());
    }
    void insertionUsesCommittedPositionAndCrossfadeRestrictions() {
        EditorController e; QVERIFY(e.importFiles({fixture(),fixture()})); e.seek(30);
        e.selectSource(0); e.seekSource(60); e.markSourceIn(); e.seekSource(89); e.markSourceOut();
        e.skim(90); QVERIFY(e.insertSource()); QCOMPARE(e.project().clips.size(),4); QCOMPARE(e.position(),30);
        QCOMPARE(e.project().clipFrames(e.project().clips[0]),30); QCOMPARE(e.project().clips[1].inSeconds,1.0);
        QCOMPARE(e.duration(),270); e.undo(); QCOMPARE(e.duration(),240);
        e.showProject(); e.setCrossfade(.5); e.seek(100); e.selectSource(0);
        const int before=e.duration(); QVERIFY(!e.insertSource()); QCOMPARE(e.duration(),before);
        QVERIFY(e.notice().contains("crossfade",Qt::CaseInsensitive));
        QVERIFY(!e.insertSourceAt(-1)); QVERIFY(!e.insertSourceAt(before+1));
    }
    void sourceScrubCancellationKeepsCommittedLibraryState() {
        EditorController e;QVERIFY(e.importMedia({fixture()}));e.seekSource(12);QTemporaryDir dir;QVERIFY(e.saveTo(dir.filePath("cursor.json")));
        QVERIFY(QMetaObject::invokeMethod(&e,"beginSourceScrub"));e.seekSource(45);QCOMPARE(e.sourcePosition(),45);
        QCOMPARE(e.project().assets[0].cursorFrame,12);QVERIFY(!e.dirty());
        QVERIFY(QMetaObject::invokeMethod(&e,"finishSourceScrub",Q_ARG(bool,true)));QCOMPARE(e.sourcePosition(),12);QVERIFY(!e.dirty());
        QVERIFY(QMetaObject::invokeMethod(&e,"beginSourceScrub"));e.seekSource(30);
        QVERIFY(QMetaObject::invokeMethod(&e,"finishSourceScrub",Q_ARG(bool,false)));QCOMPARE(e.project().assets[0].cursorFrame,30);QVERIFY(e.dirty());
    }
    void sourcePreviewAndPlaybackKeepProjectCursor() {
        EditorController e;QVERIFY(e.importFiles({fixture()}));e.seek(37);e.selectSource(0);e.seekSource(90);
        QTRY_VERIFY_WITH_TIMEOUT(!e.frame().isNull(),2000);QCOMPARE(e.position(),37);QCOMPARE(e.sourcePosition(),90);
        e.togglePlayback();QTRY_VERIFY_WITH_TIMEOUT(e.sourcePosition()>90,2000);e.togglePlayback();QCOMPARE(e.position(),37);
        const int source=e.sourcePosition();e.showProject();QCOMPARE(e.position(),37);e.selectSource(0);QCOMPARE(e.sourcePosition(),source);
        e.seekSource(0);QTRY_VERIFY_WITH_TIMEOUT(e.frame().pixelColor(e.frame().width()/2,e.frame().height()/2).red()>180,2000);
    }
    void audioSourcesAndMalformedLibrary() {
        QTemporaryDir dir;QProcess ffmpeg;const auto audio=dir.filePath("tone.wav");
        ffmpeg.start("/opt/homebrew/bin/ffmpeg",{"-v","error","-f","lavfi","-i","sine=frequency=440:duration=0.2","-y",audio});
        QVERIFY(ffmpeg.waitForFinished());QCOMPARE(ffmpeg.exitCode(),0);
        const auto disguised=dir.filePath("audio-only.mp4");QVERIFY(QFile::copy(audio,disguised));
        EditorController typeCheck;QVERIFY(typeCheck.importMedia({disguised}));QCOMPARE(typeCheck.assets()[0].toMap()["kind"].toString(),QString("audio"));QVERIFY(!typeCheck.importFiles({disguised}));
        EditorController e;QVERIFY(e.importMedia({audio,fixture()}));QCOMPARE(e.duration(),0);QCOMPARE(e.assets().size(),2);
        QCOMPARE(e.filteredAssets("","audio","unused").size(),1);e.selectSource(0);QVERIFY(!e.appendSource());
        e.setVoicePath(audio);QCOMPARE(e.filteredAssets("","audio","used").size(),1);
        const auto path=dir.filePath("media.json");QVERIFY(e.saveTo(path));QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));
        auto document=QJsonDocument::fromJson(file.readAll()).object();file.close();auto list=document["assets"].toArray();
        auto asset=list[0].toObject();asset["cursorFrame"]=10000000;list[0]=asset;document["assets"]=list;
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(document).toJson());file.close();
        QString error;QVERIFY(!loadProject(path,&error));QVERIFY(!error.isEmpty());
    }
    void libraryRemovalPersistsAndDropIsAtomic() {
        EditorController e;QVERIFY(e.importFiles({fixture(),fixture()}));e.setCrossfade(.5);
        QVERIFY(!e.dropMediaAt({fixture()},100));QCOMPARE(e.project().clips.size(),2);
        e.selectSource(0);QVERIFY(e.removeSource(0,true));QTemporaryDir dir;const auto path=dir.filePath("removed.json");QVERIFY(e.saveTo(path));
        EditorController loaded;QVERIFY(loaded.loadFrom(path));QCOMPARE(loaded.assets().size(),0);QCOMPARE(loaded.project().clips.size(),2);
        QVERIFY(loaded.dropMediaAt({fixture()},0));QCOMPARE(loaded.assets().size(),1);QCOMPARE(loaded.project().clips.size(),3);
        loaded.undo();QCOMPARE(loaded.assets().size(),0);QCOMPARE(loaded.project().clips.size(),2);
    }
    void legacySourceBrowserFindsCompleteOriginalWithoutChangingEdit() {
        QTemporaryDir dir;QFile file(dir.filePath("legacy.json"));QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"version",4},{"clips",QJsonArray{QJsonObject{{"path",fixture()},{"in",.5},{"out",1.0}}}}}).toJson());file.close();
        EditorController e;QVERIFY(e.loadFrom(file.fileName()));e.selectSource(0);
        QCOMPARE(e.currentSource()["durationFrames"].toInt(),120);QCOMPARE(e.currentSource()["outFrame"].toInt(),120);QCOMPARE(e.duration(),30);QVERIFY(!e.dirty());
    }
    void legacyAssetsAndFiltersRemoval() {
        QTemporaryDir dir; QFile file(dir.filePath("old.json")); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"version\":4,\"clips\":[{\"path\":\"old.mp4\",\"in\":1,\"out\":3,\"rotationDegrees\":90}],\"voicePath\":\"voice.wav\"}"); file.close();
        auto p=loadProject(file.fileName()); QVERIFY(p); QCOMPARE(p->assets.size(),2); QCOMPARE(p->durationFrames(),120); QCOMPARE(p->clips[0].rotationDegrees,90);
        EditorController e; QVERIFY(e.importFiles({fixture()}));
        QCOMPARE(e.filteredAssets("SEEK","video","used").size(),1); QCOMPARE(e.filteredAssets("seek","audio","all").size(),0);
        QCOMPARE(e.filteredAssets("","all","unused").size(),0);
        QVERIFY(!e.removeSource(0)); QCOMPARE(e.assets().size(),1); QVERIFY(e.removeSource(0,true));
        QCOMPARE(e.assets().size(),0); QCOMPARE(e.clips().size(),1); QVERIFY(QFile::exists(fixture())); e.undo(); QCOMPARE(e.assets().size(),1);
    }
};
QTEST_MAIN(SourceBrowserTest)
#include "source_browser_test.moc"
