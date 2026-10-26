#pragma once

#include <QListWidget>
#include <QStringList>
#include <QVector>
#include <functional>

class QMimeData;
QStringList localFilePaths(const QMimeData *mime);

class ClipListWidget : public QListWidget {
public:
    explicit ClipListWidget(QWidget *parent = nullptr);

    std::function<void(const QStringList &)> onFilesDropped;
    std::function<void(const QVector<int> &)> onReordered;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void setDropActive(bool active);
    QVector<int> itemOrder() const;
};
