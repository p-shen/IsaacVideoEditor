#include "quick_timeline.h"

#include <QCursor>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QStyleHints>
#include <QUrl>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace {
QString timecode(int frame) {
    frame = std::max(0, frame);
    const int seconds = frame / Project::fps;
    return QString("%1:%2:%3").arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0')).arg(frame % 60, 2, 10, QLatin1Char('0'));
}
QColor blend(const QColor &a, const QColor &b, double amount) {
    return QColor::fromRgbF(a.redF() * (1 - amount) + b.redF() * amount,
                           a.greenF() * (1 - amount) + b.greenF() * amount,
                           a.blueF() * (1 - amount) + b.blueF() * amount);
}
}

QuickTimeline::QuickTimeline(QQuickItem *parent) : QQuickPaintedItem(parent), font_(QGuiApplication::font()) {
    setObjectName("quickTimeline");
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setActiveFocusOnTab(true);
    setFlag(ItemAcceptsDrops, true);
    setAntialiasing(true);
    setClip(true);
    setImplicitWidth(800);
    setImplicitHeight(rulerHeight() + videoHeight() + 2 * audioHeight());
    edgeTimer_.setInterval(16);
    connect(&edgeTimer_, &QTimer::timeout, this, &QuickTimeline::scrollAtEdge);
    qGuiApp->installEventFilter(this);
}

const Project &QuickTimeline::displayProject() const {
    return dragging_ && gesture_ != Gesture::None && gesture_ != Gesture::Scrub ? preview_ : project_;
}
int QuickTimeline::labelWidth() const { return std::max(96, QFontMetrics(font_).horizontalAdvance("Narration") + 30); }
int QuickTimeline::rulerHeight() const { return std::max(32, QFontMetrics(font_).height() + 12); }
int QuickTimeline::videoHeight() const { return std::max(100, QFontMetrics(font_).height() * 3 + 36); }
int QuickTimeline::audioHeight() const { return std::max(54, QFontMetrics(font_).height() + 32); }
double QuickTimeline::origin() const { return labelWidth() + 12; }
double QuickTimeline::xForFrame(int frame) const { return origin() + frame * pixelsPerSecond_ / 60 - contentOffset_; }
double QuickTimeline::unclampedFrameAt(double x) const { return (x - origin() + contentOffset_) * 60 / pixelsPerSecond_; }
int QuickTimeline::frameAt(double x) const {
    if (!std::isfinite(x)) return 0;
    return qBound(0, qRound(unclampedFrameAt(x)), std::max(0, project_.durationFrames() - 1));
}
QRectF QuickTimeline::rectForClip(const Project &p, int index) const {
    if (index < 0 || index >= p.clips.size()) return {};
    return QRectF(xForFrame(p.clipStartFrame(index)), rulerHeight() + 8,
                  std::max(1.0, p.clipFrames(p.clips[index]) * pixelsPerSecond_ / 60), videoHeight() - 16);
}
QRectF QuickTimeline::clipRect(int index) const { return rectForClip(displayProject(), index); }
int QuickTimeline::clipAt(const QPointF &point) const {
    if (point.x() < labelWidth()) return -1;
    int edge = -1;
    double nearest = 10;
    for (int i = 0; i < project_.clips.size(); ++i) {
        const auto rect = rectForClip(project_, i);
        if (!rect.contains(point) || edgeAt(i, point.x()) == Gesture::Body) continue;
        const double distance = std::min(std::abs(point.x() - rect.left()), std::abs(point.x() - rect.right()));
        if (distance < nearest) { nearest = distance; edge = i; }
    }
    if (edge >= 0) return edge;
    for (const QVariant &v : selection_) {
        const int index = v.toInt();
        if (rectForClip(project_, index).contains(point)) return index;
    }
    for (int i = int(project_.clips.size()) - 1; i >= 0; --i)
        if (rectForClip(project_, i).contains(point)) return i;
    return -1;
}
QuickTimeline::Gesture QuickTimeline::edgeAt(int index, double x) const {
    const auto rect = rectForClip(project_, index);
    // Tiny clips split their hit area into two distinct handles after selection.
    if (rect.width() < 18 && !selection_.contains(index)) return Gesture::Body;
    if (x - rect.left() <= std::min(9.0, rect.width() / 2)) return Gesture::TrimStart;
    if (rect.right() - x <= std::min(9.0, rect.width() / 2)) return Gesture::TrimEnd;
    return Gesture::Body;
}

void QuickTimeline::setProject(const Project &p, const QVector<double> &durations, double voiceDuration) {
    if (gesture_ != Gesture::None) finishGesture(true);
    endSkim();
    project_ = p;
    sourceDurations_ = durations;
    voiceDurationSeconds_ = std::isfinite(voiceDuration) ? std::max(0.0, voiceDuration) : 0;
    setPosition(position_);
    setSelection(selection_);
    retainSources();
    updateViewport(autoFit_);
}
void QuickTimeline::setPosition(int frame) {
    frame = qBound(0, frame, std::max(0, project_.durationFrames() - 1));
    const bool changed = position_ != frame;
    position_ = frame;
    const double x = xForFrame(position_);
    if (width() > origin() + 48) {
        if (x < origin()) setContentOffset(contentOffset_ + x - origin());
        else if (x > width() - 24) setContentOffset(contentOffset_ + x - width() + 24);
    }
    if (changed) emit positionChanged();
    update();
}
void QuickTimeline::setSelection(const QVariantList &values) {
    QVector<int> indices;
    for (const auto &value : values) {
        bool valid = false;
        const int index = value.toInt(&valid);
        if (valid && index >= 0 && index < project_.clips.size() && !indices.contains(index)) indices.append(index);
    }
    std::sort(indices.begin(), indices.end());
    QVariantList normalized;
    for (int index : indices) normalized.append(index);
    if (selection_ == normalized) return;
    selection_ = normalized;
    emit selectionChanged();
    update();
}
void QuickTimeline::setSnapping(bool enabled) {
    if (snapping_ == enabled) return;
    snapping_ = enabled;
    snapSource_ = snapTimeline_ = -1;
    emit snappingChanged();
    if (dragging_) updateGesture(lastModifiers_);
    update();
}
void QuickTimeline::setSkimming(bool enabled) {
    if (skimming_ == enabled) return;
    skimming_ = enabled;
    if (!enabled) endSkim();
    emit skimmingChanged();
}
void QuickTimeline::setPlaying(bool playing) {
    if (playing_ == playing) return;
    playing_ = playing;
    if (playing) endSkim();
    updateInteractive();
    emit playingChanged();
}
void QuickTimeline::setFont(const QFont &font) {
    if (font_ == font) return;
    font_ = font;
    setImplicitHeight(rulerHeight() + videoHeight() + 2 * audioHeight());
    emit fontChanged();
    updateViewport(autoFit_);
}
void QuickTimeline::setWaveformScale(double scale) {
    if (!std::isfinite(scale)) return;
    scale = qBound(1.0, scale, 4.0);
    if (qFuzzyCompare(waveformScale_, scale)) return;
    waveformScale_ = scale;
    emit waveformScaleChanged();
    update();
}
void QuickTimeline::setContentOffset(double offset) {
    if (!std::isfinite(offset)) return;
    offset = qBound(0.0, offset, maximumOffset_);
    if (qFuzzyCompare(contentOffset_ + 1, offset + 1)) return;
    contentOffset_ = offset;
    emit viewportChanged();
    refreshMedia();
    update();
}
void QuickTimeline::updateViewport(bool fit) {
    const double available = std::max(1.0, width() - labelWidth() - 36);
    if (fit) pixelsPerSecond_ = available / (project_.clips.isEmpty() ? 10.0 : std::max(1.0 / 60, project_.durationFrames() / 60.0));
    // Keep the pre-gesture extent while ripple shortening, or scroll clamping feeds back into the trim delta.
    const int frames = gesture_ == Gesture::None ? displayProject().durationFrames()
        : std::max(project_.durationFrames(), displayProject().durationFrames());
    const double content = frames * pixelsPerSecond_ / 60;
    maximumOffset_ = std::max(0.0, content - available);
    contentOffset_ = qBound(0.0, contentOffset_, maximumOffset_);
    emit viewportChanged();
    refreshMedia();
    update();
}
bool QuickTimeline::eventFilter(QObject *watched, QEvent *event) {
    if (watched == qGuiApp && event->type() == QEvent::ApplicationPaletteChange) update();
    return QQuickPaintedItem::eventFilter(watched, event);
}
void QuickTimeline::geometryChange(const QRectF &now, const QRectF &before) {
    QQuickPaintedItem::geometryChange(now, before);
    if (now.size() != before.size()) updateViewport(autoFit_ && gesture_ == Gesture::None);
}
void QuickTimeline::zoomAt(double factor, double anchorX) {
    if (!std::isfinite(factor) || factor <= 0 || !std::isfinite(anchorX)) return;
    const double x = qBound(origin(), anchorX, std::max(origin(), width() - 24));
    const double seconds = (x - origin() + contentOffset_) / pixelsPerSecond_;
    const double scale = qBound(0.2, pixelsPerSecond_ * factor, std::max(1200.0, pixelsPerSecond_));
    autoFit_ = false;
    pixelsPerSecond_ = scale;
    contentOffset_ = std::max(0.0, seconds * scale - (x - origin()));
    updateViewport();
}
void QuickTimeline::zoomIn() { zoomAt(1.5, qBound(origin(), xForFrame(position_), std::max(origin(), width() - 24))); }
void QuickTimeline::zoomOut() { zoomAt(1 / 1.5, qBound(origin(), xForFrame(position_), std::max(origin(), width() - 24))); }
void QuickTimeline::fitToWidth() {
    autoFit_ = true;
    contentOffset_ = 0;
    updateViewport(true);
}

MediaCache *QuickTimeline::mediaCache() const { return mediaCache_.data(); }
void QuickTimeline::setMediaCache(MediaCache *cache) {
    if (mediaCache_ == cache) return;
    if (mediaCache_) disconnect(mediaCache_, nullptr, this, nullptr);
    mediaCache_ = cache;
    if (cache) {
        connect(cache, &MediaCache::changed, this, [this] { refreshMedia(); update(); });
        connect(cache, &QObject::destroyed, this, [this] { filmTiles_.clear(); waveforms_.clear(); waveformStates_.clear(); emit mediaCacheChanged(); update(); });
        retainSources();
        updateInteractive();
    }
    refreshMedia();
    emit mediaCacheChanged();
    update();
}
void QuickTimeline::retainSources() {
    if (!mediaCache_) return;
    QStringList paths;
    for (const auto &clip : project_.clips) paths.append(clip.path);
    if (!project_.voicePath.isEmpty()) paths.append(project_.voicePath);
    if (!project_.musicPath.isEmpty()) paths.append(project_.musicPath);
    paths.removeDuplicates();
    mediaCache_->retainSources(paths);
}
void QuickTimeline::updateInteractive() {
    if (mediaCache_) mediaCache_->setInteractive(playing_ || gesture_ != Gesture::None);
}
void QuickTimeline::refreshMedia() {
    // QQuickPaintedItem paints on the render thread; all cache calls stay here on the GUI thread.
    filmTiles_.clear();
    waveforms_.clear();
    waveformStates_.clear();
    if (!mediaCache_ || width() <= labelWidth()) return;
    const Project &p = displayProject();
    const double visibleLeft = labelWidth() - 96;
    const double visibleRight = width() + 96;
    mediaCache_->beginViewport();
    const auto requestWave = [this](const QString &path) {
        if (path.isEmpty() || waveforms_.contains(path)) return;
        waveforms_.insert(path, mediaCache_->waveform(path));
        waveformStates_.insert(path, mediaCache_->waveformState(path));
    };
    const double labelHeight = QFontMetrics(font_).height() + 6;
    for (int i = 0; i < p.clips.size(); ++i) {
        const auto r = rectForClip(p, i);
        if (r.right() < visibleLeft || r.left() > visibleRight) continue;
        const Clip &clip = p.clips[i];
        requestWave(clip.path);
        if (r.width() < 28) continue;
        const double tileWidth = std::min(90.0, r.width());
        const int first = std::max(0, int(std::floor((visibleLeft - r.left()) / tileWidth)));
        const int last = std::min(int(std::ceil(r.width() / tileWidth)), int(std::ceil((visibleRight - r.left()) / tileWidth)));
        for (int n = first; n < last; ++n) {
            const double dx = n * tileWidth;
            const double sample = std::min(clip.outSeconds - 1.0 / 60, clip.inSeconds + (dx + std::min(tileWidth, r.width() - dx) / 2) / pixelsPerSecond_);
            const double sourceTime = std::max(clip.inSeconds, qRound(sample * 60) / 60.0);
            filmTiles_.append({QRectF(r.left() + dx, r.top() + labelHeight,
                                     std::min(tileWidth, r.width() - dx), std::max(1.0, r.height() - labelHeight - 18)),
                               mediaCache_->thumbnail(clip.path, sourceTime, clip.rotationDegrees), !QFileInfo::exists(clip.path)});
        }
    }
    const int voiceStart = qRound(p.voiceStartSeconds * 60);
    const double voiceLeft = xForFrame(voiceStart);
    const double voiceRight = xForFrame(std::min(p.durationFrames(), voiceStart + qRound(voiceDurationSeconds_ * 60)));
    if (voiceRight >= visibleLeft && voiceLeft <= visibleRight && voiceRight > voiceLeft) requestWave(p.voicePath);
    if (xForFrame(p.durationFrames()) >= visibleLeft) requestWave(p.musicPath);
    mediaCache_->endViewport();
}

void QuickTimeline::endSkim() {
    if (skimFrame_ < 0) return;
    skimFrame_ = -1;
    emit skimEnded();
    update();
}
void QuickTimeline::skimAt(const QPointF &point) {
    if (!skimming_ || playing_ || gesture_ != Gesture::None || clipAt(point) < 0) { endSkim(); return; }
    const int frame = frameAt(point.x());
    if (frame == skimFrame_) return;
    skimFrame_ = frame;
    emit skimRequested(frame);
    update();
}
void QuickTimeline::hoverMoveEvent(QHoverEvent *event) {
    const int index = clipAt(event->position());
    if (index >= 0) setCursor(edgeAt(index, event->position().x()) == Gesture::Body ? Qt::OpenHandCursor : Qt::SizeHorCursor);
    else unsetCursor();
    skimAt(event->position());
    event->accept();
}
void QuickTimeline::hoverLeaveEvent(QHoverEvent *event) { endSkim(); unsetCursor(); event->accept(); }
void QuickTimeline::seekAt(double x) {
    if (project_.clips.isEmpty()) return;
    setPosition(frameAt(x));
    emit seekRequested(position_);
}
void QuickTimeline::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || event->position().x() < labelWidth()) { event->ignore(); return; }
    forceActiveFocus(Qt::MouseFocusReason);
    endSkim();
    pressPoint_ = lastPoint_ = event->position();
    pressOffset_ = contentOffset_;
    lastModifiers_ = event->modifiers();
    dragging_ = false;
    snapSource_ = snapTimeline_ = -1;
    const int index = clipAt(event->position());
    if (event->position().y() < rulerHeight() && !project_.clips.isEmpty()) {
        gesture_ = Gesture::Scrub;
        scrubStart_ = position_;
        setKeepMouseGrab(true);
        updateInteractive();
        emit scrubStarted();
        seekAt(event->position().x());
    } else if (index >= 0) {
        gestureClip_ = index;
        gesture_ = edgeAt(index, event->position().x());
        deferredSelection_ = gesture_ == Gesture::Body && selection_.contains(index) && event->modifiers() == Qt::NoModifier;
        if (!deferredSelection_) emit selectionRequested(index, int(event->modifiers()));
        group_.clear();
        if (selection_.contains(index)) for (const QVariant &v : selection_) group_.append(v.toInt());
        else group_.append(index);
        preview_ = project_;
        previewOrder_.resize(project_.clips.size());
        std::iota(previewOrder_.begin(), previewOrder_.end(), 0);
        setKeepMouseGrab(true);
        updateInteractive();
    } else {
        const double audioTop = rulerHeight() + videoHeight();
        if (event->position().y() >= audioTop && event->position().y() < audioTop + 2 * audioHeight())
            emit audioSelected(event->position().y() < audioTop + audioHeight() ? 1 : 2);
        else emit clearSelectionRequested();
    }
    event->accept();
}
void QuickTimeline::mouseMoveEvent(QMouseEvent *event) {
    lastPoint_ = event->position();
    lastModifiers_ = event->modifiers();
    if (gesture_ == Gesture::Scrub) {
        seekAt(lastPoint_.x());
    } else if (gesture_ != Gesture::None) {
        if (!dragging_ && (lastPoint_ - pressPoint_).manhattanLength() >= QGuiApplication::styleHints()->startDragDistance()) dragging_ = true;
        if (dragging_) {
            updateGesture(lastModifiers_);
            if (!edgeTimer_.isActive()) edgeTimer_.start();
        }
    }
    event->accept();
}
int QuickTimeline::snappedSourceFrame(double raw, int minimum, int maximum, Qt::KeyboardModifiers modifiers) {
    if (!snapping_ || modifiers.testFlag(Qt::AltModifier)) {
        snapSource_ = snapTimeline_ = -1;
        snapLabel_.clear();
        return qBound(minimum, qRound(raw), maximum);
    }
    const double ppf = pixelsPerSecond_ / 60;
    if (snapSource_ >= minimum && snapSource_ <= maximum && std::abs(raw - snapSource_) * ppf <= 12) return snapSource_;
    snapSource_ = snapTimeline_ = -1;
    snapLabel_.clear();
    const Clip &clip = project_.clips[gestureClip_];
    const bool start = gesture_ == Gesture::TrimStart;
    const int oldSource = qRound((start ? clip.inSeconds : clip.outSeconds) * 60);
    const int oldEdge = project_.clipStartFrame(gestureClip_) + (start ? 0 : project_.clipFrames(clip));
    struct Target { int frame; QString label; bool playhead = false; };
    QVector<Target> targets{{position_, "Playhead", true}};
    if (oldEdge != 0) targets.append({0, "Start"});
    if (oldEdge != project_.durationFrames()) targets.append({project_.durationFrames(), "End"});
    for (int i = 0; i < project_.clips.size(); ++i) {
        if (i == gestureClip_) continue;
        const int first = project_.clipStartFrame(i);
        targets.append({first, QString("Clip %1 start").arg(i + 1)});
        targets.append({first + project_.clipFrames(project_.clips[i]), QString("Clip %1 end").arg(i + 1)});
    }
    double bestDistance = std::numeric_limits<double>::max();
    bool playhead = false;
    for (const auto &target : targets) {
        // Ripple trim uses the moving source edge against the pre-gesture boundaries.
        const int candidate = oldSource + target.frame - oldEdge;
        if (candidate < minimum || candidate > maximum) continue;
        const double distance = std::abs(raw - candidate);
        if (distance * ppf > 8 || distance > bestDistance || (distance == bestDistance && (playhead || !target.playhead))) continue;
        bestDistance = distance;
        playhead = target.playhead;
        snapSource_ = candidate;
        snapTimeline_ = target.frame;
        snapLabel_ = target.label;
    }
    return snapSource_ >= 0 ? snapSource_ : qBound(minimum, qRound(raw), maximum);
}
int QuickTimeline::boundaryAt(double x) const {
    int boundary = 0;
    double distance = std::numeric_limits<double>::max();
    for (int i = 0; i <= project_.clips.size(); ++i) {
        const int frame = i == project_.clips.size() ? project_.durationFrames() : project_.clipStartFrame(i);
        const double candidate = std::abs(xForFrame(frame) - x);
        if (candidate < distance) { boundary = i; distance = candidate; }
    }
    return boundary;
}
QVector<int> QuickTimeline::reorderedIndices(int boundary) const {
    QVector<int> order;
    int insert = 0;
    for (int i = 0; i < project_.clips.size(); ++i) {
        if (group_.contains(i)) continue;
        if (i < boundary) ++insert;
        order.append(i);
    }
    for (int i = 0; i < group_.size(); ++i) order.insert(insert + i, group_[i]);
    return order;
}
void QuickTimeline::updateGesture(Qt::KeyboardModifiers modifiers) {
    if (!dragging_ || gestureClip_ < 0 || gestureClip_ >= project_.clips.size()) return;
    preview_ = project_;
    if (gesture_ == Gesture::Body) {
        moveBoundary_ = boundaryAt(lastPoint_.x());
        previewOrder_ = reorderedIndices(moveBoundary_);
        preview_.reorderClips(previewOrder_);
        setCursor(Qt::ClosedHandCursor);
        gestureText_ = QString("Move %1 clip%2 to boundary %3 · Duration %4").arg(group_.size()).arg(group_.size() == 1 ? "" : "s").arg(moveBoundary_).arg(timecode(preview_.durationFrames()));
        if (preview_.effectiveCrossfadeFrames() < project_.effectiveCrossfadeFrames()) gestureText_ += " · Other fades shorten";
    } else {
        const Clip &original = project_.clips[gestureClip_];
        Clip &clip = preview_.clips[gestureClip_];
        const double delta = (lastPoint_.x() - pressPoint_.x() + contentOffset_ - pressOffset_) * 60 / pixelsPerSecond_;
        int frames = 0;
        if (gesture_ == Gesture::TrimStart) {
            const int old = qRound(original.inSeconds * 60);
            const int value = snappedSourceFrame(old + delta, 0, qRound(original.outSeconds * 60) - 1, modifiers);
            clip.inSeconds = value / 60.0;
            frames = value - old;
        } else {
            const int old = qRound(original.outSeconds * 60);
            const double duration = sourceDurations_.value(gestureClip_, original.outSeconds);
            const int maximum = qRound(std::max(original.outSeconds, std::isfinite(duration) ? duration : original.outSeconds) * 60);
            const int value = snappedSourceFrame(old + delta, qRound(original.inSeconds * 60) + 1, maximum, modifiers);
            clip.outSeconds = value / 60.0;
            frames = value - old;
        }
        gestureText_ = QString("%1%2 frames · Duration %3").arg(frames >= 0 ? "+" : "").arg(frames).arg(timecode(preview_.durationFrames()));
        if (preview_.effectiveCrossfadeFrames() < project_.effectiveCrossfadeFrames()) gestureText_ += " · Other fades shorten";
        if (snapSource_ >= 0) gestureText_ += " · Snap: " + snapLabel_;
        updateTrimComparison();
    }
    emit gestureChanged();
    updateViewport();
}
void QuickTimeline::updateTrimComparison() {
    const int outIndex = gesture_ == Gesture::TrimStart ? gestureClip_ - 1 : gestureClip_;
    const int inIndex = outIndex + 1;
    QVariantMap result{{"active", true}, {"outPath", QString()}, {"outTime", 0.0}, {"outRotation", 0},
                       {"inPath", QString()}, {"inTime", 0.0}, {"inRotation", 0}};
    if (outIndex >= 0 && outIndex < preview_.clips.size()) {
        const Clip &clip = preview_.clips[outIndex];
        result["outPath"] = clip.path;
        result["outTime"] = (qRound(clip.outSeconds * 60) - 1) / 60.0;
        result["outRotation"] = clip.rotationDegrees;
    }
    if (inIndex >= 0 && inIndex < preview_.clips.size()) {
        const Clip &clip = preview_.clips[inIndex];
        result["inPath"] = clip.path;
        result["inTime"] = qRound(clip.inSeconds * 60) / 60.0;
        result["inRotation"] = clip.rotationDegrees;
    }
    trimPreview_ = result;
}
void QuickTimeline::finishGesture(bool canceled) {
    const Gesture action = gesture_;
    const bool dragged = dragging_;
    const bool select = deferredSelection_ && !dragged && !canceled;
    const int index = gestureClip_;
    const int boundary = moveBoundary_;
    const Project preview = preview_;
    const auto order = previewOrder_;
    const auto group = group_;
    gesture_ = Gesture::None;
    gestureClip_ = moveBoundary_ = -1;
    dragging_ = deferredSelection_ = false;
    snapSource_ = snapTimeline_ = -1;
    gestureText_.clear();
    trimPreview_ = {{"active", false}};
    edgeTimer_.stop();
    setKeepMouseGrab(false);
    unsetCursor();
    updateInteractive();
    updateViewport();
    emit gestureChanged();
    if (action == Gesture::Scrub) {
        if (canceled) { setPosition(scrubStart_); emit seekRequested(position_); }
        emit scrubFinished(canceled);
    }
    if (select) emit selectionRequested(index, int(Qt::NoModifier));
    if (canceled || !dragged || index < 0 || index >= project_.clips.size()) return;
    if (action == Gesture::Body && boundary >= 0) {
        bool changed = false;
        for (int i = 0; i < order.size(); ++i) changed |= order[i] != i;
        if (changed) {
            QVariantList indices;
            for (int i : group) indices.append(i);
            emit groupMoveRequested(indices, boundary);
        }
    } else if (action == Gesture::TrimStart || action == Gesture::TrimEnd) {
        const Clip &before = project_.clips[index];
        const Clip &after = preview.clips[index];
        if (qRound(before.inSeconds * 60) != qRound(after.inSeconds * 60) || qRound(before.outSeconds * 60) != qRound(after.outSeconds * 60))
            emit trimRequested(index, after.inSeconds, after.outSeconds);
    }
}
void QuickTimeline::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && gesture_ != Gesture::None) {
        lastPoint_ = event->position();
        if (gesture_ == Gesture::Scrub) seekAt(lastPoint_.x());
        else if (dragging_) updateGesture(event->modifiers());
        finishGesture(false);
    }
    event->accept();
}
void QuickTimeline::mouseUngrabEvent() { if (gesture_ != Gesture::None) finishGesture(true); }
void QuickTimeline::scrollAtEdge() {
    if (!dragging_ || gesture_ == Gesture::None) { edgeTimer_.stop(); return; }
    const double left = origin() + 24;
    const double right = width() - 32;
    double speed = 0;
    if (lastPoint_.x() < left) speed = -std::min(28.0, (left - lastPoint_.x()) * 0.4);
    else if (lastPoint_.x() > right) speed = std::min(28.0, (lastPoint_.x() - right) * 0.4);
    const double before = contentOffset_;
    setContentOffset(before + speed);
    if (before != contentOffset_) updateGesture(lastModifiers_);
}
void QuickTimeline::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && gesture_ != Gesture::None) {
        finishGesture(true);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_N && event->modifiers() == Qt::NoModifier) {
        setSnapping(!snapping_);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Alt && dragging_) {
        lastModifiers_ |= Qt::AltModifier;
        updateGesture(lastModifiers_);
        event->accept();
        return;
    }
    if (project_.clips.isEmpty()) { event->ignore(); return; }
    int frame = position_;
    switch (event->key()) {
    case Qt::Key_Left: --frame; break;
    case Qt::Key_Right: ++frame; break;
    case Qt::Key_PageUp: frame += 60; break;
    case Qt::Key_PageDown: frame -= 60; break;
    case Qt::Key_Home: frame = 0; break;
    case Qt::Key_End: frame = project_.durationFrames() - 1; break;
    default: QQuickPaintedItem::keyPressEvent(event); return;
    }
    endSkim();
    setPosition(frame);
    emit seekRequested(position_);
    event->accept();
}
void QuickTimeline::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Alt && dragging_) {
        lastModifiers_ &= ~Qt::AltModifier;
        updateGesture(lastModifiers_);
        event->accept();
    } else QQuickPaintedItem::keyReleaseEvent(event);
}
void QuickTimeline::wheelEvent(QWheelEvent *event) {
    const QPoint pixel = event->pixelDelta();
    const QPoint angle = event->angleDelta();
    if (event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::MetaModifier)) {
        const double delta = !pixel.isNull() ? pixel.y() : angle.y() / 8.0;
        zoomAt(std::pow(1.01, delta), event->position().x());
    } else {
        const double delta = !pixel.isNull() ? (pixel.x() != 0 ? pixel.x() : pixel.y()) : (angle.x() != 0 ? angle.x() : angle.y()) / 2.0;
        setContentOffset(contentOffset_ - delta);
    }
    event->accept();
}
QStringList QuickTimeline::droppedFiles(const QMimeData *mime) const {
    if (!mime || !mime->hasUrls()) return {};
    QStringList paths;
    for (const auto &url : mime->urls()) {
        if (!url.isLocalFile()) return {};
        paths.append(url.toLocalFile());
    }
    return paths;
}
void QuickTimeline::dragEnterEvent(QDragEnterEvent *event) {
    dropActive_ = !droppedFiles(event->mimeData()).isEmpty();
    if (dropActive_) event->acceptProposedAction(); else event->ignore();
    update();
}
void QuickTimeline::dragMoveEvent(QDragMoveEvent *event) {
    if (!droppedFiles(event->mimeData()).isEmpty()) event->acceptProposedAction(); else event->ignore();
}
void QuickTimeline::dragLeaveEvent(QDragLeaveEvent *event) { dropActive_ = false; event->accept(); update(); }
void QuickTimeline::dropEvent(QDropEvent *event) {
    const auto paths = droppedFiles(event->mimeData());
    dropActive_ = false;
    if (!paths.isEmpty()) { event->acceptProposedAction(); emit filesDropped(paths); } else event->ignore();
    update();
}

void QuickTimeline::paint(QPainter *painter) {
    QPainter &p = *painter;
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(font_);
    const QFontMetrics fm(font_);
    const auto palette = QGuiApplication::palette();
    const QColor base = palette.color(QPalette::Base);
    const QColor surface = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::Text);
    const QColor muted = palette.color(QPalette::PlaceholderText);
    const QColor accent = palette.color(QPalette::Highlight);
    const QColor divider = blend(base, text, 0.25);
    const int labels = labelWidth();
    const int ruler = rulerHeight();
    const int video = videoHeight();
    const int audio = audioHeight();
    const Project &project = displayProject();
    p.fillRect(boundingRect(), base);
    p.fillRect(QRectF(0, 0, width(), ruler), surface);
    p.fillRect(QRectF(0, ruler, labels, std::max(0.0, height() - ruler)), surface);
    p.setPen(divider);
    for (int y : {ruler, ruler + video, ruler + video + audio, ruler + video + 2 * audio}) p.drawLine(QPointF(0, y), QPointF(width(), y));
    p.drawLine(QPointF(labels, 0), QPointF(labels, height()));
    p.setPen(muted);
    p.drawText(QRectF(12, 0, labels - 20, ruler), Qt::AlignVCenter, "TRACKS");
    p.setPen(text);
    p.drawText(QRectF(12, ruler, labels - 20, video), Qt::AlignVCenter, "Video");
    p.drawText(QRectF(12, ruler + video, labels - 20, audio), Qt::AlignVCenter, "Narration");
    p.drawText(QRectF(12, ruler + video + audio, labels - 20, audio), Qt::AlignVCenter, "Music");
    p.save();
    p.setClipRect(QRectF(labels + 1, 0, std::max(0.0, width() - labels - 1), height()));
    const double intervals[] = {1.0 / 60, 0.1, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600};
    double interval = 3600;
    const int spacing = std::max(74, fm.horizontalAdvance("00:00:00") + 14);
    for (double candidate : intervals) if (candidate * pixelsPerSecond_ >= spacing) { interval = candidate; break; }
    const double firstSecond = std::max(0.0, std::floor(contentOffset_ / pixelsPerSecond_ / interval) * interval);
    const double lastSecond = (contentOffset_ + std::max(0.0, width() - labels)) / pixelsPerSecond_;
    for (double sec = firstSecond; sec <= lastSecond; sec += interval) {
        const double x = xForFrame(qRound(sec * 60));
        p.setPen(divider);
        p.drawLine(QPointF(x, ruler - 7), QPointF(x, ruler + video + 2 * audio));
        p.setPen(muted);
        p.drawText(QRectF(x + 5, 0, spacing, ruler - 5), Qt::AlignVCenter, timecode(qRound(sec * 60)));
    }
    if (project.clips.isEmpty()) {
        const QRectF r(origin(), ruler + 8, std::max(1.0, width() - origin() - 24), video - 16);
        p.setPen(QPen(dropActive_ ? accent : divider, 1.5, Qt::DashLine));
        p.setBrush(blend(base, accent, dropActive_ ? 0.18 : 0.03));
        p.drawRoundedRect(r, 5, 5);
        p.setPen(muted);
        p.drawText(r.adjusted(12, 0, -12, 0), Qt::AlignCenter | Qt::TextWordWrap, "Drop video clips here to start your edit");
    }
    const auto drawWave = [&](const QString &path, const QRectF &rect, double sourceStart, bool loop, double sourceDuration, const QColor &color, bool stateLabel) {
        const auto peaks = waveforms_.constFind(path);
        if (peaks == waveforms_.cend() || peaks->isEmpty()) {
            if (stateLabel && rect.width() > 110) {
                QString state = waveformStates_.value(path);
                const bool missing = !QFileInfo::exists(path);
                QString label = missing ? "Media missing" : state == "unavailable" ? "Waveform unavailable" : "Loading waveform…";
                p.setPen(muted);
                p.drawText(rect.adjusted(6, 0, -6, 0), Qt::AlignVCenter, fm.elidedText(label, Qt::ElideRight, std::max(0, int(rect.width() - 12))));
            }
            return;
        }
        const double length = sourceDuration > 0 ? sourceDuration : peaks->size() / 20.0;
        const double left = std::max(double(labels + 1), rect.left());
        const double right = std::min(width(), rect.right());
        p.setPen(QPen(color, 1));
        for (double x = left; x <= right; x += 2) {
            const double seconds = sourceStart + (x - rect.left()) / pixelsPerSecond_;
            const double sample = loop && length > 0 ? std::fmod(std::max(0.0, seconds), length) : seconds;
            const int from = std::max(0, int(std::floor(sample * 20)));
            const int count = std::max(1, int(std::ceil(2 * 20 / pixelsPerSecond_)));
            float peak = 0;
            for (int n = 0; n < count; ++n) {
                int index = from + n;
                if (loop && !peaks->isEmpty()) index %= peaks->size();
                if (index >= 0 && index < peaks->size()) peak = std::max(peak, (*peaks)[index]);
            }
            const double amplitude = qBound(0.0, double(peak) * waveformScale_, 1.0) * std::max(0.0, rect.height() / 2 - 1);
            p.drawLine(QPointF(x, rect.center().y() - amplitude), QPointF(x, rect.center().y() + amplitude));
        }
        if (loop && length > 0 && length * pixelsPerSecond_ >= 18) {
            p.setPen(QPen(color, 1, Qt::DashLine));
            const double visibleSeconds = std::max(0.0, (labels - rect.left()) / pixelsPerSecond_);
            for (double sec = (std::floor(visibleSeconds / length) + 1) * length; rect.left() + sec * pixelsPerSecond_ < right; sec += length) {
                const double x = rect.left() + sec * pixelsPerSecond_;
                p.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
            }
        }
    };
    for (int i = 0; i < project.clips.size(); ++i) {
        const QRectF r = rectForClip(project, i);
        if (r.right() < labels || r.left() > width()) continue;
        const Clip &clip = project.clips[i];
        const int originalIndex = dragging_ && gesture_ == Gesture::Body ? previewOrder_.value(i, i) : i;
        const bool selected = selection_.contains(originalIndex) || (gestureClip_ == originalIndex && gesture_ != Gesture::None);
        p.fillRect(r, blend(base, accent, selected ? 0.24 : 0.12));
        p.setPen(QPen(selected ? accent : blend(base, text, 0.46), selected ? 2 : 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
        p.save();
        p.setClipRect(r.adjusted(2, 2, -2, -2), Qt::IntersectClip);
        for (const FilmTile &tile : filmTiles_) {
            if (!r.contains(tile.rect.center())) continue;
            if (!tile.image.isNull()) {
                const QSizeF imageSize = tile.image.size().scaled(tile.rect.size().toSize(), Qt::KeepAspectRatio);
                const QRectF imageRect(tile.rect.center().x() - imageSize.width() / 2, tile.rect.center().y() - imageSize.height() / 2, imageSize.width(), imageSize.height());
                p.fillRect(tile.rect, blend(base, text, 0.05));
                p.drawImage(imageRect, tile.image);
            } else {
                p.fillRect(tile.rect.adjusted(1, 1, -1, -1), blend(base, text, 0.06));
                p.setPen(muted);
                if (tile.rect.width() >= 58) p.drawText(tile.rect.adjusted(3, 0, -3, 0), Qt::AlignCenter | Qt::TextWordWrap, tile.missing ? "Media\nmissing" : "Loading…");
            }
        }
        const double labelHeight = fm.height() + 6;
        p.fillRect(QRectF(r.left(), r.top(), r.width(), labelHeight), blend(base, accent, selected ? 0.24 : 0.12));
        p.setPen(text);
        const QString label = QString::number(originalIndex + 1) + "  " + QFileInfo(clip.path).fileName();
        p.drawText(QRectF(r.left() + 12, r.top(), std::max(0.0, r.width() - 24), labelHeight), Qt::AlignVCenter, fm.elidedText(label, Qt::ElideRight, std::max(0, int(r.width() - 24))));
        drawWave(clip.path, QRectF(r.left(), r.bottom() - 17, r.width(), 16), clip.inSeconds, false, 0, blend(text, accent, 0.45), true);
        p.restore();
        if (selected && r.width() >= 8) {
            p.setPen(QPen(accent, 3, Qt::SolidLine, Qt::RoundCap));
            for (double x : {r.left() + 4, r.right() - 4}) p.drawLine(QPointF(x, r.center().y() - 9), QPointF(x, r.center().y() + 9));
        }
    }
    if (!mediaCache_ && !project.clips.isEmpty()) {
        p.setPen(muted);
        p.drawText(QRectF(origin() + 10, ruler + 42, std::max(1.0, width() - origin() - 20), 22), Qt::AlignVCenter, "Media previews unavailable");
    }
    for (int i = 1; i < project.clips.size(); ++i) {
        const int overlap = project.transitionFramesBefore(i);
        if (overlap <= 0) continue;
        const QRectF r(xForFrame(project.clipStartFrame(i)), ruler + 8, overlap * pixelsPerSecond_ / 60, video - 16);
        if (r.right() < labels || r.left() > width() || r.width() < 3) continue;
        p.fillRect(r, blend(base, accent, 0.24));
        p.setPen(QPen(accent, 1.2));
        p.drawLine(r.topLeft(), r.bottomRight());
        p.drawLine(r.bottomLeft(), r.topRight());
    }
    const auto audioBlock = [&](int lane, const QString &path, int start, int frames) {
        const int y = ruler + video + (lane - 1) * audio;
        if (path.isEmpty() || frames <= 0) {
            const QString hint = path.isEmpty() ? (lane == 1 ? "Add narration in Audio" : "Add music in Audio")
                : start >= project.durationFrames() ? "Narration starts after this edit" : "Narration duration unavailable";
            p.setPen(muted);
            p.drawText(QRectF(labels + 16, y, std::max(0.0, width() - labels - 24), audio), Qt::AlignVCenter, fm.elidedText(hint, Qt::ElideRight, std::max(0, int(width() - labels - 24))));
            return;
        }
        const QRectF r(xForFrame(start), y + 6, frames * pixelsPerSecond_ / 60, audio - 12);
        QColor tint = accent.toHsl();
        tint.setHslF(std::fmod(std::max(0.0, double(tint.hslHueF())) + (lane == 1 ? 0.38 : 0.58), 1.0), 0.44, 0.54);
        p.setPen(blend(text, tint, 0.5));
        p.setBrush(blend(base, tint, 0.18));
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
        double duration = 0;
        if (lane == 2) for (int i = 0; i < project_.clips.size(); ++i)
            if (project_.clips[i].path == path && std::isfinite(sourceDurations_.value(i)) && sourceDurations_.value(i) > 0) { duration = sourceDurations_[i]; break; }
        p.save();
        p.setClipRect(r.adjusted(2, 2, -2, -2), Qt::IntersectClip);
        drawWave(path, r.adjusted(0, fm.height() + 5, 0, -1), 0, lane == 2, duration, blend(text, tint, 0.45), true);
        p.setPen(text);
        const QString label = QFileInfo(path).fileName() + (lane == 2 ? " · Loops to end" : "");
        const double labelLeft = std::max(r.left() + 8, double(labels + 8));
        p.drawText(QRectF(labelLeft, r.top() + 2, std::max(0.0, r.right() - labelLeft - 8), fm.height() + 3), Qt::AlignVCenter, fm.elidedText(label, Qt::ElideRight, std::max(0, int(std::min(width(), r.right()) - labelLeft - 8))));
        p.restore();
    };
    const int voiceStart = qRound(project.voiceStartSeconds * 60);
    audioBlock(1, project.voicePath, voiceStart, std::max(0, std::min(project.durationFrames() - voiceStart, qRound(voiceDurationSeconds_ * 60))));
    audioBlock(2, project.musicPath, 0, project.durationFrames());
    if (dragging_ && gesture_ == Gesture::Body && moveBoundary_ >= 0) {
        const int frame = moveBoundary_ == project_.clips.size() ? project_.durationFrames() : project_.clipStartFrame(moveBoundary_);
        const double x = xForFrame(frame);
        p.setPen(QPen(accent, 3));
        p.drawLine(QPointF(x, ruler + 3), QPointF(x, ruler + video - 3));
    }
    if (snapTimeline_ >= 0 && dragging_) {
        const double x = xForFrame(snapTimeline_);
        p.setPen(QPen(accent, 1, Qt::DashLine));
        p.drawLine(QPointF(x, ruler), QPointF(x, ruler + video + 2 * audio));
    }
    if (!project.clips.isEmpty()) {
        const double x = xForFrame(position_);
        p.setPen(QPen(accent, 1.5));
        p.drawLine(QPointF(x, ruler - 8), QPointF(x, ruler + video + 2 * audio));
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(x - 5, 0), QPointF(x + 5, 0), QPointF(x + 5, 10), QPointF(x, 15), QPointF(x - 5, 10)}));
    }
    if (skimFrame_ >= 0) {
        const double x = xForFrame(skimFrame_);
        p.setPen(QPen(text, 1, Qt::DashLine));
        p.drawLine(QPointF(x, ruler), QPointF(x, ruler + video));
        p.drawText(QRectF(x + 5, ruler, 80, fm.height() + 4), Qt::AlignVCenter, "Skim");
    }
    p.restore();
    if (hasActiveFocus()) {
        p.setPen(QPen(accent, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(boundingRect().adjusted(1, 1, -1, -1));
    }
}
