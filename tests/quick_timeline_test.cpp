#include <QFontMetrics>
#include <QPaintEngine>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <limits>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QtTest>

#include "quick_timeline.h"

namespace {
struct PaintCounter : QPaintDevice {
    struct Engine : QPaintEngine {
        int texts=0;
        bool begin(QPaintDevice *) override {return true;}
        bool end() override {return true;}
        Type type() const override {return User;}
        void updateState(const QPaintEngineState &) override {}
        void drawPixmap(const QRectF &,const QPixmap &,const QRectF &) override {}
        void drawPath(const QPainterPath &) override {}
        void drawPolygon(const QPointF *,int,PolygonDrawMode) override {}
        void drawTextItem(const QPointF &,const QTextItem &) override {++texts;}
    } mutable engine;
    QPaintEngine *paintEngine() const override {return &engine;}
    int metric(PaintDeviceMetric m) const override {
        if(m==PdmWidth)return 1000;if(m==PdmHeight)return 320;
        if(m==PdmDpiX||m==PdmDpiY||m==PdmPhysicalDpiX||m==PdmPhysicalDpiY)return 96;
        if(m==PdmDevicePixelRatio)return 1;if(m==PdmDevicePixelRatioScaled)return devicePixelRatioFScale();
        return 32;
    }
};
Project sequence() {
    Project p;
    p.clips = {{"first.mp4", 0, 4}, {"second.mp4", 1, 5}, {"third.mp4", 2, 6}};
    return p;
}
struct Scene {
    QQuickWindow window;
    QuickTimeline *view = new QuickTimeline(window.contentItem());
    Scene() {
        window.resize(1000, 320);
        view->setSize(QSizeF(1000, 320));
        view->setProject(sequence(), {8, 8, 8});
        window.show();
        view->forceActiveFocus();
        QTest::qWait(20);
    }
    QPoint atFrame(int frame, int y = 12) const {
        return QPoint(qRound(view->labelWidth() + 12 + frame * view->pixelsPerSecond() / 60 - view->contentOffset()), y);
    }
};
QImage render(QuickTimeline *view) {
    QImage image(view->size().toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    view->paint(&painter);
    return image;
}
int barcode(const QImage &image, const QRectF &bounds) {
    int value = 0;
    for (int bit = 0; bit < 12; ++bit) {
        const QPoint point(qRound(bounds.left() + (bit * 8 + 4) * bounds.width() / 128), qRound(bounds.center().y()));
        if (image.pixelColor(point).red() > 127) value |= 1 << bit;
    }
    return value;
}
void beginDrag(Scene &s, const QPoint &from, const QPoint &to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::mousePress(&s.window, Qt::LeftButton, modifiers, from);
    QTest::mouseMove(&s.window, to, 1);
}
}

class QuickTimelineTest : public QObject {
    Q_OBJECT
private slots:
    void playheadMovementReusesTimelineArtwork() {
        Scene s;const auto original=render(s.view);
        QElapsedTimer timer;timer.start();
        for(int i=1;i<=120;++i){s.view->setPosition(i);render(s.view);}
        qInfo()<<"120 timeline frames (ms):"<<timer.nsecsElapsed()/1000000.;
        PaintCounter counter;QPainter painter(&counter);s.view->paint(&painter);painter.end();
        QCOMPARE(counter.engine.texts,0);
        s.view->setPosition(0);QCOMPARE(render(s.view),original);
        s.view->setSelection({1});const auto selected=render(s.view);QVERIFY(selected!=original);
        s.view->setSelection({});QCOMPARE(render(s.view),original);
        s.view->zoomIn();QVERIFY(render(s.view)!=original);
    }
    void audioMediaDetailsAreAvailableWithoutHover();
    void offscreenMediaKeepsItsCompletedStatus();
    void compactMediaFeedbackKeepsHoverAndKeyboardDetails();
    void loadingFilmstripUsesQuietPlaceholders();
    void captionsHaveIndependentIntervalsAndFrameEdgeGestures();
    void captionExtentAndDenseSequentialRowsRemainAccessible();
    void textBlocksMoveTrimAndCancel();
    void selectedTextHasKeyboardTimingAlternatives();
    void geometryUsesGlobalCrossfades();
    void anchoredZoomDoesNotSeek();
    void scrubReleaseUsesFinalPointerPosition();
    void bodySelectionDoesNotSeek();
    void programmaticPlayheadTracksIntoView();
    void modifiersAreDelegatedAndSelectedGroupSurvivesPress();
    void rulerScrubEscRestoresStartingFrame();
    void trimPreviewRecomputesEveryBoundaryAndCancels();
    void trimCommitsOnceWithBoundsAndExactSourceComparison();
    void frontTrimHandleWinsOverTransition_data();
    void frontTrimHandleWinsOverTransition();
    void snappingAcquiresReleasesAndHonorsBounds();
    void reorderUsesExistingBoundariesAndPreservesGroupOrder();
    void landingPreviewMatchesCommittedGeometry_data();
    void landingPreviewMatchesCommittedGeometry();
    void cancelClearsLandingPreview();
    void reorderReleaseUsesFinalLandingPosition();
    void landingLabelStaysVisible_data();
    void landingLabelStaysVisible();
    void durationChangingReorderKeepsLandingViewport_data();
    void durationChangingReorderKeepsLandingViewport();
    void noOpReorderHasNoCommit();
    void activeTrimEdgeDoesNotSnapToItself();
    void dragEdgeScrollStopsOnCancel();
    void keyboardAndSkimmingKeepIndependentPositions();
    void lostGrabCancelsTrimAndScrub();
    void wheelPansAndModifiedWheelZoomsWithoutSeeking();
    void audioLanesAndNativeFileDrops();
    void renderedFilmstripUsesSourceTrimAndRotation();
    void appendWithAutoFitReusesCompletedFilmstrip();
    void filmstripReuseStaysInsideTileInterval();
    void waveformsUseTrimAndMusicLoops();
    void waveformScaleClampsFiniteValues();
    void waveformScaleChangesOnlyPaintedAmplitude();
};

void QuickTimelineTest::audioMediaDetailsAreAvailableWithoutHover() {
    Scene s; MediaCache cache;
    auto project=sequence();project.voicePath="missing-voice.wav";
    project.musicPath=QDir::current().filePath("tests/fixtures/seek.mp4");
    s.view->setProject(project,{},2);s.view->setMediaCache(&cache);
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(project.musicPath),QString("unavailable"),5000);
    const auto details=s.view->property("selectionMediaStatus").toString();
    QVERIFY2(details.contains("Narration: missing-voice.wav: Media missing"),qPrintable(details));
    QVERIFY2(details.contains("Music: seek.mp4: Waveform unavailable"),qPrintable(details));
}
void QuickTimelineTest::offscreenMediaKeepsItsCompletedStatus() {
    Scene s; MediaCache cache;
    const auto path=QDir::current().filePath("tests/fixtures/seek.mp4");
    Project project;project.clips={{path,0,2},{"long.mp4",0,120}};
    s.view->setProject(project);s.view->setMediaCache(&cache);s.view->setSelection({0});
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(path),QString("unavailable"),5000);
    for(int i=0;i<8;++i)s.view->zoomIn();
    s.view->setContentOffset(s.view->maximumOffset());
    QVERIFY(s.view->clipRect(0).right()<s.view->labelWidth());
    const auto details=s.view->property("selectionMediaStatus").toString();
    QVERIFY2(details.contains("Waveform unavailable"),qPrintable(details));
    QVERIFY(!details.contains("Loading waveform"));
}

void QuickTimelineTest::compactMediaFeedbackKeepsHoverAndKeyboardDetails() {
    Scene s;
    s.view->setSelection({0});
    QVERIFY2(s.view->property("selectionMediaStatus").isValid(),"Compact status symbols need keyboard-readable details");
    QVERIFY(s.view->property("selectionMediaStatus").toString().contains("Media missing"));
    const auto center=s.view->clipRect(0).center();
    QHoverEvent hover(QEvent::HoverMove,center,center,center);
    QCoreApplication::sendEvent(s.view,&hover);
    QVERIFY(s.view->property("hoverMediaStatus").toString().contains("Media missing"));
    QHoverEvent leave(QEvent::HoverLeave,QPointF(-1,-1),center,center);
    QCoreApplication::sendEvent(s.view,&leave);
    QVERIFY(s.view->property("hoverMediaStatus").toString().isEmpty());
    QVERIFY(!s.view->property("selectionMediaStatus").toString().isEmpty());

    MediaCache cache;
    s.view->setMediaCache(&cache);
    auto project=sequence();project.clips={{QDir::current().filePath("tests/fixtures/seek.mp4"),0,2}};
    s.view->setPlaying(true);s.view->setProject(project);s.view->setSelection({0});
    QVERIFY(s.view->property("selectionMediaStatus").toString().contains("Loading"));
    s.view->setPlaying(false);
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(project.clips[0].path),QString("unavailable"),5000);
    QVERIFY(s.view->property("selectionMediaStatus").toString().contains("Waveform unavailable"));
    QVERIFY(!s.view->property("selectionMediaStatus").toString().contains("Media missing"));
}
void QuickTimelineTest::loadingFilmstripUsesQuietPlaceholders() {
    Scene s; MediaCache cache;
    s.view->setMediaCache(&cache);s.view->setPlaying(true);
    auto project=sequence();project.clips={{QDir::current().filePath("tests/fixtures/seek.mp4"),0,2}};
    s.view->setProject(project);
    const auto rect=s.view->clipRect(0);
    const auto image=render(s.view);
    const int label=QFontMetrics(s.view->font()).height()+6;
    const QRect tile(qRound(rect.left()+12),qRound(rect.top()+label+6),60,qRound(rect.height()-label-18-12));
    QVERIFY(tile.height()>8);
    const auto color=image.pixelColor(tile.topLeft());
    for(int y=tile.top();y<tile.bottom();++y)
        for(int x=tile.left();x<tile.right();++x)
            QCOMPARE(image.pixelColor(x,y),color);
}

void QuickTimelineTest::textBlocksMoveTrimAndCancel() {
    Scene s;auto project=sequence();TextOverlay t;t.startFrame=60;t.durationFrames=120;project.textOverlays={t};s.view->setProject(project);s.view->setSnapping(false);
    QRectF rect;
    QVERIFY(QMetaObject::invokeMethod(s.view,"textRect",Qt::DirectConnection,Q_RETURN_ARG(QRectF,rect),Q_ARG(int,0)));
    QVERIFY(!rect.isEmpty());
    QSignalSpy moves(s.view,SIGNAL(textTimingRequested(int,int,int)));
    QSignalSpy selects(s.view,SIGNAL(textSelected(int)));
    const QPoint center=rect.center().toPoint();
    beginDrag(s,center,center+QPoint(60,0));QTest::mouseRelease(&s.window,Qt::LeftButton,Qt::NoModifier,center+QPoint(60,0));
    QCOMPARE(selects.size(),1);QCOMPARE(moves.size(),1);QCOMPARE(moves[0][0].toInt(),0);QCOMPARE(moves[0][2].toInt(),120);QVERIFY(moves[0][1].toInt()>60);
    const auto edge=QPoint(qRound(rect.right()-3),qRound(rect.center().y()));
    beginDrag(s,edge,edge+QPoint(30,0));QTest::keyClick(&s.window,Qt::Key_Escape);QTest::mouseRelease(&s.window,Qt::LeftButton,Qt::NoModifier,edge+QPoint(30,0));QCOMPARE(moves.size(),1);
    beginDrag(s,edge,edge+QPoint(30,0));QTest::mouseRelease(&s.window,Qt::LeftButton,Qt::NoModifier,edge+QPoint(30,0));
    QCOMPARE(moves.size(),2);QCOMPARE(moves[1][1].toInt(),60);QVERIFY(moves[1][2].toInt()>120);
}
void QuickTimelineTest::selectedTextHasKeyboardTimingAlternatives() {
    Scene s;auto p=sequence();TextOverlay t;t.startFrame=60;t.durationFrames=120;p.textOverlays={t};s.view->setProject(p);
    QVERIFY(s.view->setProperty("selectedText",0));QSignalSpy changes(s.view,SIGNAL(textTimingRequested(int,int,int)));
    QTest::keyClick(&s.window,Qt::Key_Left,Qt::AltModifier);QCOMPARE(changes.size(),1);QCOMPARE(changes[0][1].toInt(),59);QCOMPARE(changes[0][2].toInt(),120);
    QTest::keyClick(&s.window,Qt::Key_Right,Qt::AltModifier|Qt::ShiftModifier);QCOMPARE(changes.size(),2);QCOMPARE(changes[1][1].toInt(),60);QCOMPARE(changes[1][2].toInt(),121);
}

void QuickTimelineTest::geometryUsesGlobalCrossfades() {
    Scene s;
    Project p;
    p.clips = {{"a.mp4", 0, 2}, {"b.mp4", 0, 3}, {"c.mp4", 0, 4}};
    p.crossfadeFrames = 30;
    s.view->setProject(p);
    const auto first = s.view->clipRect(0);
    const double ppf = s.view->pixelsPerSecond() / 60;
    QCOMPARE(qRound((s.view->clipRect(1).left() - first.left()) / ppf), 90);
    QCOMPARE(qRound((s.view->clipRect(2).left() - first.left()) / ppf), 240);
    QVERIFY(s.view->clipRect(-1).isEmpty());
    QVERIFY(s.view->clipRect(3).isEmpty());
}

void QuickTimelineTest::anchoredZoomDoesNotSeek() {
    Scene s;
    s.view->setPosition(480);
    QSignalSpy seek(s.view, &QuickTimeline::seekRequested);
    s.view->zoomAt(4, s.view->labelWidth() + 12);
    s.view->setContentOffset(317.25);
    const int anchor = 487;
    const int before = s.view->frameAt(anchor);
    const double scale = s.view->pixelsPerSecond();
    const double origin = s.view->labelWidth() + 12;
    const double exactBefore = (anchor - origin + s.view->contentOffset()) / scale;
    s.view->zoomAt(1.5, anchor);
    QVERIFY(qAbs(s.view->frameAt(anchor) - before) <= 1);
    const double exactAfter = (anchor - origin + s.view->contentOffset()) / s.view->pixelsPerSecond();
    QVERIFY(qAbs(exactBefore - exactAfter) < 1e-9);
    s.view->fitToWidth();
    QCOMPARE(s.view->position(), 480);
    QCOMPARE(seek.count(), 0);
    QCOMPARE(s.view->contentOffset(), 0.0);
}

void QuickTimelineTest::scrubReleaseUsesFinalPointerPosition() {
    Scene s;
    QSignalSpy seek(s.view, &QuickTimeline::seekRequested);
    QSignalSpy finished(s.view, &QuickTimeline::scrubFinished);
    QTest::mousePress(&s.window, Qt::LeftButton, Qt::NoModifier, s.atFrame(120));
    const QPoint final = s.atFrame(180);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(final), QPointF(s.window.mapToGlobal(final)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&s.window, &release);
    QVERIFY(qAbs(s.view->position() - 180) <= 1);
    QVERIFY(qAbs(seek.last().at(0).toInt() - 180) <= 1);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).toBool(), false);
}

void QuickTimelineTest::programmaticPlayheadTracksIntoView() {
    Scene s;
    s.view->zoomAt(4, s.view->labelWidth() + 12);
    s.view->setPosition(700);
    QVERIFY(s.view->contentOffset() > 0);
    QVERIFY(qAbs(s.view->frameAt(976) - 700) <= 1);
}

void QuickTimelineTest::bodySelectionDoesNotSeek() {
    Scene s;
    s.view->setPosition(30);
    QSignalSpy selected(s.view, &QuickTimeline::selectionRequested);
    QSignalSpy seek(s.view, &QuickTimeline::seekRequested);
    QSignalSpy cleared(s.view, &QuickTimeline::clearSelectionRequested);
    QTest::mouseClick(&s.window, Qt::LeftButton, Qt::NoModifier, s.view->clipRect(1).center().toPoint());
    QCOMPARE(selected.count(), 1);
    QCOMPARE(selected.first().at(0).toInt(), 1);
    QCOMPARE(seek.count(), 0);
    QCOMPARE(s.view->position(), 30);
    QTest::mouseClick(&s.window, Qt::LeftButton, Qt::NoModifier, QPoint(500, 310));
    QCOMPARE(cleared.count(), 1);
}

void QuickTimelineTest::modifiersAreDelegatedAndSelectedGroupSurvivesPress() {
    Scene s;
    QSignalSpy selected(s.view, &QuickTimeline::selectionRequested);
    QTest::mouseClick(&s.window, Qt::LeftButton, Qt::ControlModifier, s.view->clipRect(1).center().toPoint());
    QCOMPARE(selected.last().at(1).toInt(), int(Qt::ControlModifier));
    QTest::mouseClick(&s.window, Qt::LeftButton, Qt::ShiftModifier, s.view->clipRect(2).center().toPoint());
    QCOMPARE(selected.last().at(1).toInt(), int(Qt::ShiftModifier));
    s.view->setSelection({0, 2});
    selected.clear();
    QTest::mousePress(&s.window, Qt::LeftButton, Qt::NoModifier, s.view->clipRect(0).center().toPoint());
    QCOMPARE(selected.count(), 0);
    QCOMPARE(s.view->selection(), QVariantList({0, 2}));
    QTest::keyClick(&s.window, Qt::Key_Escape);
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, s.view->clipRect(0).center().toPoint());
}

void QuickTimelineTest::rulerScrubEscRestoresStartingFrame() {
    Scene s;
    s.view->setPosition(60);
    QSignalSpy started(s.view, &QuickTimeline::scrubStarted);
    QSignalSpy finished(s.view, &QuickTimeline::scrubFinished);
    QSignalSpy seek(s.view, &QuickTimeline::seekRequested);
    beginDrag(s, s.atFrame(120), s.atFrame(180));
    QCOMPARE(started.count(), 1);
    QVERIFY(qAbs(s.view->position() - 180) <= 1);
    QTest::keyClick(&s.window, Qt::Key_Escape);
    QCOMPARE(s.view->position(), 60);
    QCOMPARE(seek.last().at(0).toInt(), 60);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).toBool(), true);
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, s.atFrame(180));
    QCOMPARE(finished.count(), 1);
}

void QuickTimelineTest::trimPreviewRecomputesEveryBoundaryAndCancels() {
    Scene s;
    Project p;
    p.clips = {{"a.mp4", 0, 4}, {"b.mp4", 0, 4}, {"c.mp4", 0, 4}};
    p.crossfadeFrames = 60;
    s.view->setProject(p, {8, 8, 8});
    s.view->setSnapping(false);
    const auto initial = s.view->clipRect(1);
    const double ppf = s.view->pixelsPerSecond() / 60;
    const QPoint from(qRound(initial.right() - 3), qRound(initial.center().y()));
    const QPoint to = from - QPoint(qRound(180 * ppf), 0);
    QSignalSpy trimmed(s.view, &QuickTimeline::trimRequested);
    beginDrag(s, from, to);
    // Middle clip now has 60 frames. Two adjacent fades clamp globally to 29.
    QCOMPARE(qRound((s.view->clipRect(1).left() - s.view->clipRect(0).left()) / ppf), 211);
    QCOMPARE(qRound((s.view->clipRect(2).left() - s.view->clipRect(0).left()) / ppf), 242);
    QVERIFY(s.view->gestureText().contains("fade", Qt::CaseInsensitive));
    QVERIFY(s.view->trimPreview().value("active").toBool());
    QTest::keyClick(&s.window, Qt::Key_Escape);
    QCOMPARE(s.view->clipRect(1), initial);
    QVERIFY(!s.view->trimPreview().value("active").toBool());
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, to);
    QCOMPARE(trimmed.count(), 0);
}

void QuickTimelineTest::trimCommitsOnceWithBoundsAndExactSourceComparison() {
    Scene s;
    Project p;
    p.clips = {{"a.mp4", 1, 5, false, 90}, {"b.mp4", 2, 6, false, 270}};
    s.view->setProject(p, {7, 8});
    s.view->setSnapping(false);
    auto r = s.view->clipRect(0);
    const double ppf = s.view->pixelsPerSecond() / 60;
    const QPoint from(qRound(r.right() - 3), qRound(r.center().y()));
    const QPoint to = from + QPoint(qRound(60 * ppf), 0);
    QSignalSpy trimmed(s.view, &QuickTimeline::trimRequested);
    beginDrag(s, from, to);
    QCOMPARE(trimmed.count(), 0);
    const auto preview = s.view->trimPreview();
    QCOMPARE(preview.value("outPath").toString(), QString("a.mp4"));
    QCOMPARE(preview.value("outTime").toDouble(), 359 / 60.0);
    QCOMPARE(preview.value("outRotation").toInt(), 90);
    QCOMPARE(preview.value("inPath").toString(), QString("b.mp4"));
    QCOMPARE(preview.value("inTime").toDouble(), 2.0);
    QCOMPARE(preview.value("inRotation").toInt(), 270);
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, to);
    QCOMPARE(trimmed.count(), 1);
    QCOMPARE(trimmed.first().at(0).toInt(), 0);
    QCOMPARE(trimmed.first().at(1).toDouble(), 1.0);
    QCOMPARE(trimmed.first().at(2).toDouble(), 6.0);
    r = s.view->clipRect(1);
    const QPoint start(qRound(r.left() + 3), qRound(r.center().y()));
    const QPoint pastEnd(990, start.y());
    beginDrag(s, start, pastEnd);
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, pastEnd);
    QCOMPARE(trimmed.last().at(1).toDouble(), 359 / 60.0);
}

void QuickTimelineTest::frontTrimHandleWinsOverTransition_data() {
    QTest::addColumn<int>("clip");
    QTest::addColumn<bool>("top");
    QTest::addColumn<int>("fade");
    QTest::newRow("first-top") << 0 << true << 0;
    QTest::newRow("first-middle") << 0 << false << 0;
    QTest::newRow("second-top") << 1 << true << 0;
    QTest::newRow("second-middle") << 1 << false << 0;
    QTest::newRow("dissolve-top") << 1 << true << 30;
    QTest::newRow("dissolve-middle") << 1 << false << 30;
}

void QuickTimelineTest::frontTrimHandleWinsOverTransition() {
    QFETCH(int, clip);
    QFETCH(bool, top);
    QFETCH(int, fade);
    Scene s;
    auto project = sequence();
    project.crossfadeFrames = fade;
    s.view->setProject(project, {8, 8, 8});
    s.view->setSnapping(false);
    const auto rect = s.view->clipRect(clip);
    const QPoint from(qRound(rect.left() + 3), qRound(top ? rect.top() + 5 : rect.center().y()));
    const QPoint to = from + QPoint(qRound(s.view->pixelsPerSecond()), 0);
    QSignalSpy trimmed(s.view, &QuickTimeline::trimRequested);
    QSignalSpy transitions(s.view, &QuickTimeline::transitionPreviewRequested);
    beginDrag(s, from, to);
    QVERIFY(s.view->trimPreview().value("active").toBool());
    QCOMPARE(s.view->trimPreview().value("inTime").toDouble(), clip + 1.0);
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, to);
    QCOMPARE(transitions.count(), 0);
    QCOMPARE(trimmed.count(), 1);
    QCOMPARE(trimmed.first()[0].toInt(), clip);
    QCOMPARE(trimmed.first()[1].toDouble(), clip + 1.0);
    QCOMPARE(trimmed.first()[2].toDouble(), clip + 4.0);
}

void QuickTimelineTest::snappingAcquiresReleasesAndHonorsBounds() {
    Scene s;
    Project p;
    p.clips = {{"a.mp4", 0, 4}};
    s.view->setProject(p, {8});
    s.view->zoomAt(4, s.view->labelWidth() + 12);
    s.view->setPosition(180);
    const auto r = s.view->clipRect(0);
    const double ppf = s.view->pixelsPerSecond() / 60;
    const QPoint from(qRound(r.right() - 3), qRound(r.center().y()));
    // Recenter so the end handle is visible while keeping 1-frame resolution.
    s.view->setContentOffset(s.view->contentOffset() + qMax(0.0, r.right() - 900));
    const QPoint visibleFrom(qRound(s.view->clipRect(0).right() - 3), from.y());
    const QPoint near = visibleFrom - QPoint(qRound(60 * ppf - 5), 0);
    QTest::mousePress(&s.window, Qt::LeftButton, Qt::NoModifier, visibleFrom);
    QTest::mouseMove(&s.window, near, 1);
    QCOMPARE(s.view->trimPreview().value("outTime").toDouble(), 179 / 60.0);
    QTest::mouseMove(&s.window, near + QPoint(5, 0), 1);
    QCOMPARE(s.view->trimPreview().value("outTime").toDouble(), 179 / 60.0);
    QTest::mouseMove(&s.window, near + QPoint(10, 0), 1);
    QVERIFY(s.view->trimPreview().value("outTime").toDouble() > 179 / 60.0);
    QTest::keyClick(&s.window, Qt::Key_Escape);
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, near);
    // A source limit wins over a nearby playhead target.
    s.view->fitToWidth();
    s.view->setPosition(239);
    s.view->setProject(p, {4});
    const auto bounded = s.view->clipRect(0);
    const QPoint edge(qRound(bounded.right() - 3), qRound(bounded.center().y()));
    QSignalSpy trimmed(s.view, &QuickTimeline::trimRequested);
    beginDrag(s, edge, edge + QPoint(20, 0), Qt::AltModifier);
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::AltModifier, edge + QPoint(20, 0));
    QCOMPARE(trimmed.count(), 0);
}

void QuickTimelineTest::reorderUsesExistingBoundariesAndPreservesGroupOrder() {
    Scene s;
    Project p = sequence();
    p.clips.append({"fourth.mp4", 0, 4});
    s.view->setProject(p, {8, 8, 8, 8});
    s.view->setSelection({0, 2});
    const auto from = s.view->clipRect(0).center().toPoint();
    const auto last = s.view->clipRect(3);
    const QPoint end(qRound(last.right()), qRound(last.center().y()));
    QSignalSpy moved(s.view, &QuickTimeline::groupMoveRequested);
    beginDrag(s, from, end);
    QVERIFY(s.view->gestureText().contains("Place at end"));
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, end);
    QCOMPARE(moved.count(), 1);
    QCOMPARE(moved.first().at(0).toList(), QVariantList({0, 2}));
    QCOMPARE(moved.first().at(1).toInt(), 4);
}

void QuickTimelineTest::landingPreviewMatchesCommittedGeometry_data() {
    QTest::addColumn<QVariantList>("selection");
    QTest::addColumn<int>("boundary");
    QTest::addColumn<QVector<int>>("order");
    QTest::addColumn<int>("crossfade");
    QTest::addColumn<QString>("placement");
    QTest::newRow("forward") << QVariantList{0} << 3 << QVector<int>{1,2,0,3} << 0 << "Place before Clip 4";
    QTest::newRow("backward") << QVariantList{2} << 0 << QVector<int>{2,0,1,3} << 0 << "Place before Clip 1";
    QTest::newRow("end") << QVariantList{1} << 4 << QVector<int>{0,2,3,1} << 0 << "Place at end";
    QTest::newRow("crossfade") << QVariantList{0} << 3 << QVector<int>{1,2,0,3} << 30 << "Place before Clip 4";
    QTest::newRow("group") << QVariantList{0,2} << 4 << QVector<int>{1,3,0,2} << 30 << "Place at end";
    QTest::newRow("no-op") << QVariantList{1} << 2 << QVector<int>{0,1,2,3} << 0 << "Place before Clip 3";
}

void QuickTimelineTest::landingPreviewMatchesCommittedGeometry() {
    QFETCH(QVariantList, selection);
    QFETCH(int, boundary);
    QFETCH(QVector<int>, order);
    QFETCH(int, crossfade);
    QFETCH(QString, placement);
    Scene s;
    Project p;
    p.clips = {{"first.mp4",0,2},{"second.mp4",0,5},{"third.mp4",0,3},{"fourth.mp4",0,4}};
    p.crossfadeFrames = crossfade;
    s.view->setProject(p);
    s.view->setPosition(31);
    s.view->setSelection(selection);
    const QPoint from = s.view->clipRect(selection.first().toInt()).center().toPoint();
    const int targetFrame = boundary == p.clips.size() ? p.durationFrames() : p.clipStartFrame(boundary);
    const QPoint to = s.atFrame(targetFrame, from.y());
    QSignalSpy moved(s.view, &QuickTimeline::groupMoveRequested);
    beginDrag(s, from, to);
    QVERIFY2(s.view->gestureText().startsWith(placement), qPrintable(s.view->gestureText()));
    const int first = order.indexOf(selection.first().toInt());
    const QRectF landing = s.view->clipRect(first).united(s.view->clipRect(first + selection.size() - 1));
    QVector<QRectF> previewRects;
    for (int i=0; i<order.size(); ++i) previewRects.append(s.view->clipRect(i));
    const auto previewImage = render(s.view);
    const QPoint marker(qRound(landing.left()), qRound(landing.top()-4));
    QCOMPARE(previewImage.pixelColor(marker), QGuiApplication::palette().color(QPalette::Highlight));
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, to);
    const bool changed = p.reorderClips(order);
    QCOMPARE(moved.size(), changed ? 1 : 0);
    if (changed) {
        QCOMPARE(moved.first().at(0).toList(), selection);
        QCOMPARE(moved.first().at(1).toInt(), boundary);
    }
    s.view->setProject(p);
    QVariantList finalSelection;
    for (const auto &index : selection) finalSelection.append(order.indexOf(index.toInt()));
    s.view->setSelection(finalSelection);
    for (int i=0; i<order.size(); ++i) QCOMPARE(s.view->clipRect(i), previewRects[i]);
    const QPoint interior(qRound(landing.left()+20), qRound(landing.top()+36));
    QVERIFY2(previewImage.pixelColor(interior) != render(s.view).pixelColor(interior),
             "The landing area must have a visible translucent fill while dragging");
    QVERIFY(s.view->gestureText().isEmpty());
}

void QuickTimelineTest::cancelClearsLandingPreview() {
    Scene s;
    s.view->setSelection({0});
    const auto before = render(s.view);
    const auto from = s.view->clipRect(0).center().toPoint();
    const QPoint to(qRound(s.view->clipRect(2).right()), from.y());
    QSignalSpy moved(s.view, &QuickTimeline::groupMoveRequested);
    beginDrag(s, from, to);
    QVERIFY(s.view->gestureText().startsWith("Place at end"));
    QVERIFY(render(s.view) != before);
    QTest::keyClick(&s.window, Qt::Key_Escape);
    QCOMPARE(render(s.view), before);
    QVERIFY(s.view->gestureText().isEmpty());
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, to);
    QCOMPARE(moved.size(), 0);
}

void QuickTimelineTest::reorderReleaseUsesFinalLandingPosition() {
    Scene s;
    s.view->setSelection({0});
    const auto from = s.view->clipRect(0).center().toPoint();
    const QPoint intermediate(qRound(s.view->clipRect(2).left()), from.y());
    const QPoint final(qRound(s.view->clipRect(2).right()), from.y());
    QSignalSpy moved(s.view, &QuickTimeline::groupMoveRequested);
    QString finalPlacement;
    QRectF finalLanding;
    connect(s.view, &QuickTimeline::gestureChanged, s.view, [&] {
        if (s.view->gestureText().startsWith("Place at end")) {
            finalPlacement = s.view->gestureText();
            finalLanding = s.view->clipRect(2);
        }
    });
    beginDrag(s, from, intermediate);
    QVERIFY(s.view->gestureText().startsWith("Place before Clip 3"));
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(final), QPointF(s.window.mapToGlobal(final)),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&s.window, &release);
    QVERIFY(!finalPlacement.isEmpty());
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.first().at(1).toInt(), 3);
    auto p = sequence();
    QVERIFY(p.reorderClips({1,2,0}));
    s.view->setProject(p);
    QCOMPARE(s.view->clipRect(2), finalLanding);
}

void QuickTimelineTest::landingLabelStaysVisible_data() {
    QTest::addColumn<bool>("dark");
    QTest::addColumn<int>("fontSize");
    QTest::newRow("light") << false << 13;
    QTest::newRow("dark") << true << 13;
    QTest::newRow("large-text") << false << 24;
}

void QuickTimelineTest::landingLabelStaysVisible() {
    QFETCH(bool, dark);
    QFETCH(int, fontSize);
    const auto originalPalette = QGuiApplication::palette();
    const auto restorePalette = qScopeGuard([&] { QGuiApplication::setPalette(originalPalette); });
    auto palette = originalPalette;
    palette.setColor(QPalette::Base, dark ? QColor("#202020") : QColor("#ffffff"));
    palette.setColor(QPalette::Text, dark ? QColor("#eeeeee") : QColor("#202020"));
    palette.setColor(QPalette::Highlight, QColor("#3876d4"));
    QGuiApplication::setPalette(palette);
    Scene s;
    s.window.resize(760, 540);
    s.view->setSize(QSizeF(760, 540));
    auto font = s.view->font(); font.setPointSize(fontSize); s.view->setFont(font);
    Project p;
    p.clips = {{"first.mp4",0,2},{"second.mp4",0,5},{"short.mp4",0,.25},{"fourth.mp4",0,4}};
    TextOverlay title; title.text="Title"; title.durationFrames=120; p.textOverlays={title};
    s.view->setProject(p);
    s.view->zoomAt(1.5, s.view->labelWidth()+12);
    s.view->setContentOffset(s.view->maximumOffset());
    s.view->setSelection({2});
    const auto from = s.view->clipRect(2).center().toPoint();
    const auto to = s.atFrame(p.durationFrames(), from.y());
    beginDrag(s, from, to);
    QVERIFY(s.view->gestureText().startsWith("Place at end"));
    const auto landing = s.view->clipRect(3);
    const auto image = render(s.view);
    // A short landing at the right edge leaves no room for the label to its right.
    QVERIFY(landing.left()+QFontMetrics(font).horizontalAdvance("Place at end") > s.view->width());
    const int labelY = qRound(landing.bottom()+8-QFontMetrics(font).height()/2.0-13);
    QCOMPARE(image.pixelColor(image.width()-12, labelY), palette.color(QPalette::Base));
    const QString label = "Place at end";
    const int textWidth = qCeil(QFontMetricsF(font).horizontalAdvance(label));
    const int labelHeight = QFontMetrics(font).height()+10;
    QImage expectedText(textWidth, labelHeight, QImage::Format_ARGB32_Premultiplied);
    expectedText.fill(palette.color(QPalette::Base));
    QPainter textPainter(&expectedText);
    textPainter.setFont(font);
    textPainter.setPen(palette.color(QPalette::Text));
    textPainter.drawText(expectedText.rect(), Qt::AlignVCenter, label);
    textPainter.end();
    const int labelTop = qRound(landing.bottom()-QFontMetrics(font).height()-10);
    // Compare the actual glyphs with the full phrase, excluding the bubble border.
    QCOMPARE(image.copy(image.width()-16-textWidth, labelTop+2, textWidth, labelHeight-4),
             expectedText.copy(0, 2, textWidth, labelHeight-4));
    const auto captures = qEnvironmentVariable("ISAAC_LANDING_SCREENSHOTS");
    if (!captures.isEmpty()) {
        QDir().mkpath(captures);
        QVERIFY(image.save(captures+"/landing-"+QTest::currentDataTag()+".png"));
    }
    QTest::keyClick(&s.window, Qt::Key_Escape);
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, to);
}

void QuickTimelineTest::durationChangingReorderKeepsLandingViewport_data() {
    QTest::addColumn<int>("shortIndex");
    QTest::addColumn<int>("boundary");
    QTest::addColumn<QVector<int>>("order");
    QTest::addColumn<bool>("zoomed");
    QTest::newRow("fit-grows") << 0 << 2 << QVector<int>{1,0,2,3} << false;
    QTest::newRow("fit-shrinks") << 1 << 0 << QVector<int>{1,0,2,3} << false;
    QTest::newRow("scrolled-grows") << 3 << 1 << QVector<int>{0,3,1,2} << true;
    QTest::newRow("scrolled-shrinks") << 1 << 4 << QVector<int>{0,2,3,1} << true;
}

void QuickTimelineTest::durationChangingReorderKeepsLandingViewport() {
    QFETCH(int, shortIndex);
    QFETCH(int, boundary);
    QFETCH(QVector<int>, order);
    QFETCH(bool, zoomed);
    Scene s;
    Project p;
    p.clips = {{"first.mp4",0,5},{"second.mp4",0,5},{"third.mp4",0,5},{"fourth.mp4",0,5}};
    p.clips[shortIndex].outSeconds=1;
    p.crossfadeFrames=100;
    s.view->setProject(p);
    s.view->setSelection({shortIndex});
    if (zoomed) {
        s.view->zoomAt(1.25, s.view->labelWidth()+12);
        s.view->setContentOffset(s.view->maximumOffset());
    }
    const auto from = s.view->clipRect(shortIndex).center().toPoint();
    const auto to = s.atFrame(boundary==p.clips.size() ? p.durationFrames() : p.clipStartFrame(boundary), from.y());
    QSignalSpy moved(s.view, &QuickTimeline::groupMoveRequested);
    Project committed=p;
    connect(s.view, &QuickTimeline::groupMoveRequested, s.view, [&] {
        committed.reorderClips(order);
        // The controller refreshes the timeline synchronously after committing.
        s.view->setProject(committed);
    });
    beginDrag(s, from, to);
    QVERIFY(s.view->gestureText().startsWith("Place"));
    if (zoomed) s.view->setContentOffset(s.view->maximumOffset());
    QVector<QRectF> previewRects;
    for (int i=0; i<order.size(); ++i) previewRects.append(s.view->clipRect(i));
    const double scale=s.view->pixelsPerSecond(), offset=s.view->contentOffset();
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, to);
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.first().at(1).toInt(), boundary);
    QVERIFY(committed.durationFrames()!=p.durationFrames());
    QCOMPARE(s.view->pixelsPerSecond(), scale);
    QCOMPARE(s.view->contentOffset(), offset);
    for (int i=0; i<order.size(); ++i) QCOMPARE(s.view->clipRect(i), previewRects[i]);
}

void QuickTimelineTest::dragEdgeScrollStopsOnCancel() {
    Scene s;
    s.view->zoomAt(4, s.view->labelWidth() + 12);
    s.view->setSelection({0});
    QSignalSpy moved(s.view, &QuickTimeline::groupMoveRequested);
    beginDrag(s, QPoint(400, qRound(s.view->clipRect(0).center().y())), QPoint(990, qRound(s.view->clipRect(0).center().y())));
    QTRY_VERIFY_WITH_TIMEOUT(s.view->contentOffset() > 0, 1000);
    QTest::keyClick(&s.window, Qt::Key_Escape);
    const double stoppedAt = s.view->contentOffset();
    QTest::qWait(50);
    QCOMPARE(s.view->contentOffset(), stoppedAt);
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, QPoint(990, 82));
    QCOMPARE(moved.count(), 0);
}

void QuickTimelineTest::activeTrimEdgeDoesNotSnapToItself() {
    Scene s;
    Project p;
    p.clips = {{"a.mp4", 0, 4}};
    s.view->setProject(p, {8});
    const auto r = s.view->clipRect(0);
    const QPoint from(qRound(r.right() - 3), qRound(r.center().y()));
    QTest::mousePress(&s.window, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&s.window, from + QPoint(20, 0), 1);
    QTest::mouseMove(&s.window, from + QPoint(5, 0), 1);
    QCOMPARE(s.view->trimPreview().value("outTime").toDouble(), 4.0);
    QTest::keyClick(&s.window, Qt::Key_Escape);
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, from);
}

void QuickTimelineTest::noOpReorderHasNoCommit() {
    Scene s;
    s.view->setSelection({0, 1});
    const auto from = s.view->clipRect(0).center().toPoint();
    const QPoint boundary(qRound(s.view->clipRect(2).left()), from.y());
    QSignalSpy moved(s.view, &QuickTimeline::groupMoveRequested);
    beginDrag(s, from, boundary);
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, boundary);
    QCOMPARE(moved.count(), 0);
}

void QuickTimelineTest::keyboardAndSkimmingKeepIndependentPositions() {
    Scene s;
    s.view->setPosition(60);
    QSignalSpy seek(s.view, &QuickTimeline::seekRequested);
    QSignalSpy skim(s.view, &QuickTimeline::skimRequested);
    QSignalSpy ended(s.view, &QuickTimeline::skimEnded);
    s.view->setSkimming(true);
    QTest::mouseMove(&s.window, s.view->clipRect(1).center().toPoint(), 1);
    QVERIFY(skim.count() >= 1);
    QCOMPARE(s.view->position(), 60);
    QCOMPARE(seek.count(), 0);
    s.view->setPlaying(true);
    QVERIFY(ended.count() >= 1);
    skim.clear();
    QTest::mouseMove(&s.window, s.view->clipRect(2).center().toPoint(), 1);
    QCOMPARE(skim.count(), 0);
    QTest::keyClick(&s.window, Qt::Key_Right);
    QCOMPARE(s.view->position(), 61);
    QTest::keyClick(&s.window, Qt::Key_PageUp);
    QCOMPARE(s.view->position(), 121);
    QTest::keyClick(&s.window, Qt::Key_Home);
    QCOMPARE(s.view->position(), 0);
    QTest::keyClick(&s.window, Qt::Key_End);
    QCOMPARE(s.view->position(), 719);
}

void QuickTimelineTest::lostGrabCancelsTrimAndScrub() {
    Scene s;
    QSignalSpy trimmed(s.view, &QuickTimeline::trimRequested);
    const auto initial = s.view->clipRect(0);
    const QPoint from(qRound(initial.right() - 3), qRound(initial.center().y()));
    beginDrag(s, from, from - QPoint(100, 0));
    s.view->ungrabMouse();
    QVERIFY(!s.view->trimPreview().value("active").toBool());
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, from - QPoint(100, 0));
    QCOMPARE(trimmed.count(), 0);
    QCOMPARE(s.view->clipRect(0), initial);
    s.view->setPosition(90);
    QSignalSpy finished(s.view, &QuickTimeline::scrubFinished);
    beginDrag(s, s.atFrame(120), s.atFrame(150));
    s.view->ungrabMouse();
    QCOMPARE(s.view->position(), 90);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).toBool(), true);
    QTest::mouseRelease(&s.window, Qt::LeftButton, Qt::NoModifier, s.atFrame(150));
}


void QuickTimelineTest::wheelPansAndModifiedWheelZoomsWithoutSeeking() {
    Scene s;
    s.view->zoomAt(4, s.view->labelWidth() + 12);
    QSignalSpy seek(s.view, &QuickTimeline::seekRequested);
    const QPoint point(500, 70);
    QWheelEvent pan(point, s.window.mapToGlobal(point), QPoint(-90, 0), QPoint(), Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    pan.setTimestamp(10);
    QCoreApplication::sendEvent(&s.window, &pan);
    QCOMPARE(s.view->contentOffset(), 90.0);
    QCoreApplication::processEvents();
    const int before = s.view->frameAt(point.x());
    const double scale = s.view->pixelsPerSecond();
    QWheelEvent zoom(point, s.window.mapToGlobal(point), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    zoom.setTimestamp(20);
    QCoreApplication::sendEvent(&s.window, &zoom);
    QVERIFY(s.view->pixelsPerSecond() > scale);
    QVERIFY(qAbs(s.view->frameAt(point.x()) - before) <= 1);
    QCOMPARE(seek.count(), 0);
}

void QuickTimelineTest::audioLanesAndNativeFileDrops() {
    Scene s;
    QSignalSpy selected(s.view, &QuickTimeline::audioSelected);
    const int audioTop = qRound(s.view->clipRect(0).bottom() + 8);
    QTest::mouseClick(&s.window, Qt::LeftButton, Qt::NoModifier, QPoint(500, audioTop + 20));
    QCOMPARE(selected.last().at(0).toInt(), 1);
    QTest::mouseClick(&s.window, Qt::LeftButton, Qt::NoModifier, QPoint(500, audioTop + 74));
    QCOMPARE(selected.last().at(0).toInt(), 2);
    QSignalSpy dropped(s.view, &QuickTimeline::filesDropped);
    QMimeData mime;
    const QString source=QFINDTESTDATA("fixtures/seek.mp4");
    mime.setUrls({QUrl::fromLocalFile(source), QUrl::fromLocalFile(source)});
    QDragEnterEvent enter(QPointF(300, 80), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&s.window, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(300, 80), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&s.window, &drop);
    QVERIFY(drop.isAccepted());
    QCOMPARE(dropped.count(), 1);
    QCOMPARE(dropped.first().at(0).toStringList(), QStringList({source,source}));
    mime.setUrls({QUrl("https://example.com/clip.mp4")});
    QDragEnterEvent remote(QPointF(300, 80), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&s.window, &remote);
    QVERIFY(!remote.isAccepted());
}

void QuickTimelineTest::renderedFilmstripUsesSourceTrimAndRotation() {
    Scene s;
    MediaCache cache;
    Project p;
    const QString rotation = QFINDTESTDATA("fixtures/rotation.mp4");
    p.clips = {{rotation, 0, 0.5, false, 90}};
    s.view->setProject(p, {1});
    s.view->setMediaCache(&cache);
    auto r = s.view->clipRect(0);
    const double bandTop = r.top() + QFontMetrics(s.view->font()).height() + 6;
    const double bandHeight = r.height() - (bandTop - r.top()) - 18;
    const QPoint bluePoint(qRound(r.left() + 45 - bandHeight * 9 / 16 / 4), qRound(bandTop + bandHeight / 4));
    QTRY_VERIFY_WITH_TIMEOUT(render(s.view).pixelColor(bluePoint).blue() > 180 && render(s.view).pixelColor(bluePoint).red() < 80, 15000);
    const QString timing = QFINDTESTDATA("fixtures/proxy-timing-30.mp4");
    p.clips = {{timing, 0.1, 0.3}};
    s.view->setProject(p, {0.4});
    r = s.view->clipRect(0);
    const double imageWidth = bandHeight * 16 / 9;
    const QRectF bounds(r.left() + 45 - imageWidth / 2, bandTop, imageWidth, bandHeight);
    // First tile samples source 7/60 seconds: nearest original 30 fps frame 4.
    QTRY_COMPARE_WITH_TIMEOUT(barcode(render(s.view), bounds), 4, 15000);
    s.view->setMediaCache(nullptr);
}

void QuickTimelineTest::appendWithAutoFitReusesCompletedFilmstrip() {
    Scene s;MediaCache cache;
    const auto path=QFINDTESTDATA("fixtures/seek.mp4");
    Project project;project.clips={{path,0,2}};
    s.view->setProject(project);s.view->setMediaCache(&cache);
    // This request finishes behind all filmstrip tiles, and covers the narrow final tile after fitting.
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path,119./60).isNull(),10000);
    auto *process=cache.findChild<QProcess*>();QVERIFY(process);
    QTRY_COMPARE(process->state(),QProcess::NotRunning);
    int oldSourceRequests=0;
    connect(process,&QProcess::started,this,[&]{if(process->arguments().contains(path))++oldSourceRequests;});
    const double scale=s.view->pixelsPerSecond();
    project.clips.append({"offline-append.mp4",0,2});
    s.view->setProject(project);
    QVERIFY(s.view->pixelsPerSecond()<scale);
    QTest::qWait(150);
    QCOMPARE(oldSourceRequests,0);
    s.view->setMediaCache(nullptr);
}

void QuickTimelineTest::filmstripReuseStaysInsideTileInterval() {
    Scene s;MediaCache cache;
    const auto path=QFINDTESTDATA("fixtures/proxy-timing-30.mp4");
    cache.thumbnail(path,14./60);
    QTRY_VERIFY_WITH_TIMEOUT(!cache.thumbnail(path,14./60).isNull(),10000);
    Project project;project.clips={{path,0,2}};
    s.view->setWidth(s.view->labelWidth()+36+2344);s.view->setProject(project);
    s.view->setPlaying(true);s.view->setMediaCache(&cache);
    const auto r=s.view->clipRect(0);
    const double top=r.top()+QFontMetrics(s.view->font()).height()+6;
    const double height=r.height()-(top-r.top())-18;
    const double width=height*16/9;
    const QRectF tile(r.left()+225-width/2,top,width,height);
    // Tile 2 covers [9.215, 13.823) source frames; cached frame 14 is outside it.
    QVERIFY(barcode(render(s.view),tile)!=7);
    s.view->setPlaying(false);
    QTRY_COMPARE_WITH_TIMEOUT(barcode(render(s.view),tile),6,10000);
    s.view->setMediaCache(nullptr);
}

void QuickTimelineTest::waveformsUseTrimAndMusicLoops() {
    QTemporaryDir directory;
    const QString path = directory.filePath("silence-then-tone.wav");
    QProcess encoder;
    QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) ffmpeg = QStandardPaths::findExecutable("ffmpeg", {"/opt/homebrew/bin", "/usr/local/bin"});
    QVERIFY(!ffmpeg.isEmpty());
    encoder.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "aevalsrc=if(lt(t\\,1)\\,0\\,0.75):s=2000:d=2.01", "-c:a", "pcm_s16le", path});
    QVERIFY(encoder.waitForFinished(10000));
    QVERIFY2(encoder.exitCode() == 0, encoder.readAllStandardError().constData());
    Scene s;
    MediaCache cache;
    Project p;
    p.clips = {{path, 1, 2}};
    s.view->setProject(p, {2.01});
    s.view->setMediaCache(&cache);
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(path), QString("ready"), 10000);
    const auto r = s.view->clipRect(0);
    const auto image = render(s.view);
    const QColor signal = image.pixelColor(qRound(r.left()) + 30, qRound(r.bottom()) - 5);
    const QColor background = image.pixelColor(qRound(r.left()) + 30, qRound(r.bottom()) - 16);
    QVERIFY(std::abs(signal.red() - background.red()) + std::abs(signal.green() - background.green()) + std::abs(signal.blue() - background.blue()) > 20);
    p.clips = {{"placeholder.mp4", 0, 4}};
    p.musicPath = path;
    s.view->setProject(p);
    const double audioTop = s.view->clipRect(0).bottom() + 8;
    const double musicTop = audioTop + 54 + 6;
    const double waveTop = musicTop + QFontMetrics(s.view->font()).height() + 5;
    const double waveHeight = 42 - QFontMetrics(s.view->font()).height() - 6;
    const int sampleY = qRound(waveTop + waveHeight / 2 - 5);
    const auto music = render(s.view);
    const auto colorAt = [&](double second) { return music.pixelColor(qRound(s.view->labelWidth() + 12 + second * s.view->pixelsPerSecond()), sampleY); };
    const auto silence = colorAt(0.5);
    const auto tone = colorAt(1.5);
    QVERIFY(std::abs(silence.red() - tone.red()) + std::abs(silence.green() - tone.green()) + std::abs(silence.blue() - tone.blue()) > 20);
    QVERIFY(qAbs(colorAt(2.5).lightness() - silence.lightness()) < 5);
    QVERIFY(qAbs(colorAt(3.5).lightness() - tone.lightness()) < 5);
    p.clips={{path,0,1},{"long.mp4",0,120}};p.musicPath.clear();
    s.view->setProject(p);s.view->setSelection({0});
    for(int i=0;i<8;++i)s.view->zoomIn();
    s.view->setContentOffset(s.view->maximumOffset());
    QVERIFY(s.view->clipRect(0).right()<s.view->labelWidth());
    QVERIFY(!s.view->property("selectionMediaStatus").toString().contains("Loading waveform"));
    s.view->setMediaCache(nullptr);
}

void QuickTimelineTest::waveformScaleClampsFiniteValues() {
    Scene s;
    QSignalSpy changed(s.view, &QuickTimeline::waveformScaleChanged);
    QCOMPARE(s.view->property("waveformScale").toDouble(), 1.0);
    QVERIFY(s.view->setProperty("waveformScale", 2.0));
    QCOMPARE(s.view->property("waveformScale").toDouble(), 2.0);
    QVERIFY(s.view->setProperty("waveformScale", 99.0));
    QCOMPARE(s.view->property("waveformScale").toDouble(), 4.0);
    QVERIFY(s.view->setProperty("waveformScale", std::numeric_limits<double>::infinity()));
    QCOMPARE(s.view->property("waveformScale").toDouble(), 4.0);
    QVERIFY(s.view->setProperty("waveformScale", std::numeric_limits<double>::quiet_NaN()));
    QCOMPARE(s.view->property("waveformScale").toDouble(), 4.0);
    QVERIFY(s.view->setProperty("waveformScale", -10.0));
    QCOMPARE(s.view->property("waveformScale").toDouble(), 1.0);
    QCOMPARE(changed.count(), 3);
}

void QuickTimelineTest::waveformScaleChangesOnlyPaintedAmplitude() {
    QTemporaryDir directory;
    const QString path = directory.filePath("half-amplitude.wav");
    QProcess encoder;
    QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) ffmpeg = QStandardPaths::findExecutable("ffmpeg", {"/opt/homebrew/bin", "/usr/local/bin"});
    QVERIFY(!ffmpeg.isEmpty());
    encoder.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "aevalsrc=0.5:s=2000:d=1", "-c:a", "pcm_s16le", path});
    QVERIFY(encoder.waitForFinished(10000));
    QVERIFY2(encoder.exitCode() == 0, encoder.readAllStandardError().constData());
    Scene s;
    MediaCache cache;
    Project p;
    p.clips = {{path, 0, 1}};
    p.musicPath = path;
    p.musicDb = -18;
    s.view->setProject(p, {1});
    s.view->setPosition(30);
    s.view->setSelection({0});
    s.view->setMediaCache(&cache);
    QTRY_COMPARE_WITH_TIMEOUT(cache.waveformState(path), QString("ready"), 10000);
    const auto peaks = cache.waveform(path);
    const auto rect = s.view->clipRect(0);
    const double scale = s.view->pixelsPerSecond();
    const int end = s.view->frameAt(10000);
    const auto normal = render(s.view);
    QSignalSpy viewport(s.view, &QuickTimeline::viewportChanged);
    QSignalSpy seek(s.view, &QuickTimeline::seekRequested);
    QVERIFY(s.view->setProperty("waveformScale", 2.0));
    const auto doubled = render(s.view);
    const QPoint sample(qRound(rect.left()) + 30, qRound(rect.bottom()) - 15);
    // A half-scale peak rises under four pixels at 1x and seven pixels at 2x.
    QVERIFY(normal.pixelColor(sample) != doubled.pixelColor(sample));
    QCOMPARE(s.view->clipRect(0), rect);
    QCOMPARE(s.view->pixelsPerSecond(), scale);
    QCOMPARE(s.view->frameAt(10000), end);
    QCOMPARE(s.view->position(), 30);
    QCOMPARE(s.view->selection(), QVariantList{QVariant(0)});
    QCOMPARE(cache.waveform(path), peaks);
    QCOMPARE(viewport.count(), 0);
    QCOMPARE(seek.count(), 0);
    // Saturating the display scale still leaves all pixels outside waveform bands unchanged.
    s.view->setProperty("waveformScale", 4.0);
    const auto maximum = render(s.view);
    const auto clipOutside = QRect(0, 0, normal.width(), qRound(rect.bottom()) - 17);
    QCOMPARE(maximum.copy(clipOutside), normal.copy(clipOutside));
    s.view->setMediaCache(nullptr);
}

void QuickTimelineTest::captionsHaveIndependentIntervalsAndFrameEdgeGestures() {
    Scene s;auto p=sequence();TextOverlay a;a.startFrame=30;a.durationFrames=60;TextOverlay b=a;b.startFrame=60;p.captions={a,b};s.view->setProject(p,{8,8,8});s.view->fitToWidth();
    QVERIFY(!s.view->captionRect(0).isEmpty());QVERIFY(s.view->captionRect(1).top()>s.view->captionRect(0).top());
    QSignalSpy timing(s.view,&QuickTimeline::captionTimingRequested);QSignalSpy selected(s.view,&QuickTimeline::captionSelected);
    const auto center=s.view->captionRect(0).center().toPoint();const auto delta=QPoint(qRound(s.view->pixelsPerSecond()*.5),0);beginDrag(s,center,center+delta,Qt::AltModifier);QTest::mouseRelease(&s.window,Qt::LeftButton,Qt::AltModifier,center+delta);QCOMPARE(selected.count(),1);QCOMPARE(timing.count(),1);QVERIFY(std::abs(timing[0][1].toInt()-60)<=1);QCOMPARE(timing[0][2].toInt(),60);
    timing.clear();const auto edge=QPoint(qRound(s.view->captionRect(0).right()-2),qRound(s.view->captionRect(0).center().y()));beginDrag(s,edge,edge-QPoint(200,0),Qt::AltModifier);QTest::mouseRelease(&s.window,Qt::LeftButton,Qt::AltModifier,edge-QPoint(200,0));QCOMPARE(timing.count(),1);QVERIFY(timing[0][2].toInt()>=1);
    timing.clear();beginDrag(s,center,center+delta,Qt::AltModifier);QTest::keyClick(&s.window,Qt::Key_Escape);QTest::mouseRelease(&s.window,Qt::LeftButton,Qt::AltModifier,center+delta);QCOMPARE(timing.count(),0);
}
void QuickTimelineTest::captionExtentAndDenseSequentialRowsRemainAccessible() {
    Scene s;auto p=sequence();for(int i=0;i<1000;++i){TextOverlay cue;cue.startFrame=i*120;cue.durationFrames=60;p.captions.append(cue);}s.view->setProject(p,{8,8,8});s.view->fitToWidth();
    QCOMPARE(s.view->captionRect(0).top(),s.view->captionRect(999).top());QVERIFY(s.view->captionRect(999).right()<=s.view->width());QVERIFY(!render(s.view).isNull());
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setFont(QFont("Helvetica Neue", 13));
    QuickTimelineTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "quick_timeline_test.moc"
