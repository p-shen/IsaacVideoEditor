#include "timeline_widget.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QSignalSpy>
#include <QScrollBar>
#include <QUrl>
#include <QtTest>

namespace {
Project sequence() {
    Project p;
    p.clips = {{"first.mp4", 0, 4}, {"second.mp4", 0, 4}, {"third.mp4", 0, 4}};
    return p;
}
int left(const TimelineWidget &view) {
    return std::max(96, view.fontMetrics().horizontalAdvance("Narration") + 30) + 12;
}
int videoY(const TimelineWidget &view) {
    return std::max(32, view.fontMetrics().height() + 12) + 26;
}
double fitScale(const TimelineWidget &view, double seconds) {
    return (view.viewport()->width() - left(view) - 24) / (seconds > 0 ? std::max(1.0, seconds) : 10.0);
}
void drag(TimelineWidget &view, QPoint from, QPoint to) {
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(view.viewport(), to);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, to);
}
}

class TimelineWidgetTest : public QObject {
    Q_OBJECT
private slots:
    void crossfadePositionsFollowProject();
    void splitJoinHasNoCrossfadeOverlap();
    void frameKeyboardSeeking();
    void rulerSeeking();
    void clipSelectionAndReorder();
    void trimUsesSelectedSourceBounds();
    void trimCannotRemoveFinalFrame();
    void zoomKeepsKeyboardPlayheadVisible();
    void shortEditFitUsesAvailableWidth();
    void zoomKeepsPausedPlayheadVisible();
    void emptyTimelineAndFileDrops();
};

void TimelineWidgetTest::crossfadePositionsFollowProject() {
    TimelineWidget view;
    Project p;
    p.clips = {{"a.mp4", 0, 2}, {"b.mp4", 0, 3}, {"c.mp4", 0, 4}};
    p.crossfadeFrames = 30;
    view.setProject(p);
    QCOMPARE(view.clipStartFrame(0), 0);
    QCOMPARE(view.clipStartFrame(1), 90);
    QCOMPARE(view.clipStartFrame(2), 240);
    QCOMPARE(view.clipStartFrame(-1), -1);
    QCOMPARE(view.clipStartFrame(3), -1);
}

void TimelineWidgetTest::splitJoinHasNoCrossfadeOverlap() {
    TimelineWidget view;
    Project p;
    p.clips = {{"a.mp4", 0, 2}, {"a.mp4", 2, 5, true}, {"c.mp4", 0, 4}};
    p.crossfadeFrames = 30;
    view.setProject(p);
    QCOMPARE(view.clipStartFrame(0), 0);
    QCOMPARE(view.clipStartFrame(1), 120);
    QCOMPARE(view.clipStartFrame(2), 270);
}

void TimelineWidgetTest::frameKeyboardSeeking() {
    TimelineWidget view;
    view.setProject(sequence());
    view.setPosition(60);
    QSignalSpy seek(&view, &TimelineWidget::seekRequested);
    QTest::keyClick(&view, Qt::Key_Right);
    QCOMPARE(seek.count(), 1);
    QCOMPARE(seek.takeLast().at(0).toInt(), 61);
    QTest::keyClick(&view, Qt::Key_PageUp);
    QCOMPARE(seek.count(), 1);
    QCOMPARE(seek.takeLast().at(0).toInt(), 121);
    QTest::keyClick(&view, Qt::Key_PageDown);
    QCOMPARE(seek.count(), 1);
    QCOMPARE(seek.takeLast().at(0).toInt(), 61);
    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(seek.count(), 1);
    QCOMPARE(seek.takeLast().at(0).toInt(), 0);
    QTest::keyClick(&view, Qt::Key_End);
    QCOMPARE(seek.count(), 1);
    QCOMPARE(seek.takeLast().at(0).toInt(), 719);
}

void TimelineWidgetTest::rulerSeeking() {
    TimelineWidget view;
    view.resize(1000, 270);
    view.show();
    QTest::qWait(1);
    view.setProject(sequence());
    QSignalSpy seek(&view, &TimelineWidget::seekRequested);
    const int x = qRound(left(view) + 2 * fitScale(view, 12));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(x, 12));
    QCOMPARE(seek.count(), 1);
    QVERIFY(qAbs(seek.last().at(0).toInt() - 120) <= 1);
}

void TimelineWidgetTest::clipSelectionAndReorder() {
    TimelineWidget view;
    view.resize(1000, 270);
    view.show();
    QTest::qWait(1);
    view.setProject(sequence());
    QSignalSpy selection(&view, &TimelineWidget::clipSelected);
    QSignalSpy moved(&view, &TimelineWidget::clipMoved);
    QSignalSpy seek(&view, &TimelineWidget::seekRequested);
    const double scale = fitScale(view, 12);
    const QPoint from(qRound(left(view) + scale), videoY(view));
    const QPoint to(qRound(left(view) + 11 * scale), videoY(view));
    drag(view, from, to);
    QCOMPARE(selection.count(), 1);
    QCOMPARE(selection.first().at(0).toInt(), 0);
    QCOMPARE(seek.count(), 1);
    QVERIFY(qAbs(seek.first().at(0).toInt() - 60) <= 1);
    QCOMPARE(moved.count(), 1);
    QCOMPARE(moved.first().at(0).toInt(), 0);
    QCOMPARE(moved.first().at(1).toInt(), 2);
}

void TimelineWidgetTest::trimUsesSelectedSourceBounds() {
    TimelineWidget view;
    view.resize(1000, 270);
    view.show();
    QTest::qWait(1);
    Project p;
    p.clips = {{"same.mp4", 0, 4}, {"same.mp4", 1, 3}};
    view.setProject(p, {9, 5});
    view.setSelectedClip(1);
    QSignalSpy trimmed(&view, &TimelineWidget::trimRequested);
    const double scale = fitScale(view, 6);
    const int startX = qRound(left(view) + 4 * scale);
    drag(view, QPoint(startX + 3, videoY(view)), QPoint(left(view) + 1, videoY(view)));
    QCOMPARE(trimmed.count(), 1);
    QCOMPARE(trimmed.last().at(0).toInt(), 1);
    QCOMPARE(trimmed.last().at(1).toDouble(), 0.0);
    QCOMPARE(trimmed.last().at(2).toDouble(), 3.0);

    trimmed.clear();
    view.setProject(p, {9, 5});
    view.setSelectedClip(1);
    view.zoomOut();
    const int endX = qRound(left(view) + 6 * scale / 1.5);
    drag(view, QPoint(endX - 3, videoY(view)), QPoint(view.viewport()->width() - 3, videoY(view)));
    QCOMPARE(trimmed.count(), 1);
    QCOMPARE(trimmed.last().at(0).toInt(), 1);
    QCOMPARE(trimmed.last().at(1).toDouble(), 1.0);
    QCOMPARE(trimmed.last().at(2).toDouble(), 5.0);
}

void TimelineWidgetTest::emptyTimelineAndFileDrops() {
    TimelineWidget view;
    view.show();
    QSignalSpy seek(&view, &TimelineWidget::seekRequested);
    QTest::keyClick(&view, Qt::Key_Right);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(200, 12));
    QCOMPARE(seek.count(), 0);
    QSignalSpy dropped(&view, &TimelineWidget::filesDropped);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile("/tmp/first.mp4"), QUrl::fromLocalFile("/tmp/second.mov")});
    QDragEnterEvent enter(QPointF(200, 80), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(200, 80), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &drop);
    QVERIFY(drop.isAccepted());
    QCOMPARE(dropped.count(), 1);
    QCOMPARE(dropped.first().at(0).toStringList().size(), 2);
    QMimeData remote;
    remote.setUrls({QUrl("https://example.com/clip.mp4")});
    QDragEnterEvent remoteEnter(QPointF(200, 80), Qt::CopyAction, &remote, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &remoteEnter);
    QVERIFY(!remoteEnter.isAccepted());
}

void TimelineWidgetTest::trimCannotRemoveFinalFrame() {
    TimelineWidget view;
    view.resize(1000, 270);
    view.show();
    QTest::qWait(1);
    Project p;
    p.clips = {{"one.mp4", 1, 3}};
    view.setProject(p, {5});
    view.setSelectedClip(0);
    QSignalSpy trimmed(&view, &TimelineWidget::trimRequested);
    QSignalSpy seek(&view, &TimelineWidget::seekRequested);
    const double scale = fitScale(view, 2);
    drag(view, QPoint(left(view) + 3, videoY(view)), QPoint(view.viewport()->width() - 2, videoY(view)));
    QCOMPARE(trimmed.count(), 1);
    QCOMPARE(qRound(trimmed.last().at(1).toDouble() * Project::fps), 179);
    QCOMPARE(qRound(trimmed.last().at(2).toDouble() * Project::fps), 180);
    QCOMPARE(seek.count(), 0);
    trimmed.clear();
    view.setProject(p, {5});
    view.setSelectedClip(0);
    drag(view, QPoint(qRound(left(view) + 2 * scale) - 3, videoY(view)), QPoint(left(view) - 20, videoY(view)));
    QCOMPARE(trimmed.count(), 1);
    QCOMPARE(qRound(trimmed.last().at(1).toDouble() * Project::fps), 60);
    QCOMPARE(qRound(trimmed.last().at(2).toDouble() * Project::fps), 61);
}

void TimelineWidgetTest::zoomKeepsKeyboardPlayheadVisible() {
    TimelineWidget view;
    view.resize(1000, 270);
    view.show();
    QTest::qWait(1);
    view.setProject(sequence());
    view.zoomIn();
    view.zoomIn();
    QTest::keyClick(&view, Qt::Key_End);
    QVERIFY(view.horizontalScrollBar()->value() > 0);
    view.fitToWidth();
    QCOMPARE(view.horizontalScrollBar()->value(), 0);
    QCOMPARE(view.horizontalScrollBar()->maximum(), 0);
}

void TimelineWidgetTest::shortEditFitUsesAvailableWidth() {
    TimelineWidget view;
    view.resize(1000, 270);
    view.show();
    QTest::qWait(1);
    Project p;
    p.clips = {{"short.mp4", 0, 2}};
    view.setProject(p, {2});
    view.fitToWidth();
    QSignalSpy seek(&view, &TimelineWidget::seekRequested);
    const int x = qRound(left(view) + 1.5 * fitScale(view, 2));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(x, 12));
    QCOMPARE(seek.count(), 1);
    QVERIFY(qAbs(seek.first().at(0).toInt() - 90) <= 1);
    QCOMPARE(view.horizontalScrollBar()->maximum(), 0);
}

void TimelineWidgetTest::zoomKeepsPausedPlayheadVisible() {
    TimelineWidget view;
    view.resize(1000, 270);
    view.show();
    QTest::qWait(1);
    view.setProject(sequence());
    view.setPosition(719);
    QCOMPARE(view.horizontalScrollBar()->value(), 0);
    view.zoomIn();
    QVERIFY(view.horizontalScrollBar()->value() > 0);
    view.zoomIn();
    view.setPosition(60);
    view.zoomOut();
    const double pixelsPerSecond = fitScale(view, 12) * 1.5;
    const double playheadX = left(view) + pixelsPerSecond - view.horizontalScrollBar()->value();
    QVERIFY(playheadX >= left(view) - 1);
    QVERIFY(playheadX <= view.viewport()->width() - 23);
}

QTEST_MAIN(TimelineWidgetTest)
#include "timeline_widget_test.moc"
