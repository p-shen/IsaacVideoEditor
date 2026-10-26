#include "clip_list.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QtTest>

class ExposedClipList : public ClipListWidget {
public:
    using ClipListWidget::dragEnterEvent;
    using ClipListWidget::dropEvent;
};

class ClipListTest : public QObject {
    Q_OBJECT
private slots:
    void acceptsLocalVideosAsCopies();
};

void ClipListTest::acceptsLocalVideosAsCopies() {
    ExposedClipList list;
    QStringList paths;
    list.onFilesDropped = [&](const QStringList &dropped) { paths = dropped; };
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile("/tmp/first.mp4"),
                  QUrl::fromLocalFile("/tmp/second.mov")});
    QDragEnterEvent enter(QPointF(8, 8), Qt::CopyAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    list.dragEnterEvent(&enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(8, 8), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier);
    list.dropEvent(&drop);
    QVERIFY(drop.isAccepted());
    QCOMPARE(paths, QStringList({"/tmp/first.mp4", "/tmp/second.mov"}));
    QCOMPARE(drop.dropAction(), Qt::CopyAction);
}

QTEST_MAIN(ClipListTest)
#include "clip_list_test.moc"
