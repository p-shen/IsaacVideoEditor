#include "recovery_store.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class RecoveryStoreTest : public QObject {
    Q_OBJECT
    Project sample() const { Project p;p.clips.append({"/original/clip.mp4",0,2});return p; }
    Project largeSample() const {
        auto p=sample();TextOverlay title;title.text=QString(2000,QLatin1Char('x'));p.textOverlays.fill(title,10000);return p;
    }
    QJsonObject snapshot(const QString &path) const {
        QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
private slots:
    void timerSaveLetsQueuedEditsRunBeforeCompletion() {
        QTemporaryDir dir;RecoveryStore store(dir.path());auto project=largeSample();
        bool queued=false,responsive=false,publishedEarlier=false;int saves=0;
        connect(&store,&RecoveryStore::statusChanged,this,[&]{
            if(store.status()!="Saving recovery…")return;
            ++saves;
            if(queued){publishedEarlier=QFile::exists(store.snapshotPath());return;}
            queued=true;QTimer::singleShot(0,this,[&]{
                responsive=store.status()=="Saving recovery…"&&!QFile::exists(store.snapshotPath());
                project.musicDb=-11;store.schedule(project,"/original/intermediate.isaac.json");
                project.musicDb=-9;store.schedule(project,"/original/intermediate.isaac.json");
                project.musicDb=-7;store.schedule(project,"/original/latest.isaac.json");
            });
        });
        store.schedule(project,"/original/first.isaac.json");
        QTRY_VERIFY_WITH_TIMEOUT(queued,3000);
        QTRY_VERIFY_WITH_TIMEOUT(store.status()=="Recovery saved",5000);
        QVERIFY2(responsive,"The timer save blocked queued editor work until recovery finished");
        QVERIFY2(publishedEarlier,"New edits discarded the active snapshot instead of retaining recovery during continuous editing");
        QCOMPARE(saves,2);
        const auto saved=loadProject(store.snapshotPath());QVERIFY(saved);QCOMPARE(saved->musicDb,-7);
        QCOMPARE(snapshot(store.snapshotPath())["_recovery"].toObject()["projectPath"].toString(),QString("/original/latest.isaac.json"));
        QVERIFY(RecoveryStore::candidates(dir.path()).isEmpty());
    }
    void clearDuringTimerSaveCannotRestoreSnapshot() {
        QTemporaryDir dir;RecoveryStore store(dir.path());bool cleared=false;
        connect(&store,&RecoveryStore::statusChanged,this,[&]{
            if(store.status()!="Saving recovery…"||cleared)return;
            QTimer::singleShot(0,this,[&]{store.clear();cleared=true;});
        });
        store.schedule(largeSample(),{});QTRY_VERIFY_WITH_TIMEOUT(cleared,3000);
        QVERIFY(store.flush()); // Drain the old worker before checking its publication.
        QCoreApplication::processEvents();
        QVERIFY(!QFile::exists(store.snapshotPath()));QCOMPARE(store.status(),QString("Recovery ready"));
    }
    void explicitFlushSupersedesAnOlderTimerSave() {
        QTemporaryDir dir;RecoveryStore store(dir.path());bool queued=false,replaced=false;
        connect(&store,&RecoveryStore::statusChanged,this,[&]{
            if(store.status()!="Saving recovery…"||queued)return;
            queued=true;QTimer::singleShot(0,this,[&]{
                store.clear();auto latest=sample();latest.musicDb=-3;
                store.schedule(latest,"/original/saved.isaac.json");QVERIFY(store.flush());
                replaced=true;
            });
        });
        store.schedule(largeSample(),{});QTRY_VERIFY_WITH_TIMEOUT(replaced,3000);
        QTRY_VERIFY_WITH_TIMEOUT(store.status()=="Recovery saved",3000);QCoreApplication::processEvents();
        const auto saved=loadProject(store.snapshotPath());QVERIFY(saved);QCOMPARE(saved->musicDb,-3);
        QCOMPARE(snapshot(store.snapshotPath())["_recovery"].toObject()["projectPath"].toString(),QString("/original/saved.isaac.json"));
    }
    void timerFailureRetainsLatestRevisionForRetry() {
        QTemporaryDir dir;QFile blocker(dir.filePath("blocked"));QVERIFY(blocker.open(QIODevice::WriteOnly));blocker.close();
        RecoveryStore store(blocker.fileName()+"/inside");auto project=sample();project.musicDb=-4;
        store.schedule(project,{});QTRY_VERIFY_WITH_TIMEOUT(store.status().contains("failed",Qt::CaseInsensitive),3000);
        QVERIFY(QFile::remove(blocker.fileName()));QVERIFY(store.flush());
        const auto saved=loadProject(store.snapshotPath());QVERIFY(saved);QCOMPARE(saved->musicDb,-4);
    }
    void destructionFinishesActiveWriteBeforeReleasingItsLock() {
        QTemporaryDir dir;auto store=std::make_unique<RecoveryStore>(dir.path());bool closing=false,closed=false;
        const auto path=store->snapshotPath();
        connect(store.get(),&RecoveryStore::statusChanged,this,[&]{
            if(store->status()!="Saving recovery…"||closing)return;
            closing=true;QTimer::singleShot(0,this,[&]{store.reset();closed=true;});
        });
        store->schedule(largeSample(),"/original/closing.isaac.json");QTRY_VERIFY_WITH_TIMEOUT(closed,3000);
        QCoreApplication::processEvents();QVERIFY(loadProject(path));
        QCOMPARE(RecoveryStore::candidates(dir.path()).size(),1);
    }
};
QTEST_GUILESS_MAIN(RecoveryStoreTest)
#include "recovery_store_test.moc"
