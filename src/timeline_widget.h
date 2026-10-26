#pragma once

#include "project.h"

#include <QAbstractScrollArea>
#include <QPoint>
#include <QStringList>

class QMimeData;

class TimelineWidget final : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget *parent = nullptr);
    void setProject(const Project &, const QVector<double> &sourceDurations = {},
                    double voiceDurationSeconds = 0);
    void setPosition(int frame);
    void setSelectedClip(int index);
    void zoomIn();
    void zoomOut();
    void fitToWidth();
    int clipStartFrame(int index) const;

signals:
    void seekRequested(int frame);
    void clipSelected(int index);
    void clipMoved(int from, int to);
    void trimRequested(int index, double inSeconds, double outSeconds);
    void audioSelected(int lane);
    void filesDropped(const QStringList &paths);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;
    bool viewportEvent(QEvent *) override;

private:
    enum class Gesture { None, Seek, Clip, TrimStart, TrimEnd };
    int labelWidth() const;
    int rulerHeight() const;
    int videoHeight() const;
    int audioHeight() const;
    double xForFrame(int frame) const;
    int frameAtX(double x) const;
    QRectF clipRect(int index) const;
    int clipAt(const QPointF &point) const;
    int reorderTarget(double x) const;
    void updateRange();
    void requestSeek(double x);
    void resetGesture();
    QStringList droppedFiles(const QMimeData *) const;

    Project project_;
    QVector<double> sourceDurations_;
    double voiceDurationSeconds_ = 0;
    double pixelsPerSecond_ = 64;
    bool autoFit_ = true;
    bool dropActive_ = false;
    int position_ = 0;
    int selected_ = -1;
    Gesture gesture_ = Gesture::None;
    QPoint pressPoint_;
    QPoint lastPoint_;
    int gestureClip_ = -1;
    int moveTarget_ = -1;
    bool dragging_ = false;
    Clip trimmed_;
};
