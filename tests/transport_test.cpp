#include "main_window.h"
#include "preview.h"
#include "preview_cache.h"
#include "timeline_widget.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMimeData>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtTest>

namespace {
bool dropClips(MainWindow &window, int count = 1) {
    QMimeData mime;
    QList<QUrl> urls;
    for (int i = 0; i < count; ++i)
        urls.append(QUrl::fromLocalFile(QDir::current().filePath("tests/fixtures/seek.mp4")));
    mime.setUrls(urls);
    QDragEnterEvent enter(QPointF(10, 10), Qt::CopyAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &enter);
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &drop);
    return drop.isAccepted();
}
}

class TransportTest : public QObject {
    Q_OBJECT
private slots:
    void arrowKeySeeksPreview();
    void controlsHaveContextualNames();
    void selectingClipSeeksItsStart();
    void editingKeepsPreviewPosition();
    void undoAndRedoRestoreImportedClips();
    void undoReorderRestoresSelectedClip();
    void timelineSelectionDoesNotSeekBeforeClickPosition();
    void trimSelectedToLength();
    void splitAtPlayhead();
    void repeatedSplitsAndBoundaryGuards();
    void trimmingSplitPiecePreservesStraightCut();
    void rotationIsPerClipAndUndoable();
    void rotationWrapsAfterFullTurn();
    void splitAndTrimPreserveRotation();
    void trimmedPreviewUsesPartialCopy();
};

void TransportTest::arrowKeySeeksPreview() {
    MainWindow window;
    window.show();
    auto *slider = window.findChild<QSlider *>();
    auto *preview = window.findChild<Preview *>();
    QVERIFY(slider);
    QVERIFY(preview);

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QDir::current().filePath("tests/fixtures/seek.mp4"))});
    QDragEnterEvent enter(QPointF(10, 10), Qt::CopyAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &enter);
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &drop);
    QVERIFY(drop.isAccepted());
    QVERIFY(slider->isEnabled());

    slider->setFocus();
    QTest::keyClick(slider, Qt::Key_Right);
    QCOMPARE(slider->value(), 1);
    QCOMPARE(preview->position(), 1);
    QCOMPARE(slider->pageStep(), 60);
    QTest::keyClick(slider, Qt::Key_PageUp);
    QCOMPARE(slider->value(), 61);
    QCOMPARE(preview->position(), 61);
}

void TransportTest::controlsHaveContextualNames() {
    MainWindow window;
    QStringList names;
    for (auto *button : window.findChildren<QPushButton *>()) {
        if (button->text() == "Choose" || button->text() == "Clear")
            names.append(button->accessibleName());
    }
    QCOMPARE(names.size(), 4);
    QVERIFY(names.contains("Choose narration audio file"));
    QVERIFY(names.contains("Clear narration audio file"));
    QVERIFY(names.contains("Choose background music file"));
    QVERIFY(names.contains("Clear background music file"));
    auto *slider = window.findChild<QSlider *>();
    QVERIFY(slider);
    QVERIFY(!slider->accessibleDescription().isEmpty());
}

void TransportTest::selectingClipSeeksItsStart() {
    MainWindow window;
    window.show();
    QVERIFY(dropClips(window, 2));
    auto *clips = window.findChild<QListWidget *>();
    auto *preview = window.findChild<Preview *>();
    QVERIFY(clips);
    QVERIFY(preview);
    QCOMPARE(clips->count(), 2);

    clips->setCurrentRow(0);
    QCOMPARE(preview->position(), 0);
    clips->setCurrentRow(1);
    QCOMPARE(preview->position(), 120);
    auto *start = window.findChild<QDoubleSpinBox *>("trimStart");
    auto *end = window.findChild<QDoubleSpinBox *>("trimEnd");
    QVERIFY(start);
    QVERIFY(end);
    QVERIFY(start->isEnabled());
    QVERIFY(end->isEnabled());
    QCOMPARE(start->value(), 0.0);
    QCOMPARE(end->value(), 2.0);
}

void TransportTest::editingKeepsPreviewPosition() {
    MainWindow window;
    window.show();
    QVERIFY(dropClips(window));
    auto *preview = window.findChild<Preview *>();
    QDoubleSpinBox *level = nullptr;
    for (auto *spin : window.findChildren<QDoubleSpinBox *>()) {
        if (spin->suffix() == " dB") level = spin;
    }
    QVERIFY(preview);
    QVERIFY(level);
    preview->seek(90);
    QCOMPARE(preview->position(), 90);

    level->setValue(-12);
    QVERIFY(QMetaObject::invokeMethod(level, "editingFinished", Qt::DirectConnection));
    QCOMPARE(preview->position(), 90);
    QCOMPARE(level->value(), -12.0);
}

void TransportTest::undoAndRedoRestoreImportedClips() {
    MainWindow window;
    window.show();
    QVERIFY(dropClips(window, 2));
    auto *clips = window.findChild<QListWidget *>();
    auto *undo = window.findChild<QAction *>("undoAction");
    auto *redo = window.findChild<QAction *>("redoAction");
    QVERIFY(clips);
    QVERIFY(undo);
    QVERIFY(redo);
    QCOMPARE(clips->count(), 2);
    QVERIFY(undo->isEnabled());
    QVERIFY(!redo->isEnabled());

    undo->trigger();
    QCOMPARE(clips->count(), 0);
    QVERIFY(!undo->isEnabled());
    QVERIFY(redo->isEnabled());
    redo->trigger();
    QCOMPARE(clips->count(), 2);
    QVERIFY(undo->isEnabled());
    QVERIFY(!redo->isEnabled());
}

void TransportTest::undoReorderRestoresSelectedClip() {
    MainWindow window;
    window.show();
    QVERIFY(dropClips(window, 3));
    auto *clips = window.findChild<QListWidget *>();
    auto *undo = window.findChild<QAction *>("undoAction");
    QPushButton *later = nullptr;
    for (auto *button : window.findChildren<QPushButton *>()) {
        if (button->text() == "Later") later = button;
    }
    QVERIFY(clips);
    QVERIFY(undo);
    QVERIFY(later);
    clips->setCurrentRow(0);
    QVERIFY(later->isEnabled());
    QTest::mouseClick(later, Qt::LeftButton);
    QCOMPARE(clips->currentRow(), 1);

    undo->trigger();
    QCOMPARE(clips->count(), 3);
    QCOMPARE(clips->currentRow(), 0);
}

void TransportTest::timelineSelectionDoesNotSeekBeforeClickPosition() {
    MainWindow window;
    window.show();
    QVERIFY(dropClips(window, 2));
    auto *clips = window.findChild<QListWidget *>();
    auto *preview = window.findChild<Preview *>();
    auto *timeline = window.findChild<TimelineWidget *>();
    QVERIFY(clips);
    QVERIFY(preview);
    QVERIFY(timeline);
    clips->setCurrentRow(0);
    QCOMPARE(preview->position(), 0);
    QSignalSpy positions(preview, &Preview::positionChanged);

    QVERIFY(QMetaObject::invokeMethod(timeline, "clipSelected", Qt::DirectConnection,
                                    Q_ARG(int, 1)));
    QCOMPARE(clips->currentRow(), 1);
    QCOMPARE(positions.count(), 0);
    QCOMPARE(preview->position(), 0);
    QVERIFY(QMetaObject::invokeMethod(timeline, "seekRequested", Qt::DirectConnection,
                                    Q_ARG(int, 180)));
    QCOMPARE(preview->position(), 180);
    QCOMPARE(positions.count(), 1);
    QCOMPARE(positions.at(0).at(0).toInt(), 180);
}

void TransportTest::trimSelectedToLength() {
    MainWindow window;
    window.show();
    QVERIFY(dropClips(window));
    auto *start = window.findChild<QDoubleSpinBox *>("trimStart");
    auto *end = window.findChild<QDoubleSpinBox *>("trimEnd");
    auto *length = window.findChild<QDoubleSpinBox *>("clipLength");
    auto *undo = window.findChild<QAction *>("undoAction");
    QVERIFY(start);
    QVERIFY(end);
    QVERIFY(length);
    QVERIFY(undo);
    QVERIFY(length->isEnabled());
    QCOMPARE(length->value(), 2.0);

    start->setValue(0.25);
    QVERIFY(QMetaObject::invokeMethod(start, "editingFinished", Qt::DirectConnection));
    QCOMPARE(length->value(), 1.75);
    length->setValue(0.5);
    QVERIFY(QMetaObject::invokeMethod(length, "editingFinished", Qt::DirectConnection));
    QCOMPARE(start->value(), 0.25);
    QCOMPARE(end->value(), 0.75);
    QCOMPARE(length->value(), 0.5);

    undo->trigger();
    QCOMPARE(start->value(), 0.25);
    QCOMPARE(end->value(), 2.0);
    QCOMPARE(length->value(), 1.75);
    length->setValue(9999.0);
    QVERIFY(QMetaObject::invokeMethod(length, "editingFinished", Qt::DirectConnection));
    QCOMPARE(length->value(), 1.75);
    QCOMPARE(end->value(), 2.0);
}

void TransportTest::splitAtPlayhead() {
    MainWindow window;
    window.show();
    auto *split = window.findChild<QPushButton *>("splitClipButton");
    auto *splitAction = window.findChild<QAction *>("splitClipAction");
    QVERIFY(split);
    QVERIFY(splitAction);
    QVERIFY(!split->isEnabled());
    QVERIFY(!splitAction->isEnabled());
    QVERIFY(dropClips(window));
    auto *clips = window.findChild<QListWidget *>();
    auto *preview = window.findChild<Preview *>();
    auto *start = window.findChild<QDoubleSpinBox *>("trimStart");
    auto *end = window.findChild<QDoubleSpinBox *>("trimEnd");
    auto *length = window.findChild<QDoubleSpinBox *>("clipLength");
    auto *slider = window.findChild<QSlider *>("videoPosition");
    auto *undo = window.findChild<QAction *>("undoAction");
    auto *redo = window.findChild<QAction *>("redoAction");
    QVERIFY(clips);
    QVERIFY(preview);
    QVERIFY(start);
    QVERIFY(end);
    QVERIFY(length);
    QVERIFY(slider);
    QVERIFY(undo);
    QVERIFY(redo);
    QVERIFY(!split->isEnabled());

    preview->seek(60);
    QVERIFY(split->isEnabled());
    QVERIFY(splitAction->isEnabled());
    QTest::mouseClick(split, Qt::LeftButton);
    QCOMPARE(clips->count(), 2);
    QCOMPARE(clips->currentRow(), 1);
    QCOMPARE(start->value(), 1.0);
    QCOMPARE(end->value(), 2.0);
    QCOMPARE(length->value(), 1.0);
    QCOMPARE(slider->maximum(), 119);
    QCOMPARE(preview->position(), 60);
    QVERIFY(!split->isEnabled());

    undo->trigger();
    QCOMPARE(clips->count(), 1);
    QCOMPARE(start->value(), 0.0);
    QCOMPARE(end->value(), 2.0);
    QCOMPARE(length->value(), 2.0);
    QCOMPARE(slider->maximum(), 119);
    redo->trigger();
    QCOMPARE(clips->count(), 2);
    QCOMPARE(clips->currentRow(), 1);
    QCOMPARE(start->value(), 1.0);
    QCOMPARE(end->value(), 2.0);
}

void TransportTest::repeatedSplitsAndBoundaryGuards() {
    MainWindow window;
    window.show();
    QVERIFY(dropClips(window));
    auto *clips = window.findChild<QListWidget *>();
    auto *preview = window.findChild<Preview *>();
    auto *split = window.findChild<QPushButton *>("splitClipButton");
    auto *splitAction = window.findChild<QAction *>("splitClipAction");
    auto *start = window.findChild<QDoubleSpinBox *>("trimStart");
    auto *end = window.findChild<QDoubleSpinBox *>("trimEnd");
    auto *slider = window.findChild<QSlider *>("videoPosition");
    QVERIFY(clips);
    QVERIFY(preview);
    QVERIFY(split);
    QVERIFY(splitAction);
    QVERIFY(start);
    QVERIFY(end);
    QVERIFY(slider);
    preview->seek(60);
    splitAction->trigger();
    QCOMPARE(clips->count(), 2);
    QVERIFY(!split->isEnabled());
    preview->seek(90);
    QVERIFY(split->isEnabled());
    splitAction->trigger();
    QCOMPARE(clips->count(), 3);
    QCOMPARE(clips->currentRow(), 2);
    QCOMPARE(slider->maximum(), 119);
    QCOMPARE(start->value(), 1.5);
    QCOMPARE(end->value(), 2.0);

    clips->setCurrentRow(0);
    QCOMPARE(start->value(), 0.0);
    QCOMPARE(end->value(), 1.0);
    preview->seek(60);
    QVERIFY(!split->isEnabled());
    QVERIFY(!splitAction->isEnabled());
    preview->seek(90);
    QVERIFY(!split->isEnabled());
    splitAction->trigger();
    QCOMPARE(clips->count(), 3);
    clips->setCurrentRow(1);
    QCOMPARE(start->value(), 1.0);
    QCOMPARE(end->value(), 1.5);
    clips->setCurrentRow(-1);
    QVERIFY(!split->isEnabled());
    QVERIFY(!splitAction->isEnabled());
}

void TransportTest::trimmingSplitPiecePreservesStraightCut() {
    MainWindow window;
    window.show();
    QVERIFY(dropClips(window, 3));
    auto *clips = window.findChild<QListWidget *>();
    auto *preview = window.findChild<Preview *>();
    auto *timeline = window.findChild<TimelineWidget *>();
    auto *split = window.findChild<QPushButton *>("splitClipButton");
    auto *slider = window.findChild<QSlider *>("videoPosition");
    auto *start = window.findChild<QDoubleSpinBox *>("trimStart");
    auto *end = window.findChild<QDoubleSpinBox *>("trimEnd");
    auto *undo = window.findChild<QAction *>("undoAction");
    QDoubleSpinBox *crossfade = nullptr;
    for (auto *spin : window.findChildren<QDoubleSpinBox *>()) {
        if (spin->toolTip().startsWith("A crossfade")) crossfade = spin;
    }
    QVERIFY(clips);
    QVERIFY(preview);
    QVERIFY(timeline);
    QVERIFY(split);
    QVERIFY(slider);
    QVERIFY(start);
    QVERIFY(end);
    QVERIFY(undo);
    QVERIFY(crossfade);
    crossfade->setValue(0.25);
    QVERIFY(QMetaObject::invokeMethod(crossfade, "editingFinished", Qt::DirectConnection));
    clips->setCurrentRow(1);
    preview->seek(165);
    QVERIFY(split->isEnabled());
    QTest::mouseClick(split, Qt::LeftButton);
    QCOMPARE(clips->count(), 4);
    QCOMPARE(clips->currentRow(), 2);
    QCOMPARE(slider->maximum(), 329);
    QCOMPARE(timeline->clipStartFrame(2), 165);

    QVERIFY(QMetaObject::invokeMethod(timeline, "trimRequested", Qt::DirectConnection,
                                    Q_ARG(int, 2), Q_ARG(double, 1.0), Q_ARG(double, 1.75)));
    QCOMPARE(clips->currentRow(), 2);
    QCOMPARE(start->value(), 1.0);
    QCOMPARE(end->value(), 1.75);
    QCOMPARE(slider->maximum(), 314);
    QCOMPARE(timeline->clipStartFrame(2), 165);
    undo->trigger();
    QCOMPARE(slider->maximum(), 329);
    QCOMPARE(timeline->clipStartFrame(2), 165);
    QCOMPARE(start->value(), 1.0);
    QCOMPARE(end->value(), 2.0);
}

void TransportTest::rotationIsPerClipAndUndoable() {
    MainWindow window;
    window.show();
    auto *left = window.findChild<QPushButton *>("rotateLeftButton");
    auto *right = window.findChild<QPushButton *>("rotateRightButton");
    auto *halfTurn = window.findChild<QPushButton *>("rotate180Button");
    auto *orientation = window.findChild<QLabel *>("clipRotation");
    auto *undo = window.findChild<QAction *>("undoAction");
    auto *redo = window.findChild<QAction *>("redoAction");
    QVERIFY(left);
    QVERIFY(right);
    QVERIFY(halfTurn);
    QVERIFY(orientation);
    QVERIFY(undo);
    QVERIFY(redo);
    QVERIFY(!left->isEnabled());
    QVERIFY(!right->isEnabled());
    QVERIFY(!halfTurn->isEnabled());
    QVERIFY(dropClips(window, 2));
    auto *clips = window.findChild<QListWidget *>();
    auto *preview = window.findChild<Preview *>();
    auto *slider = window.findChild<QSlider *>("videoPosition");
    QVERIFY(clips);
    QVERIFY(preview);
    QVERIFY(slider);
    clips->setCurrentRow(0);
    preview->seek(75);
    QCOMPARE(orientation->text(), QString("Original orientation"));

    QTest::mouseClick(right, Qt::LeftButton);
    QCOMPARE(orientation->text(), QString("90° clockwise"));
    QCOMPARE(preview->position(), 75);
    QCOMPARE(slider->maximum(), 239);
    QTest::mouseClick(left, Qt::LeftButton);
    QCOMPARE(orientation->text(), QString("Original orientation"));
    QTest::mouseClick(halfTurn, Qt::LeftButton);
    QCOMPARE(orientation->text(), QString("180° clockwise"));
    undo->trigger();
    QCOMPARE(orientation->text(), QString("Original orientation"));
    redo->trigger();
    QCOMPARE(orientation->text(), QString("180° clockwise"));
    QCOMPARE(preview->position(), 75);
    QCOMPARE(slider->maximum(), 239);
    clips->setCurrentRow(1);
    QCOMPARE(orientation->text(), QString("Original orientation"));
}

void TransportTest::rotationWrapsAfterFullTurn() {
    MainWindow window;
    window.show();
    QVERIFY(dropClips(window));
    auto *right = window.findChild<QPushButton *>("rotateRightButton");
    auto *left = window.findChild<QPushButton *>("rotateLeftButton");
    auto *orientation = window.findChild<QLabel *>("clipRotation");
    QVERIFY(right);
    QVERIFY(left);
    QVERIFY(orientation);
    for (const auto &expected : {"90° clockwise", "180° clockwise", "270° clockwise", "Original orientation"}) {
        QTest::mouseClick(right, Qt::LeftButton);
        QCOMPARE(orientation->text(), QString::fromUtf8(expected));
    }
    QTest::mouseClick(left, Qt::LeftButton);
    QCOMPARE(orientation->text(), QString("270° clockwise"));
}

void TransportTest::splitAndTrimPreserveRotation() {
    MainWindow window;
    window.show();
    QVERIFY(dropClips(window));
    auto *clips = window.findChild<QListWidget *>();
    auto *preview = window.findChild<Preview *>();
    auto *right = window.findChild<QPushButton *>("rotateRightButton");
    auto *split = window.findChild<QPushButton *>("splitClipButton");
    auto *orientation = window.findChild<QLabel *>("clipRotation");
    auto *start = window.findChild<QDoubleSpinBox *>("trimStart");
    auto *length = window.findChild<QDoubleSpinBox *>("clipLength");
    QVERIFY(clips);
    QVERIFY(preview);
    QVERIFY(right);
    QVERIFY(split);
    QVERIFY(orientation);
    QVERIFY(start);
    QVERIFY(length);
    QTest::mouseClick(right, Qt::LeftButton);
    preview->seek(60);
    QTest::mouseClick(split, Qt::LeftButton);
    QCOMPARE(clips->count(), 2);
    QCOMPARE(clips->currentRow(), 1);
    QCOMPARE(orientation->text(), QString("90° clockwise"));
    length->setValue(0.5);
    QVERIFY(QMetaObject::invokeMethod(length, "editingFinished", Qt::DirectConnection));
    QCOMPARE(length->value(), 0.5);
    QCOMPARE(orientation->text(), QString("90° clockwise"));
    clips->setCurrentRow(0);
    QCOMPARE(orientation->text(), QString("90° clockwise"));
    start->setValue(0.25);
    QVERIFY(QMetaObject::invokeMethod(start, "editingFinished", Qt::DirectConnection));
    QCOMPARE(start->value(), 0.25);
    QCOMPARE(orientation->text(), QString("90° clockwise"));
    clips->setCurrentRow(1);
    QCOMPARE(orientation->text(), QString("90° clockwise"));
}

void TransportTest::trimmedPreviewUsesPartialCopy() {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QTemporaryDir directory;
    const QString source = directory.filePath("large.mp4");
    QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) ffmpeg = QStandardPaths::findExecutable("ffmpeg", {"/opt/homebrew/bin", "/usr/local/bin"});
    QVERIFY(!ffmpeg.isEmpty());
    QProcess encode;
    encode.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "color=c=red:s=2048x1152:r=60:d=4",
        "-c:v", "libx264", "-preset", "ultrafast", "-an", "-y", source});
    QVERIFY(encode.waitForFinished(15000));
    QCOMPARE(encode.exitCode(), 0);
    Project project;
    project.clips = {{source, 1.5, 2.0}};
    const QString saved = directory.filePath("trimmed.isaac.json");
    QString dialogPath = saved;
    bool removeSourceBeforeExport = false, exportErrorSeen = false;
    QVERIFY(saveProject(project, saved));
    MainWindow window;
    auto *cache = window.findChild<PreviewCache *>();
    auto *preview = window.findChild<Preview *>();
    QVERIFY(cache);
    QVERIFY(preview);
    cache->setPaused(true);
    QSignalSpy ready(cache, &PreviewCache::ready);
    QSignalSpy failed(cache, &PreviewCache::failed);
    QSignalSpy frames(preview, &Preview::frameReady);
    QAction *open = nullptr;
    for (auto *action : window.findChildren<QAction *>())
        if (action->shortcut() == QKeySequence::Open) open = action;
    QVERIFY(open);
    QTimer dialogTimer;
    QTimer dialogDeadline;
    dialogDeadline.setSingleShot(true);
    connect(&dialogDeadline, &QTimer::timeout, &window, [] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
    });
    connect(&dialogTimer, &QTimer::timeout, &window, [&] {
        if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
            if (!dialog->property("testPathSelected").toBool()) {
                dialog->setDirectory(QFileInfo(dialogPath).absolutePath());
                dialog->selectFile(QFileInfo(dialogPath).fileName());
                if (auto *name = dialog->findChild<QLineEdit *>("fileNameEdit"))
                    name->setText(QFileInfo(dialogPath).fileName());
                dialog->setProperty("testPathSelected", true);
            } else {
                if (removeSourceBeforeExport) {
                    QFile::remove(source);
                    removeSourceBeforeExport = false;
                }
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        } else if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            exportErrorSeen = true;
            message->accept();
        }
    });
    dialogTimer.start(10);
    dialogDeadline.start(10000);
    open->trigger();
    dialogTimer.stop();
    dialogDeadline.stop();
    auto *slider = window.findChild<QSlider *>("videoPosition");
    QVERIFY(slider->isEnabled());
    QCOMPARE(slider->maximum(), 29);
    preview->seek(12);
    cache->setPaused(false);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1 || !failed.isEmpty(), 10000);
    QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().last().toString()));
    QTRY_VERIFY_WITH_TIMEOUT(!cache->readyCopy(source, 90, 120).path.isEmpty(), 10000);
    const auto copy = cache->readyCopy(source, 90, 120);
    QVERIFY(copy.startFrame > 0);
    QVERIFY(cache->readyPath(source).isEmpty());
    QCOMPARE(preview->position(), 12);
    QCOMPARE(slider->maximum(), 29);
    const QImage image = frames.last().first().value<QImage>();
    QVERIFY(!image.isNull());
    QVERIFY(image.pixelColor(image.width() / 2, image.height() / 2).red() > 180);
    QCOMPARE(loadProject(saved)->clips.first().path, source);
    cache->discard(source);
    cache->setRequests({{source, 240, 0, 240}});
    auto *process = cache->findChild<QProcess *>();
    QVERIFY(process);
    QVERIFY(process->state() != QProcess::NotRunning);
    QPushButton *exportButton = nullptr;
    for (auto *button : window.findChildren<QPushButton *>())
        if (button->text() == "Export…") exportButton = button;
    QVERIFY(exportButton);
    dialogPath = directory.filePath("output.mp4");
    dialogTimer.start(10);
    dialogDeadline.start(10000);
    QTest::mouseClick(exportButton, Qt::LeftButton);
    dialogTimer.stop();
    dialogDeadline.stop();
    QVERIFY(!window.centralWidget()->isEnabled());
    QCOMPARE(process->state(), QProcess::NotRunning);
    QTRY_VERIFY_WITH_TIMEOUT(window.centralWidget()->isEnabled(), 15000);
    QVERIFY(QFileInfo(dialogPath).size() > 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 10000);
    QVERIFY(!cache->readyPath(source).isEmpty());
    cache->discard(source);
    const QString background = directory.filePath("background.mp4");
    QVERIFY(QFile::copy(source, background));
    cache->setRequests({{background, 240, 0, 240}});
    dialogPath = directory.filePath("failed.mp4");
    removeSourceBeforeExport = true;
    dialogTimer.start(10);
    dialogDeadline.start(10000);
    QTest::mouseClick(exportButton, Qt::LeftButton);
    dialogDeadline.stop();
    QVERIFY(!window.centralWidget()->isEnabled());
    QCOMPARE(process->state(), QProcess::NotRunning);
    QTRY_VERIFY_WITH_TIMEOUT(window.centralWidget()->isEnabled(), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(exportErrorSeen, 10000);
    dialogTimer.stop();
    QVERIFY(!QFile::exists(dialogPath));
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 10000);
    QVERIFY(!cache->readyPath(background).isEmpty());
    cache->discard(background);
}

QTEST_MAIN(TransportTest)
#include "transport_test.moc"
