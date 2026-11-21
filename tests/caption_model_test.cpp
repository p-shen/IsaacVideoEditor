#include "caption_model.h"
#include <QSignalSpy>
#include <QtTest>

class CaptionModelTest : public QObject {
    Q_OBJECT
private slots:
    void contentEditUpdatesOnlyItsRow() {
        Project p;p.clips={{"synthetic.mp4",0,60}};
        for(int i=0;i<1000;++i){TextOverlay t;t.startFrame=i*3;t.durationFrames=3;p.captions.append(t);}
        CaptionModel model;model.synchronize(p);
        QSignalSpy reset(&model,&QAbstractItemModel::modelReset),changed(&model,&QAbstractItemModel::dataChanged);
        const auto untouched=model.records()[1];p.captions[500].text="Changed";model.updateCaption(p,500);
        QCOMPARE(reset.size(),0);QCOMPARE(changed.size(),1);
        QCOMPARE(qvariant_cast<QModelIndex>(changed[0][0]).row(),500);
        QCOMPARE(qvariant_cast<QModelIndex>(changed[0][1]).row(),500);
        QCOMPARE(model.records()[500].toMap()["text"].toString(),QString("Changed"));QCOMPARE(model.records()[1],untouched);
        model.synchronize(p);QCOMPARE(changed.size(),1);QCOMPARE(reset.size(),0);
    }
    void timingWarningsRefreshAndUndoRestoresRecords() {
        Project p;p.clips={{"synthetic.mp4",0,2}};
        TextOverlay a;a.startFrame=0;a.durationFrames=60;TextOverlay b;b.startFrame=60;b.durationFrames=60;p.captions={a,b};
        CaptionModel model;model.synchronize(p);const auto before=model.records();QVERIFY(model.warnings().isEmpty());
        p.captions[1].startFrame=30;p.captions[1].durationFrames=180;p.captions[1].timingNeedsReview=true;model.updateCaption(p,1);
        QCOMPARE(model.warnings().size(),3);QVERIFY(model.timingNeedsReview());
        QVERIFY(model.records()[0].toMap()["overlap"].toBool());QVERIFY(model.records()[1].toMap()["clamped"].toBool());
        p.captions={a,b};model.synchronize(p);QCOMPARE(model.records(),before);QVERIFY(model.warnings().isEmpty());
        p.captions.clear();model.synchronize(p);QCOMPARE(model.rowCount(),0);QVERIFY(!model.timingNeedsReview());
    }
};
QTEST_GUILESS_MAIN(CaptionModelTest)
#include "caption_model_test.moc"
