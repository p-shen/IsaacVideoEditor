#include "editor_controller.h"
#include "captions.h"
#include "mlt_engine.h"
#include "preview.h"
#include "text_style.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class CaptionControllerTest : public QObject {
    Q_OBJECT
    QString footage() const {return QDir::current().filePath("tests/fixtures/seek.mp4");}
    QString srt(QTemporaryDir &dir,const QByteArray &text="1\n00:00:00,000 --> 00:00:01,000\nFIRST\n\n2\n00:00:00,500 --> 00:00:01,500\nSECOND\n") {
        auto path=dir.filePath("captions.srt");QFile f(path);if(!f.open(QIODevice::WriteOnly))return {};f.write(text);return path;
    }
private slots:
    void typingCoalescesPreviewAndCancelDropsPendingText() {
        QTemporaryDir dir;EditorController editor;QVERIFY(editor.importFiles({footage()}));editor.addCaption();
        auto *preview=editor.findChild<Preview*>();QVERIFY(preview);
        QTRY_VERIFY_WITH_TIMEOUT(!editor.loading()&&!preview->isSeeking()&&!editor.frame().isNull(),10000);
        QVERIFY(editor.saveTo(dir.filePath("saved.isaac.json")));
        editor.beginCaptionEdit(0);QTRY_VERIFY(!preview->isSeeking());
        QSignalSpy seeking(preview,&Preview::seekingChanged);
        for(int i=0;i<50;++i)editor.previewCaptionContent(QString("Typed %1").arg(i));
        QCOMPARE(seeking.size(),0);QCOMPARE(editor.captions()[0].toMap()["text"].toString(),QString("Typed 49"));
        editor.endTextDrag(true);QTRY_VERIFY_WITH_TIMEOUT(!preview->isSeeking(),5000);QTest::qWait(40);
        QCOMPARE(editor.project().captions[0].text,QString("New caption"));QCOMPARE(editor.captions()[0].toMap()["text"].toString(),QString("New caption"));QVERIFY(!editor.dirty());
        editor.beginCaptionEdit(0);editor.previewCaptionContent("Final typed state");editor.endTextDrag(false);
        QTRY_VERIFY_WITH_TIMEOUT(!preview->isSeeking(),5000);QCOMPARE(editor.project().captions[0].text,QString("Final typed state"));
        editor.undo();QCOMPARE(editor.project().captions[0].text,QString("New caption"));editor.redo();QCOMPARE(editor.project().captions[0].text,QString("Final typed state"));
    }
    void captionSummariesUseResolvedTimingAndKeepDesiredDuration() {
        Project project;project.clips={{"a.mp4",0,2},{"b.mp4",1,4}};project.crossfadeFrames=30;
        TextOverlay attached;attached.hostId=project.clips[1].id;attached.hostSourceFrame=120;attached.durationFrames=180;
        TextOverlay detached;detached.startFrame=300;detached.durationFrames=120;project.captions={attached,detached};
        const auto timing=project.textTiming();
        const auto a=timedTextInfo(project,0,true,&timing),b=timedTextInfo(project,1,true,&timing);
        QCOMPARE(a["startFrame"].toInt(),150);QCOMPARE(a["durationFrames"].toInt(),180);QCOMPARE(a["effectiveFrames"].toInt(),120);QVERIFY(a["clamped"].toBool());
        QCOMPARE(b["startFrame"].toInt(),300);QCOMPARE(b["durationFrames"].toInt(),120);QCOMPARE(b["effectiveFrames"].toInt(),0);QVERIFY(b["clamped"].toBool());
        QCOMPARE(a,timedTextInfo(project,0,true));QCOMPARE(b,timedTextInfo(project,1,true));
    }
    void selectedWordFormattingSharesContentGestureAndUndo() {
        QTemporaryDir dir;EditorController e;e.addCaption();e.beginCaptionEdit(0);e.previewCaptionContent(QString::fromUtf8("Hello 中文 🔥"));
        e.styleCaptionText(6,8,{{"color","#ff00ff"},{"bold",false}});
        QCOMPARE(e.project().captions[0].spans.size(),1);QVERIFY(e.dirty());
        QCOMPARE(e.captionTextStyle(6,8)["color"].toString(),"#ff00ff");QVERIFY(!e.captionTextStyle(6,8)["bold"].toBool());
        e.endTextDrag(true);QCOMPARE(e.project().captions[0].text,"New caption");QVERIFY(e.project().captions[0].spans.isEmpty());
        e.beginCaptionEdit(0);e.previewCaptionContent(QString::fromUtf8("Hello 中文 🔥"));e.styleCaptionText(6,8,{{"color","#ff00ff"},{"bold",false}});e.endTextDrag(false);
        const auto styled=e.project().captions[0];QVERIFY(e.saveTo(dir.filePath("styled.isaac.json")));
        e.undo();QCOMPARE(e.project().captions[0].text,"New caption");QVERIFY(e.project().captions[0].spans.isEmpty());e.redo();QCOMPARE(e.project().captions[0],styled);
        e.beginCaptionEdit(0);e.styleCaptionText(9,10,{{"bold",true}});QCOMPARE(e.project().captions[0],styled);e.endTextDrag(true);
        e.beginCaptionEdit(0);e.previewCaptionContent(QString::fromUtf8("Hi Hello 中文 🔥"));e.endTextDrag(false);QCOMPARE(e.project().captions[0].spans[0].start,9);
        QVERIFY(e.exportSrt(dir.filePath("plain.srt")));QFile file(dir.filePath("plain.srt"));QVERIFY(file.open(QIODevice::ReadOnly));const auto cues=Subtitles::parseSrt(file.readAll());QVERIFY(cues);QCOMPARE(cues->first().text,e.project().captions[0].text);QVERIFY(cues->first().spans.isEmpty());
    }
    void formattingAfterFocusedSaveRemainsCancelableAndOneUndo() {
        QTemporaryDir dir;EditorController e;e.addCaption();e.beginCaptionEdit(0);QVERIFY(e.saveTo(dir.filePath("focused.isaac.json")));const auto saved=e.project().captions;
        e.styleCaptionText(0,3,{{"bold",false}},true);e.endTextDrag(true);QCOMPARE(e.project().captions,saved);QVERIFY(!e.dirty());
        e.styleCaptionText(0,3,{{"color","#ff00ff"}},true);e.previewCaptionContent("New caption changed");e.endTextDrag(false);
        QVERIFY(!e.project().captions[0].spans.isEmpty());e.undo();QCOMPARE(e.project().captions,saved);QVERIFY(!e.dirty());
    }
    void presetsApplyToCaptionSelectionAndUndoTogether() {
        QTemporaryDir dir;EditorController e;QVERIFY(e.importFiles({footage()}));QVERIFY(e.importSrt(srt(dir)));
        e.selectCaption(0);e.selectCaption(1,Qt::ControlModifier);e.styleCaptions({{"preset","comic-sticker"}});
        QCOMPARE(e.project().captions[0].style,"comic-sticker");QCOMPARE(e.project().captions[1].style,"comic-sticker");
        QCOMPARE(e.currentCaption()["animation"].toString(),"bounce");
        e.styleCaptions({{"animation","none"}});QCOMPARE(e.currentCaption()["style"].toString(),"comic-sticker");
        e.undo();QCOMPARE(e.currentCaption()["animation"].toString(),"bounce");e.undo();QCOMPARE(e.project().captions[0].style,"clean");QCOMPARE(e.project().captions[1].style,"clean");
        QCOMPARE(e.project().captions[0].text,"FIRST");QCOMPARE(e.project().captions[1].startFrame,30);
    }
    void importIsAtomicAndKeepsTitlesDistinct() {
        QTemporaryDir dir;EditorController e;QVERIFY(e.importFiles({footage()}));e.addText();e.updateText(0,{{"text","TITLE"}});
        QVERIFY(e.importSrt(srt(dir)));QCOMPARE(e.project().textOverlays.size(),1);QCOMPARE(e.project().captions.size(),2);QCOMPARE(e.project().textOverlays[0].text,QString("TITLE"));
        const auto history=e.undoText();QVERIFY(!e.importSrt(srt(dir,"bad SRT")));QCOMPARE(e.project().captions.size(),2);QCOMPARE(e.undoText(),history);
        e.setBurnCaptions(false);QVERIFY(e.exportSrt(dir.filePath("output.srt")));QVERIFY(!e.exportSrt(footage()));
        QFile file(dir.filePath("output.srt"));QVERIFY(file.open(QIODevice::ReadOnly));const auto parsed=Subtitles::parseSrt(file.readAll());QVERIFY(parsed);QCOMPARE(parsed->size(),2);
    }
    void selectionAndNavigationDoNotSeekUntilPreview() {
        QTemporaryDir dir;EditorController e;QVERIFY(e.importFiles({footage()}));QVERIFY(e.importSrt(srt(dir)));e.seek(90);
        e.selectCaption(0);QCOMPARE(e.position(),90);e.navigateCaption(1);QCOMPARE(e.selectedCaption(),1);QCOMPARE(e.position(),90);
        e.previewCaption();QCOMPARE(e.position(),30);
    }
    void textDraftAndBatchChangesUndoAsWholeCommands() {
        QTemporaryDir dir;EditorController e;QVERIFY(e.importFiles({footage()}));QVERIFY(e.importSrt(srt(dir)));e.selectCaption(0);
        e.beginCaptionEdit(0);e.previewCaptionContent("FIRST changed\n第二行");e.endTextDrag(true);QCOMPARE(e.project().captions[0].text,QString("FIRST"));
        e.beginCaptionEdit(0);e.previewCaptionContent("Changed");e.endTextDrag(false);e.undo();QCOMPARE(e.project().captions[0].text,QString("FIRST"));e.redo();QCOMPARE(e.project().captions[0].text,QString("Changed"));
        e.selectCaption(0);e.selectCaption(1,Qt::ControlModifier);QCOMPARE(e.selectedCaptions().size(),2);
        e.styleCaptions({{"fontSize",90},{"color","#ff00ff"}});QCOMPARE(e.project().captions[0].fontSize,90);QCOMPARE(e.project().captions[1].fontSize,90);
        e.undo();QCOMPARE(e.project().captions[0].fontSize,57);QCOMPARE(e.project().captions[1].fontSize,57);
        e.selectCaption(0);e.styleCaptions({{"fontSize",100}},true);QCOMPARE(e.project().captions[1].fontSize,100);e.undo();QCOMPARE(e.project().captions[1].fontSize,57);
    }
    void absoluteTimingIsRetainedAndFlaggedAfterSequenceEdits() {
        QTemporaryDir dir;EditorController e;QVERIFY(e.importFiles({footage(),footage()}));QVERIFY(e.importSrt(srt(dir)));e.selectClip(0);QVERIFY(e.trimClip(0,.5,2));
        QCOMPARE(e.project().captions[0].startFrame,0);QVERIFY(e.project().captions[0].timingNeedsReview);QVERIFY(!e.captionWarnings().isEmpty());
        e.undo();QVERIFY(!e.project().captions[0].timingNeedsReview);
    }
    void hostDeletionOffersOneUndoableDetachOrRemove() {
        QTemporaryDir dir;EditorController e;QVERIFY(e.importFiles({footage(),footage()}));e.addText();QVERIFY(e.importSrt(srt(dir)));e.selectText(0);e.attachTimedText(false,0);e.selectCaption(0);e.attachTimedText(true,0);
        e.selectClip(0);e.removeSelectedHosts(true);QCOMPARE(e.project().textOverlays.size(),1);QCOMPARE(e.project().captions.size(),2);QVERIFY(e.project().textOverlays[0].hostId.isEmpty());QVERIFY(e.project().captions[0].hostId.isEmpty());
        e.undo();QVERIFY(!e.project().textOverlays[0].hostId.isEmpty());QVERIFY(!e.project().captions[0].hostId.isEmpty());e.selectClip(0);e.removeSelectedHosts(false);QVERIFY(e.project().textOverlays.isEmpty());QCOMPARE(e.project().captions.size(),1);e.undo();QCOMPARE(e.project().captions.size(),2);
    }
    void typingContinuesAfterFocusedSaveAndOtherCommandsCommitDrafts() {
        QTemporaryDir dir;EditorController e;QVERIFY(e.importFiles({footage()}));e.addCaption();e.beginCaptionEdit(0);e.previewCaptionContent("Saved");QVERIFY(e.saveTo(dir.filePath("saved.isaac.json")));e.previewCaptionContent("Saved and continued");e.endTextDrag(false);QCOMPARE(e.project().captions[0].text,QString("Saved and continued"));e.undo();QCOMPARE(e.project().captions[0].text,QString("Saved"));
        e.beginCaptionEdit(0);e.previewCaptionContent("Reviewed");e.reviewCaptions();e.undo();QCOMPARE(e.project().captions.size(),1);QCOMPARE(e.project().captions[0].text,QString("Saved"));
    }
    void backgroundExportCapturesCaptionBurnRevision() {
        QTemporaryDir dir;EditorController e;QVERIFY(e.importFiles({footage()}));e.addCaption();e.beginCaptionEdit(0);e.previewCaptionContent("FROZEN");e.endTextDrag(false);e.styleCaptions({{"color","#ff00ff"},{"fontSize",150},{"shadow",false}});
        e.styleCaptions({{"color","#ffffff"}});e.styleCaptionText(0,3,{{"color","#ff00ff"},{"bold",false}});
        const auto path=dir.filePath("frozen.mp4");QVERIFY(e.exportTo(path));e.setBurnCaptions(false);e.styleCaptions({{"color","#0000ff"}});QTRY_VERIFY_WITH_TIMEOUT(!e.exporting(),10000);QCOMPARE(e.exportState(),QString("Completed"));
        Project output;output.clips={{path,0,2}};Mlt::Profile profile;Media::configureOutputProfile(profile,640,360);auto timeline=Media::buildTimeline(output,profile);QVERIFY(timeline);timeline->seek(30);std::unique_ptr<Mlt::Frame> frame(timeline->get_frame());QVERIFY(frame);auto format=mlt_image_rgba;uint8_t *pixels=nullptr;int width=640,height=360;QVERIFY(!mlt_frame_get_image(frame->get_frame(),&pixels,&format,&width,&height,0));QVERIFY(pixels);
        QImage image(pixels,width,height,width*4,QImage::Format_RGBA8888);int magenta=0;for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto c=image.pixelColor(x,y);magenta+=c.red()>160&&c.blue()>150&&c.green()<110;}QVERIFY(magenta>30);
    }
    void visualGestureUndoRestoresPreviousCaptionSelection() {
        EditorController e;QVERIFY(e.importFiles({footage()}));e.addCaption();QVERIFY(e.addVisualOverlay(footage(),0));e.selectCaption(0);e.beginVisualGesture(0);e.setTransform({{"x",.6}});e.endVisualGesture(false);e.undo();QCOMPARE(e.selectedCaption(),0);QCOMPARE(e.selectedOverlay(),-1);
    }
    void shiftSelectionAfterDeleteUndoAndShorterOpenStaysBounded() {
        QTemporaryDir dir;EditorController e;QVERIFY(e.importFiles({footage()}));e.addCaption();e.addCaption();e.addCaption();e.selectCaption(2);e.deleteCaptions();e.selectCaption(0,Qt::ShiftModifier);
        for(const auto &index:e.selectedCaptions())QVERIFY(index.toInt()>=0&&index.toInt()<e.project().captions.size());e.deleteCaptions();e.undo();QVERIFY(!e.project().captions.isEmpty());e.selectCaption(0,Qt::ShiftModifier);for(const auto &index:e.selectedCaptions())QVERIFY(index.toInt()<e.project().captions.size());
        EditorController shortProject;shortProject.addCaption();QVERIFY(shortProject.saveTo(dir.filePath("short.isaac.json")));QVERIFY(e.loadFrom(dir.filePath("short.isaac.json")));e.selectCaption(0,Qt::ShiftModifier);QCOMPARE(e.selectedCaptions(),QVariantList{0});e.deleteCaptions();QVERIFY(e.project().captions.isEmpty());
    }
    void durationEditsPreserveChosenHostsInsideCrossfades() {
        EditorController e;QVERIFY(e.importFiles({footage(),footage()}));e.setCrossfade(.5);e.seek(100);e.addCaption();e.setCaptionTiming(0,100,20);e.attachTimedText(true,1);e.addText();e.setTextTiming(0,100,20);e.attachTimedText(false,1);const auto host=e.project().clips[1].id;
        e.setCaptionTiming(0,100,21);e.setTextTiming(0,100,21);QCOMPARE(e.project().captions[0].hostId,host);QCOMPARE(e.project().textOverlays[0].hostId,host);
        e.moveGroup({1},0);QCOMPARE(e.project().textStartFrame(0,true),10);QCOMPARE(e.project().textStartFrame(0),10);e.undo();QCOMPARE(e.project().textStartFrame(0,true),100);e.undo();QCOMPARE(e.project().textOverlays[0].durationFrames,20);QCOMPARE(e.project().textOverlays[0].hostId,host);e.undo();QCOMPARE(e.project().captions[0].durationFrames,20);
        e.setCaptionTiming(0,101,20);e.setTextTiming(0,101,20);QCOMPARE(e.project().captions[0].hostId,host);QCOMPARE(e.project().textOverlays[0].hostId,host);
    }
    void malformedUnnumberedTimingImportKeepsProjectAndHistory() {
        QTemporaryDir dir;EditorController e;QVERIFY(e.importSrt(srt(dir)));const auto before=e.project().captions;const auto history=e.undoText();
        for(const auto header:{QByteArray("00:00:02,000 -> 00:00:03,000"),QByteArray("2\n00:00:02,000 -> 00:00:03,000"),QByteArray("00:02 -> 00:03")}){
            QVERIFY(!e.importSrt(srt(dir,QByteArray("1\n00:00:00,000 --> 00:00:01,000\nvalid\n\n")+header+"\nbroken\n")));QCOMPARE(e.project().captions,before);QCOMPARE(e.undoText(),history);
        }
    }
    void draftCaptionsStayOutOfRecovery() {
        QTemporaryDir dir;EditorController e;e.enableRecovery(dir.filePath("recovery"));QVERIFY(e.importFiles({footage()}));e.addCaption();e.beginCaptionEdit(0);e.previewCaptionContent("UNCOMMITTED");e.styleCaptionText(0,3,{{"color","#ff00ff"}});
        QDir recovery(dir.filePath("recovery"));QTRY_VERIFY_WITH_TIMEOUT(!recovery.entryList({"*.recovery.json"},QDir::Files).isEmpty(),3000);
        const auto saved=loadProject(recovery.filePath(recovery.entryList({"*.recovery.json"},QDir::Files).first()));QVERIFY(saved);QCOMPARE(saved->captions[0].text,QString("New caption"));QVERIFY(saved->captions[0].spans.isEmpty());e.endTextDrag(true);
    }
};
QTEST_MAIN(CaptionControllerTest)
#include "caption_controller_test.moc"
