#pragma once
#include <QQuickPaintedItem>
#include <QImage>
#include <QPointer>
#include "editor_controller.h"
#include "media_cache.h"
class VideoPreview : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(EditorController* controller READ controller WRITE setController NOTIFY controllerChanged)
public:
    explicit VideoPreview(QQuickItem *parent=nullptr);
    EditorController *controller() const { return controller_; }
    void setController(EditorController *controller);
    void paint(QPainter *painter) override;
    Q_INVOKABLE QRectF textRect(int index) const;
    Q_INVOKABLE QRectF captionRect(int index) const;
    Q_INVOKABLE void cancelTextDrag() { mouseUngrabEvent(); }
signals:
    void controllerChanged();
    void textSelected();
    void captionSelected();
protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseUngrabEvent() override;
    void keyPressEvent(QKeyEvent *) override;
private:
    QRectF imageRect() const;
    QRectF timedRect(int index,bool caption) const;
    QPolygonF transformPolygon() const;
    QPointF rotationHandle() const;
    int transformDrag_=0;
    QPointF transformPress_,transformCenter_;
    QVariantMap transformOrigin_;
    QPointer<EditorController> controller_;
    bool textDragging_=false;
    QPointF textPress_,textOrigin_;
};
class CachedFrame : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(MediaCache* cache READ cache WRITE setCache NOTIFY changed)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(double seconds READ seconds WRITE setSeconds NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool priority READ priority WRITE setPriority NOTIFY changed)
    Q_PROPERTY(int sourceRotation READ sourceRotation WRITE setSourceRotation NOTIFY changed)
public:
    explicit CachedFrame(QQuickItem *parent=nullptr);
    MediaCache *cache() const { return cache_; }
    QString source() const { return source_; }
    double seconds() const { return seconds_; }
    bool ready() const { return !image_.isNull(); }
    bool priority() const { return priority_; }
    void setPriority(bool priority);
    int sourceRotation() const { return rotation_; }
    void setCache(MediaCache *cache);
    void setSource(const QString &source);
    void setSeconds(double seconds);
    void setSourceRotation(int rotation);
    void paint(QPainter *painter) override;
signals:
    void changed();
    void readyChanged();
private:
    void refresh();
    QPointer<MediaCache> cache_;
    QString source_;
    double seconds_=0;
    int rotation_=0;
    bool priority_=false;
    QImage image_;
};
