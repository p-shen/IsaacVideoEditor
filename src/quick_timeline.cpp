#include "quick_timeline.h"
#include <QQuickWindow>
#include "mlt_engine.h"

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
#include <QScopedValueRollback>
#include <QStyleHints>
#include <QUrl>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>

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
void drawMediaStatus(QPainter &p, const QRectF &rect, bool warning) {
    p.save();
    const qreal size = std::min({14.0, rect.width(), rect.height()});
    const QRectF bounds(rect.center().x()-size/2,rect.center().y()-size/2,size,size);
    QPen pen(p.pen().color(),1.4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
    p.setPen(pen);p.setBrush(Qt::NoBrush);
    if(warning) {
        p.drawPolygon(QPolygonF{QPointF(bounds.center().x(),bounds.top()+1),QPointF(bounds.right(),bounds.bottom()-1),QPointF(bounds.left(),bounds.bottom()-1)});
        p.drawLine(QPointF(bounds.center().x(),bounds.top()+5),QPointF(bounds.center().x(),bounds.bottom()-5));
        p.drawPoint(QPointF(bounds.center().x(),bounds.bottom()-3));
    } else p.drawArc(bounds.adjusted(1,1,-1,-1),30*16,270*16);
    p.restore();
}
QVector<int> textLanes(const Project &project,bool caption) {
    const auto &list=caption?project.captions:project.textOverlays;
    QVector<int> order,lanes(list.size());for(int i=0;i<list.size();++i)order.append(i);
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){return project.textStartFrame(a,caption)<project.textStartFrame(b,caption);});
    using End=std::pair<int,int>;std::priority_queue<End,std::vector<End>,std::greater<End>> busy;
    std::priority_queue<int,std::vector<int>,std::greater<int>> free;int count=0;
    for(int i:order){const int start=project.textStartFrame(i,caption);
        while(!busy.empty()&&busy.top().first<=start){free.push(busy.top().second);busy.pop();}
        int lane;if(free.empty())lane=count++;else{lane=free.top();free.pop();}
        lanes[i]=lane;busy.push({start+list[i].durationFrames,lane});
    }return lanes;
}
int extent(const Project &p){int frames=p.durationFrames();for(int i=0;i<p.captions.size();++i)frames=std::max(frames,p.textStartFrame(i,true)+p.captions[i].durationFrames);return frames;}

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
    setImplicitHeight(rulerHeight() + textHeight() + videoHeight() + 2 * audioHeight());
    edgeTimer_.setInterval(16);
    connect(&edgeTimer_, &QTimer::timeout, this, &QuickTimeline::scrollAtEdge);
    qGuiApp->installEventFilter(this);
}

const Project &QuickTimeline::displayProject() const {
    return dragging_ && gesture_ != Gesture::None && gesture_ != Gesture::Scrub ? preview_ : project_;
}
QVariantList QuickTimeline::visualOverlayPreview() const {
    auto p=displayProject();p.normalizeVisuals();QVariantList result;
    for(int i=0;i<p.visualOverlays.size();++i)result.append(QVariantMap{{"startFrame",p.overlayStartFrame(i)},{"durationFrames",p.overlayFrames(i)},{"hostSourceFrame",p.visualOverlays[i].hostSourceFrame}});
    return result;
}
int QuickTimeline::labelWidth() const { return std::max(96, QFontMetrics(font_).horizontalAdvance("Narration") + 30); }
int QuickTimeline::rulerHeight() const { return std::max(32, QFontMetrics(font_).height() + 12); }
int QuickTimeline::videoHeight() const { return std::max(100, QFontMetrics(font_).height() * 3 + 36); }
int QuickTimeline::audioHeight() const { return std::max(54, QFontMetrics(font_).height() + 32); }
void QuickTimeline::updateTextLanes(){
    titleLanes_=textLanes(displayProject(),false);captionLanes_=textLanes(displayProject(),true);
    titleLaneCount_=titleLanes_.isEmpty()?0:*std::max_element(titleLanes_.begin(),titleLanes_.end())+1;
    captionLaneCount_=captionLanes_.isEmpty()?0:*std::max_element(captionLanes_.begin(),captionLanes_.end())+1;
}
int QuickTimeline::titleHeight() const {return titleLaneCount_*audioHeight();}
int QuickTimeline::captionHeight() const {return captionLaneCount_*audioHeight();}
int QuickTimeline::textHeight() const {return titleHeight()+captionHeight();}
bool QuickTimeline::textGesture() const {return gesture_==Gesture::TextBody||gesture_==Gesture::TextStart||gesture_==Gesture::TextEnd;}
double QuickTimeline::origin() const { return labelWidth() + 12; }
double QuickTimeline::xForFrame(int frame) const { return origin() + frame * pixelsPerSecond_ / 60 - contentOffset_; }
double QuickTimeline::unclampedFrameAt(double x) const { return (x - origin() + contentOffset_) * 60 / pixelsPerSecond_; }
int QuickTimeline::frameAt(double x) const {
    if (!std::isfinite(x)) return 0;
    return qBound(0, qRound(unclampedFrameAt(x)), std::max(0, project_.durationFrames() - 1));
}
QRectF QuickTimeline::rectForClip(const Project &p, int index) const {
    if (index < 0 || index >= p.clips.size()) return {};
    return QRectF(xForFrame(p.clipStartFrame(index)), rulerHeight() + textHeight() + 8,
                  std::max(1.0, p.clipFrames(p.clips[index]) * pixelsPerSecond_ / 60), videoHeight() - 16);
}
QRectF QuickTimeline::clipRect(int index) const { return rectForClip(displayProject(), index); }
QRectF QuickTimeline::textRect(int index) const {
    const auto &p=displayProject();if(index<0||index>=p.textOverlays.size())return {};
    const auto &t=p.textOverlays[index];if(index>=titleLanes_.size())return {};
    return {xForFrame(p.textStartFrame(index)),qreal(rulerHeight()+titleLanes_[index]*audioHeight()+5),std::max(1.0,t.durationFrames*pixelsPerSecond_/60),qreal(audioHeight()-10)};
}
QRectF QuickTimeline::captionRect(int index) const {
    const auto &p=displayProject();if(index<0||index>=p.captions.size()||index>=captionLanes_.size())return {};
    return {xForFrame(p.textStartFrame(index,true)),qreal(rulerHeight()+titleHeight()+captionLanes_[index]*audioHeight()+5),std::max(1.0,p.captions[index].durationFrames*pixelsPerSecond_/60),qreal(audioHeight()-10)};
}
int QuickTimeline::captionAt(const QPointF &point) const {
    if(point.x()<labelWidth())return -1;
    for(int i=project_.captions.size()-1;i>=0;--i)if(captionRect(i).contains(point))return i;return -1;
}
int QuickTimeline::textAt(const QPointF &point) const {
    if(point.x()<labelWidth())return -1;
    for(int i=project_.textOverlays.size()-1;i>=0;--i)if(textRect(i).contains(point))return i;
    return -1;
}
void QuickTimeline::setSelectedText(int index){if(index==selectedText_)return;selectedText_=index;emit textSelectionChanged();emit mediaStatusChanged();invalidateContents();}
QRectF QuickTimeline::audioRect(int lane) const {
    if(lane!=1&&lane!=2)return {};
    const auto &p=displayProject();const int start=lane==1?qRound(p.voiceStartSeconds*60):0;
    const int frames=lane==1?std::max(0,std::min(p.durationFrames()-start,qRound(voiceDurationSeconds_*60))):
        p.musicLoopToEnd?p.durationFrames():std::min(p.durationFrames(),qRound(musicDurationSeconds_*60));
    if((lane==1?p.voicePath:p.musicPath).isEmpty()||frames<=0)return {};
    return {xForFrame(start),qreal(rulerHeight()+textHeight()+videoHeight()+(lane-1)*audioHeight()+6),frames*pixelsPerSecond_/60,qreal(audioHeight()-12)};
}
double QuickTimeline::gainY(const QRectF &r,double db) const {return r.bottom()-5-(std::clamp(db,-60.0,12.0)+60)/72*std::max(1.0,r.height()-20);}
bool QuickTimeline::audioGesture() const {return gesture_==Gesture::AudioMove||gesture_==Gesture::AudioGain||gesture_==Gesture::AudioFadeIn||gesture_==Gesture::AudioFadeOut||gesture_==Gesture::Transition;}
QRectF QuickTimeline::transitionRect(int index) const {
    const auto &p=displayProject();if(index<=0||index>=p.clips.size())return {};
    const double width=std::max(14.0,p.transitionFramesBefore(index)*pixelsPerSecond_/60);
    return {xForFrame(p.clipStartFrame(index)),qreal(rulerHeight()+textHeight()+7),width,qreal(std::max(20,QFontMetrics(font_).height()+4))};
}
int QuickTimeline::transitionAt(const QPointF &point) const {
    for (int i=1;i<project_.clips.size();++i) if (transitionRect(i).contains(point)) {
        if (edgeAt(i, point.x()) == Gesture::TrimStart) return -1;
        return i;
    }
    return -1;
}
int QuickTimeline::cutNear(double x) const {int result=-1;double distance=24;for(int i=1;i<project_.clips.size();++i){double d=std::abs(x-xForFrame(project_.clipStartFrame(i)));if(d<distance){distance=d;result=i;}}return result;}
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

void QuickTimeline::setProject(const Project &p, const QVector<double> &durations, double voiceDuration, double musicDuration) {
    if (gesture_ != Gesture::None) finishGesture(true);
    endSkim();
    musicDurationSeconds_=std::isfinite(musicDuration)?std::max(0.0,musicDuration):0;
    project_ = p;
    updateTextLanes();
    setHoveredMedia(-1);
    setImplicitHeight(rulerHeight()+textHeight()+videoHeight()+2*audioHeight());
    sourceDurations_ = durations;
    voiceDurationSeconds_ = std::isfinite(voiceDuration) ? std::max(0.0, voiceDuration) : 0;
    setPosition(position_);
    setSelection(selection_);
    retainSources();
    updateViewport(autoFit_);
    emit gestureChanged();
}
void QuickTimeline::setPosition(int frame) {
    frame = qBound(0, frame, std::max(0, project_.durationFrames() - 1));
    const bool changed = position_ != frame;
    position_ = frame;
    const double x = xForFrame(position_);
    if (changed && width() > origin() + 48) {
        if (x < origin()) setContentOffset(contentOffset_ + x - origin());
        else if (x > width() - 24) setContentOffset(contentOffset_ + x - width() + 24);
    }
    if (changed) emit positionChanged();
    if(changed)update();
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
    emit mediaStatusChanged();
    invalidateContents();
}
void QuickTimeline::setSnapping(bool enabled) {
    if (snapping_ == enabled) return;
    snapping_ = enabled;
    snapSource_ = snapTimeline_ = -1;
    emit snappingChanged();
    if (dragging_) updateGesture(lastModifiers_);
    invalidateContents();
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
    setImplicitHeight(rulerHeight() + textHeight() + videoHeight() + 2 * audioHeight());
    emit fontChanged();
    updateViewport(autoFit_);
}
void QuickTimeline::setWaveformScale(double scale) {
    if (!std::isfinite(scale)) return;
    scale = qBound(1.0, scale, 4.0);
    if (qFuzzyCompare(waveformScale_, scale)) return;
    waveformScale_ = scale;
    emit waveformScaleChanged();
    invalidateContents();
}
void QuickTimeline::setContentOffset(double offset) {
    if (!std::isfinite(offset)) return;
    offset = qBound(0.0, offset, maximumOffset_);
    if (qFuzzyCompare(contentOffset_ + 1, offset + 1)) return;
    contentOffset_ = offset;
    emit viewportChanged();
    refreshMedia();
    invalidateContents();
}
void QuickTimeline::updateViewport(bool fit) {
    updateTextLanes();setImplicitHeight(rulerHeight()+textHeight()+videoHeight()+2*audioHeight());
    const double available = std::max(1.0, width() - labelWidth() - 36);
    if (fit) pixelsPerSecond_ = available / (extent(project_)==0 ? 10.0 : std::max(1.0 / 60, extent(project_) / 60.0));
    // Keep the pre-gesture extent while ripple shortening, or scroll clamping feeds back into the trim delta.
    const int frames = gesture_ == Gesture::None || gesture_ == Gesture::Body ? extent(displayProject())
        : std::max(extent(project_), extent(displayProject()));
    const double content = frames * pixelsPerSecond_ / 60;
    maximumOffset_ = std::max(0.0, content - available);
    contentOffset_ = qBound(0.0, contentOffset_, maximumOffset_);
    emit viewportChanged();
    refreshMedia();
    invalidateContents();
}
bool QuickTimeline::eventFilter(QObject *watched, QEvent *event) {
    if (watched == qGuiApp && event->type() == QEvent::ApplicationPaletteChange) invalidateContents();
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
        connect(cache, &MediaCache::changed, this, [this] { refreshMedia(); invalidateContents(); });
        connect(cache, &QObject::destroyed, this, [this] { filmTiles_.clear(); waveforms_.clear(); waveformStates_.clear(); emit mediaCacheChanged(); emit mediaStatusChanged(); invalidateContents(); });
        retainSources();
        updateInteractive();
    }
    refreshMedia();
    emit mediaCacheChanged();
    invalidateContents();
}
void QuickTimeline::retainSources() {
    if (!mediaCache_) return;
    QStringList paths;
    for(const auto &asset:project_.assets)paths.append(asset.path);
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
    if (!mediaCache_ || width() <= labelWidth()) { emit mediaStatusChanged(); return; }
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
            const double tileStart = clip.inSeconds + dx / pixelsPerSecond_;
            const double tileEnd = clip.inSeconds + (dx + std::min(tileWidth, r.width() - dx)) / pixelsPerSecond_;
            const int firstFrame = std::max(qRound(clip.inSeconds * 60), int(std::ceil(tileStart * 60)));
            const int lastFrame = std::min(qRound(clip.outSeconds * 60) - 1, int(std::ceil(tileEnd * 60)) - 1);
            const int sampleFrame = qRound(sourceTime * 60);
            const int tolerance = std::max(0, std::min(sampleFrame - firstFrame, lastFrame - sampleFrame));
            filmTiles_.append({QRectF(r.left() + dx, r.top() + labelHeight,
                                     std::min(tileWidth, r.width() - dx), std::max(1.0, r.height() - labelHeight - 18)),
                               mediaCache_->thumbnail(clip.path, sourceTime, clip.rotationDegrees, false, tolerance), !QFileInfo::exists(clip.path)});
        }
    }
    const int voiceStart = qRound(p.voiceStartSeconds * 60);
    const double voiceLeft = xForFrame(voiceStart);
    const double voiceRight = xForFrame(std::min(p.durationFrames(), voiceStart + qRound(voiceDurationSeconds_ * 60)));
    if (voiceRight >= visibleLeft && voiceLeft <= visibleRight && voiceRight > voiceLeft) requestWave(p.voicePath);
    if (xForFrame(p.durationFrames()) >= visibleLeft) requestWave(p.musicPath);
    mediaCache_->endViewport();
    emit mediaStatusChanged();
}

QString QuickTimeline::waveformStatus(const QString &path) const {
    if(path.isEmpty()) return {};
    if(!QFileInfo::exists(path)) return "Media missing";
    if(!mediaCache_) return "Media previews unavailable";
    // QML reads status on the GUI thread; viewport-only peaks cannot describe offscreen sources.
    const auto state=mediaCache_->waveformState(path);
    if(state=="unavailable") return "Waveform unavailable";
    if(state=="loading") return "Loading waveform";
    if(state.isEmpty()) return "Waveform not prepared";
    return {};
}
QString QuickTimeline::clipMediaStatus(int index) const {
    if(index<0 || index>=project_.clips.size()) return {};
    const auto &clip=project_.clips[index];
    QStringList details;
    const auto waveform=waveformStatus(clip.path);
    if(!waveform.isEmpty()) details.append(waveform);
    const auto rect=rectForClip(project_,index);
    if(QFileInfo::exists(clip.path) && mediaCache_)
        for(const auto &tile:filmTiles_)
            if(rect.contains(tile.rect.center()) && tile.image.isNull() && !tile.missing) { details.append("Loading filmstrip");break; }
    return details.isEmpty()?QString():QFileInfo(clip.path).fileName()+": "+details.join(" · ");
}
QString QuickTimeline::hoverMediaStatus() const {
    if(hoveredClip_>=0) return clipMediaStatus(hoveredClip_);
    const auto status=waveformStatus(hoveredAudioPath_);
    return status.isEmpty()?QString():QFileInfo(hoveredAudioPath_).fileName()+": "+status;
}
QString QuickTimeline::selectionMediaStatus() const {
    QStringList details;
    if(selectedText_<0) for(const auto &value:selection_) {
        const auto status=clipMediaStatus(value.toInt());
        if(!status.isEmpty()) details.append(status);
    }
    for(const auto &audio:{qMakePair(QString("Narration"),project_.voicePath),qMakePair(QString("Music"),project_.musicPath)}) {
        const auto status=waveformStatus(audio.second);
        if(!status.isEmpty()) details.append(audio.first+": "+QFileInfo(audio.second).fileName()+": "+status);
    }
    return details.join("; ");
}
void QuickTimeline::setHoveredMedia(int clip,const QString &audioPath) {
    if(hoveredClip_==clip && hoveredAudioPath_==audioPath) return;
    hoveredClip_=clip;hoveredAudioPath_=audioPath;emit mediaStatusChanged();
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
    const int hoveredClip=clipAt(event->position());
    const qreal audioTop=rulerHeight()+textHeight()+videoHeight();
    QString audioPath;
    if(event->position().x()>labelWidth()) {
        if(event->position().y()>=audioTop && event->position().y()<audioTop+audioHeight()) audioPath=project_.voicePath;
        else if(event->position().y()>=audioTop+audioHeight() && event->position().y()<audioTop+2*audioHeight()) audioPath=project_.musicPath;
    }
    setHoveredMedia(hoveredClip,audioPath);
    for(int lane:{1,2})if(audioRect(lane).contains(event->position())){
        const auto a=project_.audioControls(lane);
        gestureText_=QString("%1 dB · Fade in %2f · Fade out %3f%4").arg(a.gainDb,0,'f',1).arg(a.fadeInFrames).arg(a.fadeOutFrames).arg(a.muted?" · MUTED":"");
        const auto rect=audioRect(lane);setCursor(std::abs(event->position().y()-gainY(rect,a.gainDb))<5?Qt::SizeVerCursor:Qt::OpenHandCursor);
        endSkim();emit gestureChanged();event->accept();return;
    }
    if(!gestureText_.isEmpty()&&gesture_==Gesture::None){gestureText_.clear();emit gestureChanged();}
    if(transitionAt(event->position())>=0){setCursor(Qt::SizeHorCursor);endSkim();event->accept();return;}
    const int text=textAt(event->position());
    if(text>=0){const auto rect=textRect(text);const double edge=std::min(9.0,rect.width()/3);setCursor(event->position().x()-rect.left()<edge||rect.right()-event->position().x()<edge?Qt::SizeHorCursor:Qt::OpenHandCursor);endSkim();event->accept();return;}
    const int index = clipAt(event->position());
    if (index >= 0) setCursor(edgeAt(index, event->position().x()) == Gesture::Body ? Qt::OpenHandCursor : Qt::SizeHorCursor);
    else unsetCursor();
    skimAt(event->position());
    event->accept();
}
void QuickTimeline::hoverLeaveEvent(QHoverEvent *event) { setHoveredMedia(-1);if(gesture_==Gesture::None){gestureText_.clear();emit gestureChanged();}endSkim(); unsetCursor(); event->accept(); }
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
    dragging_ = false;gestureCaption_=false;
    snapSource_ = snapTimeline_ = -1;
    const int index = clipAt(event->position());
    if (event->position().y() < rulerHeight() && !project_.clips.isEmpty()) {
        gesture_ = Gesture::Scrub;
        scrubStart_ = position_;
        setKeepMouseGrab(true);
        updateInteractive();
        emit scrubStarted();
        seekAt(event->position().x());
    } else if (const int caption=captionAt(event->position());caption>=0) {
        const auto rect=captionRect(caption);const double edge=std::min(9.0,rect.width()/3);
        gesture_=event->position().x()-rect.left()<edge?Gesture::TextStart:rect.right()-event->position().x()<edge?Gesture::TextEnd:Gesture::TextBody;
        gestureCaption_=true;gestureTextIndex_=caption;preview_=project_;emit captionSelected(caption,int(event->modifiers()));setKeepMouseGrab(true);updateInteractive();
    } else if (const int text=textAt(event->position());text>=0) {
        const auto rect=textRect(text);const double edge=std::min(9.0,rect.width()/3);
        gesture_=event->position().x()-rect.left()<edge?Gesture::TextStart:rect.right()-event->position().x()<edge?Gesture::TextEnd:Gesture::TextBody;
        gestureTextIndex_=text;preview_=project_;emit textSelected(text);setKeepMouseGrab(true);updateInteractive();
    } else if(const int cut=transitionAt(event->position());cut>=0){
        gesture_=Gesture::Transition;gestureTransition_=selectedTransition_=cut;preview_=project_;
        emit transitionSelected(cut);emit audioGestureStarted();setKeepMouseGrab(true);updateInteractive();
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
        const double audioTop = rulerHeight() + textHeight() + videoHeight();
        if (event->position().y() >= audioTop && event->position().y() < audioTop + 2 * audioHeight()){
            gestureAudioLane_=event->position().y()<audioTop+audioHeight()?1:2;emit audioSelected(gestureAudioLane_);
            const auto rect=audioRect(gestureAudioLane_);const auto a=project_.audioControls(gestureAudioLane_);
            if(rect.contains(event->position())){
                const double fadeInX=rect.left()+std::min(a.fadeInFrames,int(rect.width()*60/pixelsPerSecond_))*pixelsPerSecond_/60;
                const double fadeOutX=rect.right()-std::min(a.fadeOutFrames,int(rect.width()*60/pixelsPerSecond_))*pixelsPerSecond_/60;
                if(event->position().y()<rect.top()+12&&std::abs(event->position().x()-fadeInX)<10)gesture_=Gesture::AudioFadeIn;
                else if(event->position().y()<rect.top()+12&&std::abs(event->position().x()-fadeOutX)<10)gesture_=Gesture::AudioFadeOut;
                else if(std::abs(event->position().y()-gainY(rect,a.gainDb))<5)gesture_=Gesture::AudioGain;
                else if(gestureAudioLane_==1)gesture_=Gesture::AudioMove;
                if(gesture_!=Gesture::None){preview_=project_;emit audioGestureStarted();setKeepMouseGrab(true);updateInteractive();}
            }
        }else emit clearSelectionRequested();
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
QRectF QuickTimeline::reorderLandingRect() const {
    if (!dragging_ || gesture_ != Gesture::Body || group_.isEmpty()) return {};
    const int first = previewOrder_.indexOf(group_.first());
    return rectForClip(preview_, first).united(rectForClip(preview_, first + group_.size() - 1));
}
QString QuickTimeline::reorderPlacementLabel() const {
    if (group_.isEmpty()) return {};
    const int next = previewOrder_.indexOf(group_.first()) + group_.size();
    return next < previewOrder_.size()
        ? QString("Place before Clip %1").arg(previewOrder_[next] + 1)
        : QString("Place at end");
}
void QuickTimeline::updateGesture(Qt::KeyboardModifiers modifiers) {
    if(dragging_&&audioGesture()){
        preview_=project_;const int delta=qRound((lastPoint_.x()-pressPoint_.x()+contentOffset_-pressOffset_)*60/pixelsPerSecond_);
        if(gesture_==Gesture::Transition){
            const int requested=std::clamp(project_.transitionFramesBefore(gestureTransition_)+delta,0,600);
            preview_.setTransition(gestureTransition_,requested?"dissolve":"cut",requested);
            const int effective=preview_.transitionFramesBefore(gestureTransition_);
            gestureText_=QString("Dissolve · %1 frames overlap · Project %2%3").arg(effective).arg(timecode(preview_.durationFrames())).arg(effective<requested?" · Clamped to playable media":"");
            emit transitionPreviewRequested(gestureTransition_,requested);
        }else{
            auto a=project_.audioControls(gestureAudioLane_);QVariantMap fields;
            const auto rect=audioRect(gestureAudioLane_);
            const int frames=qRound(rect.width()*60/pixelsPerSecond_);
            if(gesture_==Gesture::AudioMove){const int start=std::clamp(qRound(project_.voiceStartSeconds*60)+delta,0,std::max(0,project_.durationFrames()-1));preview_.voiceStartSeconds=start/60.0;fields["startFrame"]=start;gestureText_=QString("Narration start · %1 · %2 frames").arg(timecode(start)).arg(start);}
            else if(gesture_==Gesture::AudioGain){a.gainDb=std::clamp(a.gainDb+(pressPoint_.y()-lastPoint_.y())*72/std::max(1.0,rect.height()-20),-60.0,12.0);fields["gainDb"]=a.gainDb;gestureText_=QString("%1 dB").arg(a.gainDb,0,'f',1);}
            else {const bool incoming=gesture_==Gesture::AudioFadeIn;int value=std::clamp((incoming?a.fadeInFrames:a.fadeOutFrames)+(incoming?delta:-delta),0,frames);if(incoming)a.fadeInFrames=value;else a.fadeOutFrames=value;fields[incoming?"fadeInFrames":"fadeOutFrames"]=value;gestureText_=QString("Fade %1 · %2 frames · %3 s").arg(incoming?"in":"out").arg(value).arg(value/60.0,0,'f',3);}
            preview_.setAudioControls(gestureAudioLane_,a);emit audioPreviewRequested(gestureAudioLane_,fields);
        }
        setCursor(gesture_==Gesture::AudioGain?Qt::SizeVerCursor:Qt::SizeHorCursor);emit gestureChanged();updateViewport();return;
    }
    const auto &textList=gestureCaption_?project_.captions:project_.textOverlays;
    if(dragging_&&textGesture()&&gestureTextIndex_>=0&&gestureTextIndex_<textList.size()){
        preview_=project_;const auto &original=textList[gestureTextIndex_];auto &text=(gestureCaption_?preview_.captions:preview_.textOverlays)[gestureTextIndex_];
        const int start=project_.textStartFrame(gestureTextIndex_,gestureCaption_);
        const int delta=qRound((lastPoint_.x()-pressPoint_.x()+contentOffset_-pressOffset_)*60/pixelsPerSecond_);
        const int end=start+original.durationFrames;
        int value=gesture_==Gesture::TextEnd?end+delta:start+delta;
        const int minimum=gesture_==Gesture::TextEnd?start+1:0;
        const int limit=gestureCaption_?86400*60:project_.durationFrames();
        int maximum=gesture_==Gesture::TextStart?end-1:gesture_==Gesture::TextBody?std::max(0,limit-original.durationFrames):limit;
        if(gestureCaption_&&!original.hostId.isEmpty()&&gesture_!=Gesture::TextEnd)maximum=std::min(maximum,std::max(0,project_.durationFrames()-1));
        value=qBound(minimum,value,std::max(minimum,maximum));snapTimeline_=-1;
        if(snapping_&&!modifiers.testFlag(Qt::AltModifier)){
            QVector<int> targets{0,position_,project_.durationFrames()};
            for(int i=0;i<project_.clips.size();++i){const int clipStart=project_.clipStartFrame(i);targets<<clipStart<<clipStart+project_.clipFrames(project_.clips[i]);}
            for(bool caption:{false,true}){const auto &list=caption?project_.captions:project_.textOverlays;for(int i=0;i<list.size();++i)if(caption!=gestureCaption_||i!=gestureTextIndex_){const int at=project_.textStartFrame(i,caption);targets<<at<<at+list[i].durationFrames;}}
            double distance=9;for(int target:targets){const double d=std::abs(target-value)*pixelsPerSecond_/60;if(target>=minimum&&target<=maximum&&d<=8&&d<distance){distance=d;snapTimeline_=target;}}
            if(snapTimeline_>=0)value=snapTimeline_;
        }
        text.hostId.clear();
        if(gesture_==Gesture::TextBody)text.startFrame=value;
        else if(gesture_==Gesture::TextStart){text.startFrame=value;text.durationFrames=end-value;}
        else {text.startFrame=start;text.durationFrames=value-start;}
        gestureText_=QString("%1 · %2 – %3 · %4 frames%5").arg(gestureCaption_?"Caption":"Text",timecode(text.startFrame),timecode(text.startFrame+text.durationFrames)).arg(text.durationFrames).arg(text.startFrame+text.durationFrames>project_.durationFrames()?" · video clamped at project end":"");
        setCursor(gesture_==Gesture::TextBody?Qt::ClosedHandCursor:Qt::SizeHorCursor);
        emit gestureChanged();updateViewport();return;
    }
    if (!dragging_ || gestureClip_ < 0 || gestureClip_ >= project_.clips.size()) return;
    preview_ = project_;
    if (gesture_ == Gesture::Body) {
        moveBoundary_ = boundaryAt(lastPoint_.x());
        previewOrder_ = reorderedIndices(moveBoundary_);
        preview_.reorderClips(previewOrder_);
        setCursor(Qt::ClosedHandCursor);
        const int start = preview_.clipStartFrame(previewOrder_.indexOf(group_.first()));
        gestureText_ = QString("%1 · Start %2 · Duration %3").arg(reorderPlacementLabel(), timecode(start), timecode(preview_.durationFrames()));
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
    preview_.normalizeVisuals();
    for(int i=0;i<preview_.visualOverlays.size();++i)if(i<project_.visualOverlays.size()&&(preview_.overlayStartFrame(i)!=project_.overlayStartFrame(i)||preview_.overlayFrames(i)!=project_.overlayFrames(i)))
        gestureText_+=QString(" · Overlay %1 → start %2, %3 frames").arg(i+1).arg(preview_.overlayStartFrame(i)).arg(preview_.overlayFrames(i));
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
    const bool wasAudio=audioGesture();
    const Gesture action = gesture_;
    const bool dragged = dragging_;
    const bool select = deferredSelection_ && !dragged && !canceled;
    const int index = gestureClip_;
    const int boundary = moveBoundary_;
    const Project preview = preview_;
    const auto order = previewOrder_;
    const auto group = group_;
    const int textIndex=gestureTextIndex_;const bool caption=gestureCaption_;
    bool reordered = false;
    if (action == Gesture::Body && !canceled && dragged && index >= 0 && index < project_.clips.size() && boundary >= 0)
        for (int i = 0; i < order.size(); ++i) reordered |= order[i] != i;
    gesture_ = Gesture::None;
    gestureClip_ = moveBoundary_ = -1;
    gestureTextIndex_=-1;gestureCaption_=false;
    dragging_ = deferredSelection_ = false;
    snapSource_ = snapTimeline_ = -1;
    gestureText_.clear();
    trimPreview_ = {{"active", false}};
    edgeTimer_.stop();
    setKeepMouseGrab(false);
    unsetCursor();
    updateInteractive();
    if (reordered) {
        QVariantList indices;
        for (int i : group) indices.append(i);
        // Keep the landing viewport through the controller's synchronous project refresh.
        QScopedValueRollback<bool> keepScale(autoFit_, false);
        emit groupMoveRequested(indices, boundary);
    }
    updateViewport();
    setImplicitHeight(rulerHeight()+textHeight()+videoHeight()+2*audioHeight());
    emit gestureChanged();
    if(wasAudio){gestureTransition_=-1;emit audioGestureFinished(canceled);return;}
    if (action == Gesture::Scrub) {
        if (canceled) { setPosition(scrubStart_); emit seekRequested(position_); }
        emit scrubFinished(canceled);
    }
    if (select) emit selectionRequested(index, int(Qt::NoModifier));
    if(action==Gesture::TextBody||action==Gesture::TextStart||action==Gesture::TextEnd){
        if(!canceled&&dragged&&textIndex>=0&&textIndex<(caption?preview.captions:preview.textOverlays).size()){
            const auto &t=(caption?preview.captions:preview.textOverlays)[textIndex];if(caption)emit captionTimingRequested(textIndex,t.startFrame,t.durationFrames);else emit textTimingRequested(textIndex,t.startFrame,t.durationFrames);
        }emit textGestureFinished(canceled);return;
    }
    if (canceled || !dragged || index < 0 || index >= project_.clips.size()) return;
    if (action == Gesture::TrimStart || action == Gesture::TrimEnd) {
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
    if(gesture_==Gesture::None&&selectedCaption_>=0&&selectedCaption_<project_.captions.size()&&event->modifiers().testFlag(Qt::AltModifier)&&(event->key()==Qt::Key_Left||event->key()==Qt::Key_Right)){
        const auto &t=project_.captions[selectedCaption_];const int delta=event->key()==Qt::Key_Left?-1:1;int start=project_.textStartFrame(selectedCaption_,true),frames=t.durationFrames;
        if(event->modifiers().testFlag(Qt::ShiftModifier))frames=qBound(1,frames+delta,86400*60-start);else start=qBound(0,start+delta,86400*60-frames);
        emit captionTimingRequested(selectedCaption_,start,frames);event->accept();return;
    }
    if(gesture_==Gesture::None&&selectedText_>=0&&selectedText_<project_.textOverlays.size()&&event->modifiers().testFlag(Qt::AltModifier)&&(event->key()==Qt::Key_Left||event->key()==Qt::Key_Right)){
        const auto &text=project_.textOverlays[selectedText_];const int delta=event->key()==Qt::Key_Left?-1:1;
        int start=project_.textStartFrame(selectedText_),frames=text.durationFrames;
        if(event->modifiers().testFlag(Qt::ShiftModifier))frames=qBound(1,frames+delta,std::max(1,project_.durationFrames()-start));
        else start=qBound(0,start+delta,std::max(0,project_.durationFrames()-frames));
        emit textTimingRequested(selectedText_,start,frames);event->accept();return;
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
void QuickTimeline::updateDropPreview(double x,const QMimeData *mime) {
    const auto paths=droppedFiles(mime);
    dropActive_=!paths.isEmpty();dropValid_=dropActive_;
    dropFrame_=qBound(0,qRound(unclampedFrameAt(x)),project_.durationFrames());
    Project proposed=project_;int insertion=dropFrame_;
    for(const auto &path:paths) {
        const auto source=std::find_if(project_.assets.cbegin(),project_.assets.cend(),[&](const MediaAsset &a){return a.path==path;});
        int first=0,last=0;
        if(source!=project_.assets.cend()) {
            if(source->kind!="video"){dropValid_=false;break;}
            first=source->inFrame;last=source->outFrame;
        }else {
            if(!dropDurations_.contains(path)){const auto info=Media::mediaInfo(path);dropDurations_[path]=info&&info->width>0&&info->height>0?qRound(info->durationSeconds*60):0;}
            last=dropDurations_[path];
        }
        if(last<=first || proposed.insertClipAtFrame({path,first/60.0,last/60.0},insertion)<0){dropValid_=false;break;}
        insertion+=last-first;
    }
    gestureText_=dropValid_?QString("Insert at %1 · Result %2 frames (%3 s)").arg(dropFrame_).arg(proposed.durationFrames()).arg(proposed.durationFrames()/60.0,0,'f',3):
        "Cannot insert here. Use a valid video range outside crossfades, or drop into Media to import only.";
    emit gestureChanged();invalidateContents();
}
void QuickTimeline::dragEnterEvent(QDragEnterEvent *event) {
    if(event->mimeData()->hasFormat("application/x-isaac-transition")){dropActive_=true;event->acceptProposedAction();invalidateContents();return;}
    dropDurations_.clear();updateDropPreview(event->position().x(),event->mimeData());
    if(dropActive_)event->acceptProposedAction();else event->ignore();
}
void QuickTimeline::dragMoveEvent(QDragMoveEvent *event) {
    if(event->mimeData()->hasFormat("application/x-isaac-transition")){
        const int cut=cutNear(event->position().x());
        if(cut>=0){if(presetCut_<0){preview_=project_;gesture_=Gesture::Transition;dragging_=true;emit audioGestureStarted();}presetCut_=gestureTransition_=cut;
            preview_=project_;preview_.setTransition(cut,"dissolve",30);emit transitionSelected(cut);emit transitionPreviewRequested(cut,30);
            gestureText_=QString("Dissolve · %1 frames · Project %2").arg(preview_.transitionFramesBefore(cut)).arg(timecode(preview_.durationFrames()));event->acceptProposedAction();emit gestureChanged();updateViewport();}
        else event->ignore();return;
    }
    updateDropPreview(event->position().x(),event->mimeData());
    if(dropActive_)event->acceptProposedAction();else event->ignore();
}
void QuickTimeline::dragLeaveEvent(QDragLeaveEvent *event) {
    if(presetCut_>=0){presetCut_=-1;finishGesture(true);}
    dropActive_=false;gestureText_.clear();dropDurations_.clear();emit gestureChanged();event->accept();invalidateContents();
}
void QuickTimeline::dropEvent(QDropEvent *event) {
    if(event->mimeData()->hasFormat("application/x-isaac-transition")){
        const int cut=cutNear(event->position().x());const bool valid=cut>=0;
        if(presetCut_>=0){presetCut_=-1;finishGesture(!valid);}
        else if(valid)emit transitionPresetRequested(cut,"dissolve",30);
        if(valid)event->acceptProposedAction();else event->ignore();dropActive_=false;invalidateContents();return;
    }
    const auto paths=droppedFiles(event->mimeData());
    updateDropPreview(event->position().x(),event->mimeData());
    dropActive_=false;
    if(dropValid_ && !paths.isEmpty()){event->acceptProposedAction();emit filesInserted(paths,dropFrame_);emit filesDropped(paths);}else event->ignore();
    gestureText_.clear();dropDurations_.clear();emit gestureChanged();invalidateContents();
}

void QuickTimeline::paintContents(QPainter *painter) {
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
    const QColor divider = palette.color(QPalette::Mid);
    const int labels = labelWidth();
    const int ruler = rulerHeight()+textHeight();
    const int video = videoHeight();
    const int audio = audioHeight();
    const Project &project = displayProject();
    p.fillRect(boundingRect(), base);
    p.fillRect(QRectF(0, 0, width(), rulerHeight()), surface);
    p.fillRect(QRectF(0, ruler, labels, std::max(0.0, height() - ruler)), surface);
    p.setPen(divider);
    for (int y : {ruler, ruler + video, ruler + video + audio, ruler + video + 2 * audio}) p.drawLine(QPointF(0, y), QPointF(width(), y));
    p.drawLine(QPointF(labels, 0), QPointF(labels, height()));
    p.setPen(muted);
    p.drawText(QRectF(12, 0, labels - 20, rulerHeight()), Qt::AlignVCenter, "TRACKS");
    p.setPen(text);
    if(titleHeight()>0)p.drawText(QRectF(12,rulerHeight(),labels-20,titleHeight()),Qt::AlignVCenter,"Text");
    if(captionHeight()>0)p.drawText(QRectF(12,rulerHeight()+titleHeight(),labels-20,captionHeight()),Qt::AlignVCenter,"Captions");
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
        p.drawLine(QPointF(x, rulerHeight() - 7), QPointF(x, ruler + video + 2 * audio));
        p.setPen(muted);
        p.drawText(QRectF(x + 5, 0, spacing, rulerHeight() - 5), Qt::AlignVCenter, timecode(qRound(sec * 60)));
    }
    if (project.clips.isEmpty()) {
        const QRectF r(origin(), ruler + 8, std::max(1.0, width() - origin() - 24), video - 16);
        p.setPen(QPen(dropActive_ ? accent : divider, 1.5, Qt::DashLine));
        p.setBrush(blend(base, accent, dropActive_ ? 0.18 : 0.03));
        p.drawRoundedRect(r, 5, 5);
        p.setPen(muted);
        p.drawText(r.adjusted(12, 0, -12, 0), Qt::AlignCenter | Qt::TextWordWrap, "Drop clips");
    }
    for(bool caption:{false,true}){const auto &list=caption?project.captions:project.textOverlays;for(int i=0;i<list.size();++i){
        const auto rect=caption?captionRect(i):textRect(i);const auto &t=list[i];if(rect.right()<labels||rect.left()>width())continue;
        const bool selected=caption?captionSelection_.contains(i):i==selectedText_;p.setPen(QPen(selected?accent:divider,selected?2:1));p.setBrush(blend(base,accent,selected?.24:.12));p.drawRoundedRect(rect.adjusted(1,1,-1,-1),4,4);
        p.save();p.setClipRect(rect.adjusted(10,2,-10,-2),Qt::IntersectClip);p.setPen(text);
        const QString name=t.text.section('\n',0,0).isEmpty()?QString(caption?"Caption %1":"Text %1").arg(i+1):t.text.section('\n',0,0);
        p.drawText(rect.adjusted(12,2,-12,-rect.height()/2),Qt::AlignVCenter,fm.elidedText(name,Qt::ElideRight,std::max(0,int(rect.width()-24))));
        const int at=project.textStartFrame(i,caption);const QString status=QString("%1 – %2%3%4").arg(timecode(at),timecode(at+t.durationFrames),t.hostId.isEmpty()?"":" · attached",caption&&at+t.durationFrames>project.durationFrames()?" · clamped":"");
        p.drawText(rect.adjusted(12,rect.height()/2,-12,-2),Qt::AlignVCenter,status);p.restore();
        if(selected){p.setPen(QPen(accent,2));for(double x:{rect.left()+4,rect.right()-4})p.drawLine(QPointF(x,rect.center().y()-8),QPointF(x,rect.center().y()+8));}
    }}
    const auto drawWave = [&](const QString &path, const QRectF &rect, double sourceStart, bool loop, double sourceDuration, const QColor &color, bool stateLabel) {
        const auto peaks = waveforms_.constFind(path);
        if (peaks == waveforms_.cend() || peaks->isEmpty()) {
            if (stateLabel && rect.width() > 22) {
                p.setPen(muted);
                const qreal left=std::max(double(labels+6),rect.left()+6);
                if(left+14<rect.right()) drawMediaStatus(p,QRectF(left,rect.top(),14,rect.height()),!QFileInfo::exists(path) || waveformStates_.value(path)=="unavailable" || !mediaCache_);
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
        p.setPen(QPen(selected ? accent : divider, selected ? 2 : 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
        p.save();
        p.setClipRect(r.adjusted(2, 2, -2, -2), Qt::IntersectClip);
        bool filmstripLoading=false;
        bool mediaMissing=!QFileInfo::exists(clip.path);
        for (const FilmTile &tile : filmTiles_) {
            if (!r.contains(tile.rect.center())) continue;
            if (!tile.image.isNull()) {
                const QSizeF imageSize = tile.image.size().scaled(tile.rect.size().toSize(), Qt::KeepAspectRatio);
                const QRectF imageRect(tile.rect.center().x() - imageSize.width() / 2, tile.rect.center().y() - imageSize.height() / 2, imageSize.width(), imageSize.height());
                p.fillRect(tile.rect, blend(base, text, 0.05));
                p.drawImage(imageRect, tile.image);
            } else {
                p.fillRect(tile.rect.adjusted(1, 1, -1, -1), blend(base, text, 0.06));
                filmstripLoading |= !tile.missing;
                mediaMissing |= tile.missing;
            }
        }
        const double labelHeight = fm.height() + 6;
        p.fillRect(QRectF(r.left(), r.top(), r.width(), labelHeight), blend(base, accent, selected ? 0.24 : 0.12));
        p.setPen(text);
        const QString label = QString::number(originalIndex + 1) + "  " + QFileInfo(clip.path).fileName()+(clip.audio.muted?" · MUTED":"");
        const qreal labelRight=std::min(width()-4,r.right()-6);
        const bool status=mediaMissing || filmstripLoading || !mediaCache_;
        const qreal labelLeft=std::max(r.left()+12,double(labels+8));
        const qreal labelWidth=std::max(0.0,labelRight-labelLeft-(status?22:0));
        p.drawText(QRectF(labelLeft,r.top(),labelWidth,labelHeight),Qt::AlignVCenter,fm.elidedText(label,Qt::ElideRight,int(labelWidth)));
        if(status && labelRight>labels+24) {p.setPen(muted);drawMediaStatus(p,QRectF(labelRight-14,r.top(),14,labelHeight),mediaMissing || !mediaCache_);}
        drawWave(clip.path, QRectF(r.left(), r.bottom() - 17, r.width(), 16), clip.inSeconds, false, 0, clip.audio.muted?muted:blend(text, accent, 0.45), true);
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
    for(int i=1;i<project.clips.size();++i){
        const auto r=transitionRect(i);if(r.right()<labels||r.left()>width())continue;
        p.setBrush(blend(base,accent,selectedTransition_==i?.5:.24));p.setPen(QPen(accent,selectedTransition_==i?2:1));p.drawRoundedRect(r,3,3);
        p.setPen(text);p.drawText(r.adjusted(2,0,-2,0),Qt::AlignCenter,project.transitionFramesBefore(i)>0?QString("%1f").arg(project.transitionFramesBefore(i)):"|");
    }
    const auto audioBlock = [&](int lane, const QString &path, int start, int frames) {
        const int y = ruler + video + (lane - 1) * audio;
        if (path.isEmpty() || frames <= 0) {
            const QString hint = path.isEmpty() ? (lane == 1 ? "Add narration" : "Add music")
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
        drawWave(path, r.adjusted(0, fm.height() + 5, 0, -1), 0, lane == 2&&project.musicLoopToEnd, lane==2?musicDurationSeconds_:duration, blend(text, tint, 0.45), true);
        p.setPen(text);
        const auto controls=project.audioControls(lane);
        if(controls.muted){p.fillRect(r,QColor(90,90,90,70));}
        p.setPen(QPen(text,1.5));const double levelY=gainY(r,controls.gainDb);p.drawLine(QPointF(r.left(),levelY),QPointF(r.right(),levelY));
        const int playable=qRound(r.width()*60/pixelsPerSecond_);
        const double inX=r.left()+std::min(controls.fadeInFrames,playable)*pixelsPerSecond_/60;
        const double outX=r.right()-std::min(controls.fadeOutFrames,playable)*pixelsPerSecond_/60;
        p.setPen(QPen(accent,2));p.drawLine(QPointF(r.left(),r.bottom()-3),QPointF(inX,levelY));p.drawLine(QPointF(outX,levelY),QPointF(r.right(),r.bottom()-3));
        p.setBrush(text);p.drawRect(QRectF(inX-4,r.top()+2,8,8));p.drawRect(QRectF(outX-4,r.top()+2,8,8));
        p.setPen(text);
        const QString label=QFileInfo(path).fileName()+QString(" · %1 dB%2%3").arg(controls.gainDb,0,'f',1).arg(controls.muted?" · MUTED":"").arg(lane==2&&project.musicLoopToEnd?" · Loops to end":"");
        const double labelLeft = std::max(r.left() + 8, double(labels + 8));
        p.drawText(QRectF(labelLeft, r.top() + 2, std::max(0.0, r.right() - labelLeft - 8), fm.height() + 3), Qt::AlignVCenter, fm.elidedText(label, Qt::ElideRight, std::max(0, int(std::min(width(), r.right()) - labelLeft - 8))));
        p.restore();
    };
    const int voiceStart = qRound(project.voiceStartSeconds * 60);
    audioBlock(1, project.voicePath, voiceStart, std::max(0, std::min(project.durationFrames() - voiceStart, qRound(voiceDurationSeconds_ * 60))));
    audioBlock(2, project.musicPath, 0, project.musicLoopToEnd?project.durationFrames():std::min(project.durationFrames(),qRound(musicDurationSeconds_*60)));
    if(dropActive_){
        const double x=xForFrame(dropFrame_);p.setPen(QPen(dropValid_?accent:text,3,dropValid_?Qt::SolidLine:Qt::DashLine));
        p.drawLine(QPointF(x,ruler),QPointF(x,ruler+video));
        p.drawText(QRectF(x+6,ruler+4,180,fm.height()+4),Qt::AlignVCenter,dropValid_?"Insert source range":"Insertion unavailable");
    }
    const QRectF landing = reorderLandingRect();
    if (!landing.isEmpty()) {
        QColor tint = accent;
        tint.setAlpha(40);
        p.setBrush(tint);
        p.setPen(QPen(accent, 2, Qt::DashLine));
        p.drawRoundedRect(landing.adjusted(1, 1, -1, -1), 4, 4);
        const double x = landing.left();
        p.setPen(QPen(accent, 3));
        p.drawLine(QPointF(x, ruler + 3), QPointF(x, ruler + video - 3));
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawPolygon(QPolygonF{QPointF(x-5, ruler+3), QPointF(x+5, ruler+3), QPointF(x, ruler+9)});

        const QString label = reorderPlacementLabel();
        const double available = std::max(0.0, width() - labels - 16);
        const double labelWidth = std::min(available, std::ceil(QFontMetricsF(font_).horizontalAdvance(label)) + 16);
        if (labelWidth > 16) {
            const double left = qBound(double(labels + 8), x + 8, width() - labelWidth - 8);
            const QRectF labelRect(left, ruler + video - fm.height() - 18, labelWidth, fm.height() + 10);
            p.setBrush(base);
            p.setPen(QPen(accent, 1));
            p.drawRoundedRect(labelRect, 4, 4);
            p.setPen(text);
            p.drawText(labelRect.adjusted(8, 0, -8, 0), Qt::AlignVCenter,
                       fm.elidedText(label, Qt::ElideRight, int(labelWidth - 16)));
        }
    }
    if (snapTimeline_ >= 0 && dragging_) {
        const double x = xForFrame(snapTimeline_);
        p.setPen(QPen(accent, 1, Qt::DashLine));
        p.drawLine(QPointF(x, ruler), QPointF(x, ruler + video + 2 * audio));
    }
    p.restore();
}

void QuickTimeline::paint(QPainter *painter) {
    const qreal ratio=window()?window()->effectiveDevicePixelRatio():1.0;
    const QSize pixels(qCeil(width()*ratio),qCeil(height()*ratio));
    if(pixels.isEmpty())return;
    if(contentsDirty_||contents_.size()!=pixels||contents_.devicePixelRatio()!=ratio){
        contents_=QImage(pixels,QImage::Format_ARGB32_Premultiplied);
        contents_.setDevicePixelRatio(ratio);contents_.fill(Qt::transparent);
        QPainter cached(&contents_);paintContents(&cached);contentsDirty_=false;
    }
    QPainter &p=*painter;
    p.drawImage(QPointF(0,0),contents_);
    p.setRenderHint(QPainter::Antialiasing);p.setFont(font_);
    const QFontMetrics fm(font_);const auto palette=QGuiApplication::palette();
    const auto accent=palette.color(QPalette::Highlight),text=palette.color(QPalette::Text);
    const auto &project=displayProject();
    const int ruler=rulerHeight()+textHeight(),video=videoHeight(),audio=audioHeight();
    p.save();p.setClipRect(QRectF(labelWidth()+1,0,std::max(0.0,width()-labelWidth()-1),height()));
    if (!project.clips.isEmpty()) {
        const double x = xForFrame(position_);
        p.setPen(QPen(accent, 1.5));
        p.drawLine(QPointF(x, rulerHeight() - 8), QPointF(x, ruler + video + 2 * audio));
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
