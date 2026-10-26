#include "captions.h"
#include "project.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class CaptionTest : public QObject {
    Q_OBJECT
private slots:
    void srtPreservesUtf8MultilineAndOverlaps() {
        QString error;
        const auto cues=Subtitles::parseSrt(QByteArray::fromStdString(u8"\xEF\xBB\xBF" u8"1\r\n00:00:00,017 --> 00:00:02,000\r\n冒険の日\r\nIsaac & Daddy\r\n\r\n2\r\n00:00:01,500 --> 00:00:03,000\r\nمرحبا بالعالم\r\n"),&error);
        QVERIFY2(cues.has_value(),qPrintable(error));QCOMPARE(cues->size(),2);
        QCOMPARE(cues->at(0).startFrame,1);QCOMPARE(cues->at(0).durationFrames,119);
        QCOMPARE(cues->at(0).text,QString::fromUtf8("冒険の日\nIsaac & Daddy"));
        QVERIFY(cues->at(1).startFrame<cues->at(0).startFrame+cues->at(0).durationFrames);
        QVERIFY(cues->at(0).hostId.isEmpty());
    }
    void emptyCaptionTextIsRetainedBySrtRoundTrip() {
        Project p;TextOverlay cue;cue.text="";cue.durationFrames=1;p.captions={cue};QString error;
        const auto parsed=Subtitles::parseSrt(Subtitles::writeSrt(p),&error);QVERIFY2(parsed.has_value(),qPrintable(error));QCOMPARE(parsed->size(),1);QVERIFY(parsed->first().text.isEmpty());
    }
    void commonSrtMarkupBecomesPlainEditableWordsAndLiteralsRoundTrip() {
        const auto input=QString::fromUtf8("1\n00:00:00,000 --> 00:00:01,000\n<i>Isaac &amp; Daddy</i>\n<font color=\"red\">冒険</font><br>done &#x1F600;\n").toUtf8();QString error;const auto parsed=Subtitles::parseSrt(input,&error);QVERIFY2(parsed.has_value(),qPrintable(error));QCOMPARE(parsed->first().text,QString::fromUtf8("Isaac & Daddy\n冒険\ndone 😀"));
        Project p;TextOverlay t;t.text="<b>literal</b> & 2 < 3";p.captions={t};const auto again=Subtitles::parseSrt(Subtitles::writeSrt(p),&error);QVERIFY(again);QCOMPARE(again->first().text,t.text);
    }
    void invalidSrtIsAtomic() {
        for(const auto input:{QByteArray("1\n00:00:01,000 --> 00:00:00,000\nreversed\n"),QByteArray("1\n00:60:00,000 --> 00:60:01,000\nbad minute\n"),QByteArray("1\n00:00:00,000 --> 00:00:01,000\nvalid\n\n2\nbroken\ntext\n"),QByteArray("\xff")}){
            QString error;QVERIFY(!Subtitles::parseSrt(input,&error));QVERIFY(!error.isEmpty());
        }
    }
    void everyFrameSurvivesSrtRounding() {
        Project p;
        for(int frame=0;frame<=3600;++frame){TextOverlay t;t.text=QString::number(frame);t.startFrame=frame;t.durationFrames=1;p.captions.append(t);}
        QString error;const auto loaded=Subtitles::parseSrt(Subtitles::writeSrt(p),&error);
        QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(loaded->size(),p.captions.size());
        for(int i=0;i<loaded->size();++i){QCOMPARE(loaded->at(i).startFrame,i);QCOMPARE(loaded->at(i).durationFrames,1);}
    }
    void persistenceKeepsCaptionsDistinctAndLegacyLoadable() {
        QTemporaryDir dir;Project p;TextOverlay title;title.text="TITLE";p.textOverlays={title};TextOverlay cue;cue.text="CAPTION";cue.timingNeedsReview=true;p.captions={cue};p.burnCaptions=false;
        QString error;const auto path=dir.filePath("caption.isaac.json");QVERIFY2(saveProject(p,path,&error),qPrintable(error));const auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));
        QCOMPARE(loaded->textOverlays,p.textOverlays);QCOMPARE(loaded->captions,p.captions);QVERIFY(!loaded->burnCaptions);
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));auto data=QJsonDocument::fromJson(file.readAll()).object();file.close();QCOMPARE(data["version"].toInt(),7);
        data["version"]=6;data.remove("captions");data.remove("burnCaptions");QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(data).toJson());file.close();
        const auto legacy=loadProject(path,&error);QVERIFY(legacy);QVERIFY(legacy->captions.isEmpty());QCOMPARE(legacy->textOverlays,p.textOverlays);
    }
    void textAttachmentsFollowHostEditsAndKeepDesiredDuration() {
        Project p;p.clips={{"a.mp4",10,14},{"b.mp4",20,24}};
        TextOverlay title;title.startFrame=150;title.durationFrames=180;p.textOverlays={title};p.captions={title};
        QVERIFY(p.attachText(0,false,0));QVERIFY(p.attachText(0,true,0));QCOMPARE(p.captions[0].hostSourceFrame,750);
        QVERIFY(p.splitClip(0,120));QCOMPARE(p.captions[0].hostId,p.clips[1].id);QCOMPARE(p.textOverlays[0].hostId,p.clips[1].id);QCOMPARE(p.textStartFrame(0,true),150);
        QVERIFY(p.reorderClips({2,0,1}));QCOMPARE(p.textStartFrame(0,true),390);QCOMPARE(p.textFrames(0,true),90);QCOMPARE(p.captions[0].durationFrames,180);
        p.clips[2].inSeconds=13;p.normalizeVisuals();QCOMPARE(p.captions[0].hostSourceFrame,780);QCOMPARE(p.textStartFrame(0,true),360);
        const int current=p.textStartFrame(0,true);QVERIFY(p.detachText(0,true));QCOMPARE(p.captions[0].startFrame,current);QVERIFY(p.captions[0].hostId.isEmpty());
        QVERIFY(p.removeClip(2));QVERIFY(p.textOverlays.isEmpty());QCOMPARE(p.captions.size(),1);
    }
    void burnOptionNeverRemovesTitles() {
        Project p;p.clips={{"a.mp4",0,4}};TextOverlay title;title.text="TITLE";TextOverlay cue;cue.text="CAPTION";p.textOverlays={title};p.captions={cue};
        QCOMPARE(p.renderedTexts().size(),2);p.burnCaptions=false;QCOMPARE(p.renderedTexts().size(),1);QCOMPARE(p.renderedTexts()[0].text,QString("TITLE"));
    }
};
QTEST_MAIN(CaptionTest)
#include "caption_test.moc"
