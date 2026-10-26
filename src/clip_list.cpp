#include "clip_list.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QStyle>
#include <QUrl>

QStringList localFilePaths(const QMimeData *mime) {
    if (!mime || !mime->hasUrls()) return {};
    QStringList files;
    for (const auto &url : mime->urls()) {
        if (!url.isLocalFile()) return {};
        files.append(url.toLocalFile());
    }
    return files;
}

ClipListWidget::ClipListWidget(QWidget *parent) : QListWidget(parent) {
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
    setDragDropOverwriteMode(false);
}

void ClipListWidget::setDropActive(bool active) {
    if (property("dropActive").toBool() == active) return;
    setProperty("dropActive", active);
    style()->unpolish(this);
    style()->polish(this);
    viewport()->update();
}

QVector<int> ClipListWidget::itemOrder() const {
    QVector<int> order;
    order.reserve(count());
    for (int i = 0; i < count(); ++i) order.append(item(i)->data(Qt::UserRole).toInt());
    return order;
}

void ClipListWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (event->source() != this && !localFilePaths(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        setDropActive(true);
        return;
    }
    QListWidget::dragEnterEvent(event);
}

void ClipListWidget::dragMoveEvent(QDragMoveEvent *event) {
    if (event->source() != this && !localFilePaths(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    QListWidget::dragMoveEvent(event);
}

void ClipListWidget::dragLeaveEvent(QDragLeaveEvent *event) {
    setDropActive(false);
    QListWidget::dragLeaveEvent(event);
}

void ClipListWidget::dropEvent(QDropEvent *event) {
    setDropActive(false);
    const auto files = localFilePaths(event->mimeData());
    if (event->source() != this && !files.isEmpty()) {
        if (onFilesDropped) onFilesDropped(files);
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    const auto before = itemOrder();
    QListWidget::dropEvent(event);
    const auto after = itemOrder();
    if (event->isAccepted() && after != before && onReordered) onReordered(after);
}

void ClipListWidget::paintEvent(QPaintEvent *event) {
    QListWidget::paintEvent(event);
    if (count() != 0) return;
    QPainter painter(viewport());
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(viewport()->rect().adjusted(24, 24, -24, -24),
                     Qt::AlignCenter | Qt::TextWordWrap,
                     "Drop video files here\nor click Import");
}
