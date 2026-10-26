#include "timeline_widget.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QToolTip>
#include <QUrl>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
QColor blend(const QColor &a, const QColor &b, double amount) {
    return QColor::fromRgbF(a.redF() * (1 - amount) + b.redF() * amount,
                           a.greenF() * (1 - amount) + b.greenF() * amount,
                           a.blueF() * (1 - amount) + b.blueF() * amount);
}
QString timecode(int frame) {
    const int seconds = std::max(0, frame) / Project::fps;
    return QString("%1:%2:%3").arg(seconds / 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0')).arg(frame % Project::fps, 2, 10, QChar('0'));
}
}

TimelineWidget::TimelineWidget(QWidget *parent) : QAbstractScrollArea(parent) {
    setObjectName("timeline");
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    viewport()->setMouseTracking(true);
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName("Editing timeline");
    setAccessibleDescription("Click the ruler to seek. Select a clip, then drag its edges to trim or its center to reorder. Arrow keys move one frame; Page keys move one second. The clip list and inspector provide alternatives to dragging.");
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
}

int TimelineWidget::labelWidth() const {
    return std::max(96, fontMetrics().horizontalAdvance("Narration") + 30);
}
int TimelineWidget::rulerHeight() const { return std::max(32, fontMetrics().height() + 12); }
int TimelineWidget::videoHeight() const { return std::max(64, fontMetrics().height() * 2 + 18); }
int TimelineWidget::audioHeight() const { return std::max(44, fontMetrics().height() + 20); }

double TimelineWidget::xForFrame(int frame) const {
    return labelWidth() + 12 + frame * pixelsPerSecond_ / Project::fps - horizontalScrollBar()->value();
}
int TimelineWidget::frameAtX(double x) const {
    const double frame = (x - labelWidth() - 12 + horizontalScrollBar()->value()) * Project::fps / pixelsPerSecond_;
    return qBound(0, qRound(frame), std::max(0, project_.durationFrames() - 1));
}
int TimelineWidget::clipStartFrame(int index) const {
    return project_.clipStartFrame(index);
}
QRectF TimelineWidget::clipRect(int index) const {
    if (index < 0 || index >= project_.clips.size()) return {};
    return QRectF(xForFrame(clipStartFrame(index)), rulerHeight() + 10,
                  std::max(1.0, project_.clipFrames(project_.clips[index]) * pixelsPerSecond_ / Project::fps),
                  videoHeight() - 12);
}
int TimelineWidget::clipAt(const QPointF &point) const {
    if (point.x() < labelWidth()) return -1;
    if (selected_ >= 0 && clipRect(selected_).contains(point)) return selected_;
    for (int i = int(project_.clips.size()) - 1; i >= 0; --i)
        if (clipRect(i).contains(point)) return i;
    return -1;
}
int TimelineWidget::reorderTarget(double x) const {
    int target = 0;
    for (int i = 0; i < project_.clips.size(); ++i) {
        if (i != gestureClip_ && x > clipRect(i).center().x()) ++target;
    }
    return qBound(0, target, std::max(0, int(project_.clips.size()) - 1));
}

void TimelineWidget::setProject(const Project &project, const QVector<double> &sourceDurations,
                                double voiceDurationSeconds) {
    project_ = project;
    sourceDurations_ = sourceDurations;
    voiceDurationSeconds_ = std::isfinite(voiceDurationSeconds) ? std::max(0.0, voiceDurationSeconds) : 0;
    position_ = qBound(0, position_, std::max(0, project_.durationFrames() - 1));
    if (selected_ >= project_.clips.size()) selected_ = -1;
    resetGesture();
    updateRange();
    viewport()->update();
}
void TimelineWidget::setPosition(int frame) {
    position_ = qBound(0, frame, std::max(0, project_.durationFrames() - 1));
    const double x = xForFrame(position_);
    if (viewport()->width() > labelWidth() + 48) {
        if (x < labelWidth() + 12)
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(x - labelWidth() - 12));
        else if (x > viewport()->width() - 24)
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(x - viewport()->width() + 24));
    }
    viewport()->update();
}
void TimelineWidget::setSelectedClip(int index) {
    selected_ = index >= 0 && index < project_.clips.size() ? index : -1;
    viewport()->update();
}
void TimelineWidget::updateRange() {
    const int available = std::max(1, viewport()->width() - labelWidth() - 36);
    if (autoFit_) {
        const double seconds = project_.durationFrames() / double(Project::fps);
        pixelsPerSecond_ = available / (project_.clips.isEmpty() ? 10.0 : std::max(1.0, seconds));
    }
    const double content = project_.durationFrames() * pixelsPerSecond_ / Project::fps;
    horizontalScrollBar()->setRange(0, std::max(0, int(std::ceil(content)) - available));
    horizontalScrollBar()->setPageStep(available);
    horizontalScrollBar()->setSingleStep(std::max(1, qRound(pixelsPerSecond_)));
    setMinimumHeight(rulerHeight() + videoHeight() + 2 * audioHeight() + 14);
}
void TimelineWidget::zoomIn() {
    autoFit_ = false;
    pixelsPerSecond_ = std::min(1200.0, pixelsPerSecond_ * 1.5);
    updateRange();
    setPosition(position_);
}
void TimelineWidget::zoomOut() {
    autoFit_ = false;
    pixelsPerSecond_ = std::max(0.2, pixelsPerSecond_ / 1.5);
    updateRange();
    setPosition(position_);
}
void TimelineWidget::fitToWidth() {
    autoFit_ = true;
    horizontalScrollBar()->setValue(0);
    updateRange();
    viewport()->update();
}
void TimelineWidget::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    updateRange();
}

void TimelineWidget::paintEvent(QPaintEvent *) {
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    const auto colors = palette();
    const QColor base = colors.color(QPalette::Base);
    const QColor surface = colors.color(QPalette::Window);
    const QColor text = colors.color(QPalette::Text);
    const QColor muted = colors.color(QPalette::PlaceholderText);
    const QColor accent = colors.color(QPalette::Highlight);
    const QColor divider = blend(base, text, 0.22);
    const int labels = labelWidth();
    const int ruler = rulerHeight();
    const int video = videoHeight();
    const int audio = audioHeight();
    p.fillRect(viewport()->rect(), base);
    p.fillRect(QRect(0, 0, viewport()->width(), ruler), surface);
    p.fillRect(QRect(0, ruler, labels, viewport()->height() - ruler), surface);
    p.setPen(divider);
    for (int y : {ruler, ruler + video, ruler + video + audio, ruler + video + 2 * audio})
        p.drawLine(0, y, viewport()->width(), y);
    p.drawLine(labels, 0, labels, viewport()->height());
    p.setPen(muted);
    p.drawText(QRect(12, 0, labels - 20, ruler), Qt::AlignVCenter, "TRACKS");
    p.setPen(text);
    p.drawText(QRect(12, ruler, labels - 20, video), Qt::AlignVCenter, "Video");
    p.drawText(QRect(12, ruler + video, labels - 20, audio), Qt::AlignVCenter, "Narration");
    p.drawText(QRect(12, ruler + video + audio, labels - 20, audio), Qt::AlignVCenter, "Music");
    p.save();
    p.setClipRect(QRect(labels + 1, 0, std::max(0, viewport()->width() - labels - 1), viewport()->height()));
    const double intervals[] = {1.0 / Project::fps, 0.1, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600};
    double interval = 3600;
    const int spacing = std::max(74, fontMetrics().horizontalAdvance("00:00:00") + 14);
    for (double candidate : intervals) {
        if (candidate * pixelsPerSecond_ >= spacing) { interval = candidate; break; }
    }
    const double start = std::floor(horizontalScrollBar()->value() / pixelsPerSecond_ / interval) * interval;
    const double end = (horizontalScrollBar()->value() + viewport()->width() - labels) / pixelsPerSecond_;
    for (double sec = start; sec <= end; sec += interval) {
        const int x = qRound(xForFrame(qRound(sec * Project::fps)));
        p.setPen(divider);
        p.drawLine(x, ruler - 8, x, viewport()->height());
        p.setPen(muted);
        p.drawText(QRect(x + 5, 0, spacing, ruler - 5), Qt::AlignVCenter, timecode(qRound(sec * Project::fps)));
    }
    if (project_.clips.isEmpty()) {
        p.setPen(QPen(dropActive_ ? accent : divider, 1.5, Qt::DashLine));
        p.setBrush(dropActive_ ? blend(base, accent, 0.12) : base);
        const QRectF empty(labels + 12, ruler + 10, std::max(1, viewport()->width() - labels - 24), video - 20);
        p.drawRoundedRect(empty, 5, 5);
        p.setPen(muted);
        p.drawText(empty.adjusted(12, 0, -12, 0), Qt::AlignCenter | Qt::TextWordWrap,
                   "Drop video clips here to start your edit");
    }
    for (int i = 0; i < project_.clips.size(); ++i) {
        QRectF rect = clipRect(i);
        const Clip &original = project_.clips[i];
        const bool trimming = i == gestureClip_ && dragging_ &&
            (gesture_ == Gesture::TrimStart || gesture_ == Gesture::TrimEnd);
        if (trimming) {
            if (gesture_ == Gesture::TrimStart)
                rect.setLeft(rect.left() + (trimmed_.inSeconds - original.inSeconds) * pixelsPerSecond_);
            else rect.setRight(rect.right() + (trimmed_.outSeconds - original.outSeconds) * pixelsPerSecond_);
        }
        if (rect.right() < labels || rect.left() > viewport()->width()) continue;
        p.setBrush(blend(base, accent, i == selected_ ? 0.24 : 0.12));
        p.setPen(QPen(i == selected_ ? accent : blend(base, text, 0.46), i == selected_ ? 2 : 1));
        p.drawRoundedRect(rect.adjusted(1, 1, -1, -1), 5, 5);
        if (rect.width() > 24) {
            const double inset = 12 + project_.transitionFramesBefore(i) * pixelsPerSecond_ / Project::fps;
            const QRectF nameRect = rect.adjusted(inset, 5, -12, -(rect.height() / 2));
            p.setPen(text);
            p.drawText(nameRect, Qt::AlignVCenter, fontMetrics().elidedText(
                QString::number(i + 1) + "  " + QFileInfo(original.path).fileName(), Qt::ElideRight, std::max(0, int(nameRect.width()))));
            p.setPen(muted);
            const int frames = trimming ? project_.clipFrames(trimmed_) : project_.clipFrames(original);
            p.drawText(rect.adjusted(inset, rect.height() / 2, -12, -4), Qt::AlignVCenter,
                       fontMetrics().elidedText(timecode(frames), Qt::ElideRight, std::max(0, int(rect.width() - inset - 12))));
        }
        if (i == selected_ && rect.width() >= 18) {
            p.setPen(QPen(accent, 3, Qt::SolidLine, Qt::RoundCap));
            for (double x : {rect.left() + 5, rect.right() - 5})
                p.drawLine(QPointF(x, rect.center().y() - 8), QPointF(x, rect.center().y() + 8));
        }
    }
    if (project_.effectiveCrossfadeFrames() > 0) {
        p.setPen(QPen(accent, 1.5));
        for (int i = 1; i < project_.clips.size(); ++i) {
            const int overlap = project_.transitionFramesBefore(i);
            if (overlap == 0) continue;
            const QRectF r(xForFrame(clipStartFrame(i)), ruler + 10,
                           overlap * pixelsPerSecond_ / Project::fps, video - 12);
            if (r.width() >= 3) {
                p.fillRect(r, blend(base, accent, 0.3));
                p.drawLine(r.topLeft(), r.bottomRight());
                p.drawLine(r.bottomLeft(), r.topRight());
            }
        }
    }
    const auto audioBlock = [&](int lane, const QString &path, int startFrame, int length, const QString &detail) {
        const int y = ruler + video + (lane - 1) * audio;
        if (path.isEmpty() || length <= 0) {
            p.setPen(muted);
            const QString hint = path.isEmpty() ? (lane == 1 ? "Add narration in Audio" : "Add music in Audio")
                : (startFrame >= project_.durationFrames() ? "Narration starts after this edit" : "Narration duration unavailable");
            p.drawText(QRect(labels + 16, y, viewport()->width() - labels - 24, audio), Qt::AlignVCenter, hint);
            return;
        }
        const QRectF r(xForFrame(startFrame), y + 7, length * pixelsPerSecond_ / Project::fps, audio - 14);
        QColor tint = accent.toHsl();
        tint.setHslF(std::fmod(tint.hslHueF() + (lane == 1 ? 0.38 : 0.58), 1.0), 0.44, 0.54);
        p.setPen(QPen(blend(text, tint, 0.5), 1));
        p.setBrush(blend(base, tint, 0.18));
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
        p.setPen(text);
        p.drawText(r.adjusted(10, 0, -10, 0), Qt::AlignVCenter, fontMetrics().elidedText(
            QFileInfo(path).fileName() + detail, Qt::ElideRight, std::max(0, int(r.width() - 20))));
    };
    const int voiceStart = qRound(project_.voiceStartSeconds * Project::fps);
    const int voiceLength = std::max(0, std::min(project_.durationFrames() - voiceStart,
        qRound(voiceDurationSeconds_ * Project::fps)));
    audioBlock(1, project_.voicePath, voiceStart, voiceLength, "");
    audioBlock(2, project_.musicPath, 0, project_.durationFrames(), "  ·  Loops to end");
    if (dragging_ && gesture_ == Gesture::Clip && moveTarget_ != gestureClip_) {
        const int boundary = moveTarget_ > gestureClip_ ? moveTarget_ + 1 : moveTarget_;
        const double x = boundary >= project_.clips.size()
            ? xForFrame(project_.durationFrames()) : xForFrame(clipStartFrame(boundary));
        p.setPen(QPen(accent, 3));
        p.drawLine(QPointF(x, ruler + 4), QPointF(x, ruler + video - 3));
        const QRectF ghost(lastPoint_.x() - 45, ruler + video + 4, 110, audio - 8);
        p.setBrush(surface);
        p.setPen(accent);
        p.drawRoundedRect(ghost, 4, 4);
        p.drawText(ghost.adjusted(5, 0, -5, 0), Qt::AlignCenter, QString("Move to %1").arg(moveTarget_ + 1));
    }
    if (!project_.clips.isEmpty()) {
        const double x = xForFrame(position_);
        p.setPen(QPen(accent, 1.5));
        p.drawLine(QPointF(x, ruler - 8), QPointF(x, ruler + video + 2 * audio));
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(x - 5, 0), QPointF(x + 5, 0), QPointF(x + 5, 10),
                                QPointF(x, 15), QPointF(x - 5, 10)}));
    }
    p.restore();
    if (hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(accent, 2));
        p.drawRect(viewport()->rect().adjusted(1, 1, -2, -2));
    }
}

void TimelineWidget::requestSeek(double x) {
    if (project_.clips.isEmpty()) return;
    position_ = frameAtX(x);
    emit seekRequested(position_);
    viewport()->update();
}
void TimelineWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) { QAbstractScrollArea::mousePressEvent(event); return; }
    setFocus(Qt::MouseFocusReason);
    const QPointF point = event->position();
    if (point.x() <= labelWidth()) return;
    pressPoint_ = lastPoint_ = point.toPoint();
    const int row = clipAt(point);
    if (row >= 0) {
        const bool wasSelected = row == selected_;
        const QRectF r = clipRect(row);
        selected_ = row;
        gestureClip_ = row;
        trimmed_ = project_.clips[row];
        gesture_ = Gesture::Clip;
        if (wasSelected && r.width() >= 18) {
            if (point.x() - r.left() <= 9) gesture_ = Gesture::TrimStart;
            else if (r.right() - point.x() <= 9) gesture_ = Gesture::TrimEnd;
        }
        // Selection may synchronously refresh the widget through the inspector.
        const Gesture action = gesture_;
        emit clipSelected(row);
        gesture_ = action;
        gestureClip_ = row;
        pressPoint_ = lastPoint_ = point.toPoint();
        trimmed_ = project_.clips[row];
        viewport()->update();
        if (action == Gesture::Clip) requestSeek(point.x());
        return;
    }
    const int y = qRound(point.y());
    if (y >= rulerHeight() + videoHeight() && y < rulerHeight() + videoHeight() + 2 * audioHeight()) {
        emit audioSelected(y < rulerHeight() + videoHeight() + audioHeight() ? 1 : 2);
        return;
    }
    gesture_ = Gesture::Seek;
    requestSeek(point.x());
}
void TimelineWidget::mouseMoveEvent(QMouseEvent *event) {
    lastPoint_ = event->position().toPoint();
    if (gesture_ == Gesture::Seek) { requestSeek(event->position().x()); return; }
    if (gestureClip_ >= 0 && gestureClip_ < project_.clips.size()) {
        if (!dragging_ && (lastPoint_ - pressPoint_).manhattanLength() >= QApplication::startDragDistance())
            dragging_ = true;
        if (dragging_) {
            const Clip &original = project_.clips[gestureClip_];
            const int deltaFrames = qRound((lastPoint_.x() - pressPoint_.x()) * Project::fps / pixelsPerSecond_);
            trimmed_ = original;
            if (gesture_ == Gesture::TrimStart) {
                const int in = qBound(0, qRound(original.inSeconds * Project::fps) + deltaFrames,
                                      qRound(original.outSeconds * Project::fps) - 1);
                trimmed_.inSeconds = in / double(Project::fps);
            } else if (gesture_ == Gesture::TrimEnd) {
                const double sourceDuration = sourceDurations_.value(gestureClip_, original.outSeconds);
                const int maximum = qRound(std::max(original.outSeconds,
                    std::isfinite(sourceDuration) ? sourceDuration : original.outSeconds) * Project::fps);
                const int out = qBound(qRound(original.inSeconds * Project::fps) + 1,
                    qRound(original.outSeconds * Project::fps) + deltaFrames, maximum);
                trimmed_.outSeconds = out / double(Project::fps);
            } else if (gesture_ == Gesture::Clip) moveTarget_ = reorderTarget(lastPoint_.x());
            if (gesture_ == Gesture::Clip) viewport()->setCursor(Qt::ClosedHandCursor);
            viewport()->update();
        }
        return;
    }
    const int row = clipAt(event->position());
    if (row == selected_ && row >= 0 && clipRect(row).width() >= 18 &&
        (event->position().x() - clipRect(row).left() <= 9 || clipRect(row).right() - event->position().x() <= 9))
        viewport()->setCursor(Qt::SizeHorCursor);
    else viewport()->setCursor(row >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
}
void TimelineWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) { QAbstractScrollArea::mouseReleaseEvent(event); return; }
    const int index = gestureClip_;
    const int target = moveTarget_;
    const Gesture action = gesture_;
    const bool dragged = dragging_;
    const Clip trim = trimmed_;
    resetGesture();
    viewport()->update();
    if (!dragged || index < 0 || index >= project_.clips.size()) return;
    if (action == Gesture::Clip && target >= 0 && target != index) emit clipMoved(index, target);
    else if ((action == Gesture::TrimStart || action == Gesture::TrimEnd) &&
        (trim.inSeconds != project_.clips[index].inSeconds || trim.outSeconds != project_.clips[index].outSeconds))
        emit trimRequested(index, trim.inSeconds, trim.outSeconds);
}
void TimelineWidget::resetGesture() {
    gesture_ = Gesture::None;
    gestureClip_ = moveTarget_ = -1;
    dragging_ = false;
    viewport()->unsetCursor();
}
void TimelineWidget::keyPressEvent(QKeyEvent *event) {
    if (project_.clips.isEmpty()) { QAbstractScrollArea::keyPressEvent(event); return; }
    int frame = position_;
    switch (event->key()) {
    case Qt::Key_Left: --frame; break;
    case Qt::Key_Right: ++frame; break;
    case Qt::Key_PageUp: frame += Project::fps; break;
    case Qt::Key_PageDown: frame -= Project::fps; break;
    case Qt::Key_Home: frame = 0; break;
    case Qt::Key_End: frame = project_.durationFrames() - 1; break;
    case Qt::Key_Escape: resetGesture(); viewport()->update(); return;
    default: QAbstractScrollArea::keyPressEvent(event); return;
    }
    setPosition(frame);
    emit seekRequested(position_);
    viewport()->update();
    event->accept();
}
QStringList TimelineWidget::droppedFiles(const QMimeData *mime) const {
    if (!mime || !mime->hasUrls()) return {};
    QStringList paths;
    for (const auto &url : mime->urls()) {
        if (!url.isLocalFile()) return {};
        paths.append(url.toLocalFile());
    }
    return paths;
}
void TimelineWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (droppedFiles(event->mimeData()).isEmpty()) { event->ignore(); return; }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    dropActive_ = true;
    viewport()->update();
}
void TimelineWidget::dragMoveEvent(QDragMoveEvent *event) {
    if (droppedFiles(event->mimeData()).isEmpty()) { event->ignore(); return; }
    event->setDropAction(Qt::CopyAction);
    event->accept();
}
void TimelineWidget::dragLeaveEvent(QDragLeaveEvent *event) {
    dropActive_ = false;
    viewport()->update();
    event->accept();
}
void TimelineWidget::dropEvent(QDropEvent *event) {
    dropActive_ = false;
    viewport()->update();
    const QStringList files = droppedFiles(event->mimeData());
    if (files.isEmpty()) { event->ignore(); return; }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit filesDropped(files);
}
bool TimelineWidget::viewportEvent(QEvent *event) {
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const int clip = clipAt(help->pos());
        if (clip >= 0) {
            QToolTip::showText(help->globalPos(), QFileInfo(project_.clips[clip].path).fileName() +
                "\nSelect to edit. Drag the center to reorder; selected edges trim.", viewport());
        } else QToolTip::hideText();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}
