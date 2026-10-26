#include "editor_controller.h"
#include "captions.h"
#include "mlt_engine.h"
#include "preview.h"
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
        QTemporaryDir dir;EditorController e;e.enableRecovery(dir.filePath("recovery"));QVERIFY(e.importFiles({footage()}));e.addCaption();e.beginCaptionEdit(0);e.previewCaptionContent("UNCOMMITTED");
        QDir recovery(dir.filePath("recovery"));QTRY_VERIFY_WITH_TIMEOUT(!recovery.entryList({"*.recovery.json"},QDir::Files).isEmpty(),3000);
        const auto saved=loadProject(recovery.filePath(recovery.entryList({"*.recovery.json"},QDir::Files).first()));QVERIFY(saved);QCOMPARE(saved->captions[0].text,QString("New caption"));e.endTextDrag(true);
    }
};
QTEST_MAIN(CaptionControllerTest)
#include "caption_controller_test.moc"
