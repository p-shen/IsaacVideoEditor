#include "captions.h"
#include "project.h"
#include "text_style.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class CaptionTest : public QObject {
    Q_OBJECT
private slots:
    void selectedTextStylesPreserveChineseEmojiAndCorrections() {
        TextOverlay t;t.text=QString::fromUtf8("今天 amazing 👨‍👩‍👧‍👦");t.bold=false;QString error;
        QVERIFY(setTextRangeFields(t,3,10,{{"color","#ff00ff"},{"bold",true}},&error));
        QCOMPARE(t.spans.size(),1);QCOMPARE(t.spans[0].start,3);QCOMPARE(t.spans[0].length,7);
        QCOMPARE(textRangeFields(t,3,10)["color"].toString(),"#ff00ff");QVERIFY(textRangeFields(t,3,10)["bold"].toBool());
        const auto before=t;
        QVERIFY(!setTextRangeFields(t,11,12,{{"bold",true}},&error));QCOMPARE(t,before);
        QVERIFY(!setTextRangeFields(t,3,10,{{"fontSize",80}},&error));QCOMPARE(t,before);
        QVERIFY(!setTextRangeFields(t,3,10,{{"color","invalid"}},&error));QCOMPARE(t,before);
        replaceTextContent(t,QString::fromUtf8("今天 really amazing 👨‍👩‍👧‍👦"));
        QCOMPARE(t.spans.size(),1);QCOMPARE(t.spans[0].start,10);QCOMPARE(t.spans[0].length,7);
        replaceTextContent(t,QString::fromUtf8("今天 really awesome 👨‍👩‍👧‍👦"));
        QCOMPARE(textRangeFields(t,10,17)["color"].toString(),"#ff00ff");QVERIFY(textRangeFields(t,10,17)["bold"].toBool());
        QVERIFY(setTextRangeFields(t,12,17,{{"bold",false}},&error));QCOMPARE(t.spans.size(),2);
        QVERIFY(textRangeFields(t,10,12)["bold"].toBool());QVERIFY(!textRangeFields(t,12,17)["bold"].toBool());
        QVERIFY(validTextSpans(t));
        Project p;p.captions={t};const auto srt=Subtitles::writeSrt(p);const auto parsed=Subtitles::parseSrt(srt);
        QVERIFY(parsed);QCOMPARE(parsed->first().text,t.text);QVERIFY(parsed->first().spans.isEmpty());
    }
    void wordStylesPersistAndRejectMalformedRanges() {
        QTemporaryDir dir;Project p;TextOverlay t;t.text=QString::fromUtf8("Hi 中文 🔥");QString error;
        QVERIFY(setTextRangeFields(t,3,5,{{"color","#ff00ff"},{"bold",false}},&error));p.captions={t};
        const auto path=dir.filePath("styled.isaac.json");QVERIFY2(saveProject(p,path,&error),qPrintable(error));
        const auto loaded=loadProject(path,&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(loaded->captions,p.captions);
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));const auto original=QJsonDocument::fromJson(file.readAll()).object();file.close();QCOMPARE(original["version"].toInt(),9);
        const QJsonArray badRanges[]{QJsonArray{QJsonObject{{"start",3.5},{"length",1},{"bold",true}}},
            QJsonArray{QJsonObject{{"start",6},{"length",1},{"bold",true}}},
            QJsonArray{QJsonObject{{"start",3},{"length",2},{"bold","yes"}}},
            QJsonArray{QJsonObject{{"start",3},{"length",2},{"color","invalid"}}},
            QJsonArray{QJsonObject{{"start",3},{"length",2},{"bold",true}},QJsonObject{{"start",4},{"length",1},{"bold",false}}}};
        for(const auto &ranges:badRanges){auto data=original;auto cue=data["captions"].toArray()[0].toObject();cue["spans"]=ranges;data["captions"]=QJsonArray{cue};
            QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(data).toJson());file.close();QVERIFY(!loadProject(path,&error));}
        p.captions[0].spans.clear();QVERIFY(saveProject(p,path,&error));QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(QJsonDocument::fromJson(file.readAll())["version"].toInt(),8);
    }
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
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));auto data=QJsonDocument::fromJson(file.readAll()).object();file.close();QCOMPARE(data["version"].toInt(),8);
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
    void textTimingResolvesOffsetsClampsAndMissingHosts() {
        Project p;p.clips={{"a.mp4",10,14},{"b.mp4",20,23},{"c.mp4",30,32}};p.crossfadeFrames=30;p.clips[1].sourceFrameOffset=600;
        TextOverlay detached;detached.startFrame=900;detached.durationFrames=100;
        TextOverlay attached;attached.hostId=p.clips[1].id;attached.hostSourceFrame=1850;
        p.textOverlays={detached,attached};
        TextOverlay cue;cue.startFrame=40;cue.durationFrames=20;p.captions={cue};
        cue.hostId=p.clips[1].id;cue.hostSourceFrame=1700;cue.durationFrames=30;p.captions.append(cue);
        cue.hostSourceFrame=2000;cue.durationFrames=150;p.captions.append(cue);
        cue.hostId="missing";cue.startFrame=550;cue.durationFrames=70;p.captions.append(cue);
        const auto timing=p.textTiming();
        QCOMPARE(timing.titleStarts,QVector<int>({900,260}));QCOMPARE(timing.captionStarts,QVector<int>({40,210,389,550}));
        QCOMPARE(timing.duration,480);QCOMPARE(timing.extent,620);
        p.burnCaptions=false;QCOMPARE(p.textTiming().extent,620);
    }
    void textTimingRefreshesAfterTrimsTransitionsAndReordering() {
        Project p;p.clips={{"a.mp4",10,14},{"b.mp4",20,24}};p.clips[0].sourceFrameOffset=1200;
        TextOverlay text;text.hostId=p.clips[0].id;text.hostSourceFrame=1950;text.durationFrames=180;
        p.textOverlays={text};p.captions={text};
        const auto original=p.textTiming();QCOMPARE(original.titleStarts,QVector<int>({150}));QCOMPARE(original.captionStarts,QVector<int>({150}));QCOMPARE(original.duration,480);QCOMPARE(original.extent,480);
        p.clips[0].inSeconds=13;
        const auto trimmed=p.textTiming();QCOMPARE(trimmed.titleStarts,QVector<int>({0}));QCOMPARE(trimmed.captionStarts,QVector<int>({0}));QCOMPARE(trimmed.duration,300);
        p.crossfadeFrames=30;
        const auto transitioned=p.textTiming();QCOMPARE(transitioned.duration,270);QCOMPARE(transitioned.captionStarts,QVector<int>({0}));
        QVERIFY(p.reorderClips({1,0}));
        const auto reordered=p.textTiming();QCOMPARE(reordered.titleStarts,QVector<int>({210}));QCOMPARE(reordered.captionStarts,QVector<int>({210}));QCOMPARE(reordered.duration,270);QCOMPARE(reordered.extent,390);
        const auto rendered=p.renderedTexts();QCOMPARE(rendered.size(),2);QCOMPARE(rendered[0].durationFrames,60);QCOMPARE(rendered[1].durationFrames,60);
        QCOMPARE(p.textOverlays[0].durationFrames,180);QCOMPARE(p.captions[0].durationFrames,180);
    }
    void renderedTextsKeepOrderingAndDesiredDurations() {
        Project p;p.clips={{"a.mp4",0,4}};
        TextOverlay title;title.text="detached title";title.startFrame=300;title.durationFrames=100;p.textOverlays={title};
        title.text="attached title";title.hostId=p.clips[0].id;title.hostSourceFrame=200;p.textOverlays.append(title);
        title.text="missing title";title.hostId="missing";title.startFrame=230;title.durationFrames=80;p.textOverlays.append(title);
        TextOverlay cue;cue.text="detached caption";cue.startFrame=220;cue.durationFrames=90;p.captions={cue};
        cue.text="attached caption";cue.hostId=p.clips[0].id;cue.hostSourceFrame=180;cue.durationFrames=100;p.captions.append(cue);
        cue.text="missing caption";cue.hostId="missing";cue.startFrame=300;cue.durationFrames=40;p.captions.append(cue);
        const auto timing=p.textTiming();QCOMPARE(timing.duration,240);QCOMPARE(timing.extent,340);
        auto expected=p.textOverlays+p.captions;
        const QVector<int> starts{300,200,230,220,180,300},frames{100,40,10,20,60,0};
        for(int i=0;i<expected.size();++i){expected[i].startFrame=starts[i];expected[i].durationFrames=frames[i];}
        QCOMPARE(p.renderedTexts(),expected);QCOMPARE(p.captions[0].durationFrames,90);QCOMPARE(p.textOverlays[1].durationFrames,100);
        p.burnCaptions=false;expected.resize(3);QCOMPARE(p.renderedTexts(),expected);QCOMPARE(p.textTiming().extent,340);
    }
    void burnOptionNeverRemovesTitles() {
        Project p;p.clips={{"a.mp4",0,4}};TextOverlay title;title.text="TITLE";TextOverlay cue;cue.text="CAPTION";p.textOverlays={title};p.captions={cue};
        QCOMPARE(p.renderedTexts().size(),2);p.burnCaptions=false;QCOMPARE(p.renderedTexts().size(),1);QCOMPARE(p.renderedTexts()[0].text,QString("TITLE"));
    }
};
QTEST_MAIN(CaptionTest)
#include "caption_test.moc"
