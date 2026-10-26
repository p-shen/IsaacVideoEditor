#pragma once

#include "project.h"
#include "media_cache.h"

#include <QFont>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class QMimeData;

class QuickTimeline : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList visualOverlayPreview READ visualOverlayPreview NOTIFY gestureChanged)
    Q_PROPERTY(int position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(QVariantList selection READ selection WRITE setSelection NOTIFY selectionChanged)
    Q_PROPERTY(int selectedCaption READ selectedCaption WRITE setSelectedCaption NOTIFY captionSelectionChanged)
    Q_PROPERTY(QVariantList selectedCaptions READ selectedCaptions WRITE setSelectedCaptions NOTIFY captionSelectionChanged)
    Q_PROPERTY(int selectedText READ selectedText WRITE setSelectedText NOTIFY textSelectionChanged)
    Q_PROPERTY(int selectedTransition READ selectedTransition WRITE setSelectedTransition NOTIFY transitionSelectionChanged)
    Q_PROPERTY(bool snapping READ snapping WRITE setSnapping NOTIFY snappingChanged)
    Q_PROPERTY(bool skimming READ skimming WRITE setSkimming NOTIFY skimmingChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(double contentOffset READ contentOffset WRITE setContentOffset NOTIFY viewportChanged)
    Q_PROPERTY(double maximumOffset READ maximumOffset NOTIFY viewportChanged)
    Q_PROPERTY(double pixelsPerSecond READ pixelsPerSecond NOTIFY viewportChanged)
    Q_PROPERTY(QString hoverMediaStatus READ hoverMediaStatus NOTIFY mediaStatusChanged)
    Q_PROPERTY(QString selectionMediaStatus READ selectionMediaStatus NOTIFY mediaStatusChanged)
    Q_PROPERTY(QString gestureText READ gestureText NOTIFY gestureChanged)
    Q_PROPERTY(QVariantMap trimPreview READ trimPreview NOTIFY gestureChanged)
    Q_PROPERTY(MediaCache* mediaCache READ mediaCache WRITE setMediaCache NOTIFY mediaCacheChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(double waveformScale READ waveformScale WRITE setWaveformScale NOTIFY waveformScaleChanged)
public:
    explicit QuickTimeline(QQuickItem *parent = nullptr);
    void setProject(const Project &, const QVector<double> &sourceDurations = {}, double voiceDurationSeconds = 0, double musicDurationSeconds = 0);
    QVariantList visualOverlayPreview() const;
    int position() const { return position_; }
    void setPosition(int frame);
    QVariantList selection() const { return selection_; }
    void setSelection(const QVariantList &selection);
    int selectedCaption() const {return selectedCaption_;}
    void setSelectedCaption(int index){if(index==selectedCaption_)return;selectedCaption_=index;emit captionSelectionChanged();invalidateContents();}
    QVariantList selectedCaptions() const {return captionSelection_;}
    void setSelectedCaptions(const QVariantList &indices){if(indices==captionSelection_)return;captionSelection_=indices;emit captionSelectionChanged();invalidateContents();}
    int selectedText() const { return selectedText_; }
    void setSelectedText(int index);
    int selectedTransition() const {return selectedTransition_;}
    void setSelectedTransition(int index) {if(index==selectedTransition_)return;selectedTransition_=index;emit transitionSelectionChanged();invalidateContents();}
    bool snapping() const { return snapping_; }
    void setSnapping(bool enabled);
    bool skimming() const { return skimming_; }
    void setSkimming(bool enabled);
    bool playing() const { return playing_; }
    void setPlaying(bool playing);
    double contentOffset() const { return contentOffset_; }
    void setContentOffset(double offset);
    double maximumOffset() const { return maximumOffset_; }
    double pixelsPerSecond() const { return pixelsPerSecond_; }
    QString gestureText() const { return gestureText_; }
    QVariantMap trimPreview() const { return trimPreview_; }
    MediaCache *mediaCache() const;
    void setMediaCache(MediaCache *cache);
    QFont font() const { return font_; }
    void setFont(const QFont &font);
    double waveformScale() const { return waveformScale_; }
    void setWaveformScale(double scale);
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void fitToWidth();
    Q_INVOKABLE void cancelGesture() { if(gesture_!=Gesture::None)finishGesture(true); }
    Q_INVOKABLE void zoomAt(double factor, double anchorX);
    Q_INVOKABLE int frameAt(double x) const;
    Q_INVOKABLE QRectF clipRect(int index) const;
    Q_INVOKABLE QRectF audioRect(int lane) const;
    Q_INVOKABLE QRectF transitionRect(int index) const;
    Q_INVOKABLE QRectF textRect(int index) const;
    Q_INVOKABLE QRectF captionRect(int index) const;
    Q_INVOKABLE int labelWidth() const;
    QString hoverMediaStatus() const;
    QString selectionMediaStatus() const;
    void paint(QPainter *) override;

signals:
    void mediaStatusChanged();
    void positionChanged();
    void selectionChanged();
    void textSelectionChanged();
    void transitionSelectionChanged();
    void captionSelectionChanged();
    void captionSelected(int index,int modifiers);
    void captionTimingRequested(int index,int start,int frames);
    void textSelected(int index);
    void textGestureFinished(bool canceled);
    void textTimingRequested(int index,int start,int frames);
    void snappingChanged();
    void skimmingChanged();
    void playingChanged();
    void viewportChanged();
    void gestureChanged();
    void mediaCacheChanged();
    void fontChanged();
    void waveformScaleChanged();
    void seekRequested(int frame);
    void scrubStarted();
    void scrubFinished(bool canceled);
    void skimRequested(int frame);
    void skimEnded();
    void selectionRequested(int index, int modifiers);
    void clearSelectionRequested();
    void groupMoveRequested(const QVariantList &indices, int boundary);
    void trimRequested(int index, double inSeconds, double outSeconds);
    void audioSelected(int lane);
    void audioGestureStarted();
    void audioPreviewRequested(int lane,const QVariantMap &fields);
    void audioGestureFinished(bool canceled);
    void transitionSelected(int index);
    void transitionPreviewRequested(int index,int frames);
    void transitionPresetRequested(int index,const QString &type,int frames);
    void filesDropped(const QStringList &paths);
    void filesInserted(const QStringList &paths,int frame);

protected:
    bool eventFilter(QObject *, QEvent *) override;
    void geometryChange(const QRectF &, const QRectF &) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseUngrabEvent() override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void hoverMoveEvent(QHoverEvent *) override;
    void hoverLeaveEvent(QHoverEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    void paintContents(QPainter *);
    void invalidateContents() { contentsDirty_=true; update(); }
    QImage contents_;
    bool contentsDirty_=true;
    enum class Gesture { None, Scrub, Body, TrimStart, TrimEnd, TextBody, TextStart, TextEnd, AudioMove, AudioGain, AudioFadeIn, AudioFadeOut, Transition };
    struct FilmTile { QRectF rect; QImage image; bool missing = false; };
    const Project &displayProject() const;
    int rulerHeight() const;
    int videoHeight() const;
    int audioHeight() const;
    int textHeight() const;
    int titleHeight() const;
    int captionHeight() const;
    void updateTextLanes();
    int captionAt(const QPointF &) const;
    int textAt(const QPointF &) const;
    int transitionAt(const QPointF &) const;
    int cutNear(double x) const;
    bool audioGesture() const;
    double gainY(const QRectF &,double db) const;
    bool textGesture() const;
    double origin() const;
    double xForFrame(int frame) const;
    double unclampedFrameAt(double x) const;
    QRectF rectForClip(const Project &, int index) const;
    int clipAt(const QPointF &) const;
    Gesture edgeAt(int index, double x) const;
    void updateViewport(bool fit = false);
    void refreshMedia();
    void retainSources();
    void updateInteractive();
    void updateGesture(Qt::KeyboardModifiers modifiers);
    int snappedSourceFrame(double raw, int minimum, int maximum, Qt::KeyboardModifiers);
    int boundaryAt(double x) const;
    QVector<int> reorderedIndices(int boundary) const;
    void updateTrimComparison();
    void finishGesture(bool canceled);
    void endSkim();
    void skimAt(const QPointF &);
    void seekAt(double x);
    void scrollAtEdge();
    QStringList droppedFiles(const QMimeData *) const;
    void updateDropPreview(double x,const QMimeData *);
    QString clipMediaStatus(int index) const;
    QString waveformStatus(const QString &path) const;
    void setHoveredMedia(int clip, const QString &audioPath = {});

    int hoveredClip_ = -1;
    QString hoveredAudioPath_;
    Project project_;
    Project preview_;
    QVector<double> sourceDurations_;
    double voiceDurationSeconds_ = 0, musicDurationSeconds_ = 0;
    QVariantList selection_;
    int position_ = 0;
    int skimFrame_ = -1;
    bool snapping_ = true;
    bool skimming_ = false;
    bool playing_ = false;
    bool autoFit_ = true;
    bool dropActive_ = false;
    bool dropValid_ = false;
    int dropFrame_ = 0;
    QHash<QString,int> dropDurations_;
    QFont font_;
    double pixelsPerSecond_ = 64;
    double waveformScale_ = 1;
    double contentOffset_ = 0;
    double maximumOffset_ = 0;
    Gesture gesture_ = Gesture::None;
    int gestureClip_ = -1;
    int gestureAudioLane_=1,gestureTransition_=-1,selectedTransition_=-1,presetCut_=-1;
    int selectedText_=-1,gestureTextIndex_=-1;
    int selectedCaption_=-1,titleLaneCount_=0,captionLaneCount_=0;
    QVariantList captionSelection_;
    QVector<int> titleLanes_,captionLanes_;
    bool gestureCaption_=false;
    int scrubStart_ = 0;
    int moveBoundary_ = -1;
    bool dragging_ = false;
    bool deferredSelection_ = false;
    QPointF pressPoint_;
    QPointF lastPoint_;
    double pressOffset_ = 0;
    Qt::KeyboardModifiers lastModifiers_ = Qt::NoModifier;
    QVector<int> group_;
    QVector<int> previewOrder_;
    int snapSource_ = -1;
    int snapTimeline_ = -1;
    QString snapLabel_;
    QString gestureText_;
    QVariantMap trimPreview_{{"active", false}};
    QTimer edgeTimer_;
    QPointer<MediaCache> mediaCache_;
    QVector<FilmTile> filmTiles_;
    QHash<QString, QVector<float>> waveforms_;
    QHash<QString, QString> waveformStates_;
};
