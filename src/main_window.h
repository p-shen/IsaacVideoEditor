#pragma once

#include "project.h"

#include <QImage>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <mlt++/Mlt.h>
#include <memory>

class QCloseEvent;
class QAction;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QResizeEvent;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class ClipListWidget;
class QProgressBar;
class QPushButton;
class QSlider;
class QThread;
class QSplitter;
class QTabWidget;
class QUndoStack;
class Preview;
class PreviewCache;
class TimelineWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void makeUi();
    void makeMenus();
    void updateClipList(int selected = -1);
    void showSelectedClip();
    void updateAudioFields();
    void updateTime();
    void updateTitle();
    void showFrame();
    void rebuildTimeline(int position = 0);
    void prioritizePreview(int position);
    void changed(const QString &label = "Edit project");
    void applySnapshot(const Project &project, int selected, int position);
    void updateWorkspaceLayout();
    void updateVisualTimeline();
    double sourceDuration(const QString &path);
    bool maybeSave();
    bool save(bool choosePath);
    void newProject();
    void openProject();
    void addClips();
    void addFiles(const QStringList &paths);
    void removeClip();
    void moveClip(int direction);
    void commitTrim();
    void commitLength();
    void rotateClip(int degrees);
    void splitClip();
    int splitOffset() const;
    void updateSplitAvailability();
    void chooseVoice();
    void chooseMusic();
    void addMusic(const QString &path);
    void exportVideo();

    Project project_;
    Project committedProject_;
    int selectionBeforeEdit_ = -1;
    QUndoStack *undo_ = nullptr;
    QString projectPath_;
    bool dirty_ = false;
    Mlt::Profile profile_;
    std::unique_ptr<Mlt::Tractor> timeline_;
    std::unique_ptr<Preview> preview_;
    QPointer<QThread> exportThread_;
    QImage lastFrame_;
    PreviewCache *previewCache_ = nullptr;
    QVector<QString> previewPathsUsed_;
    int previewPriorityStart_ = -1, previewPriorityEnd_ = -1;
    bool previewRefreshPending_ = false;

    ClipListWidget *clips_ = nullptr;
    QDoubleSpinBox *in_ = nullptr;
    QDoubleSpinBox *out_ = nullptr;
    QDoubleSpinBox *length_ = nullptr;
    QDoubleSpinBox *crossfade_ = nullptr;
    QLineEdit *voicePath_ = nullptr;
    QDoubleSpinBox *voiceStart_ = nullptr;
    QLineEdit *musicPath_ = nullptr;
    QDoubleSpinBox *musicDb_ = nullptr;
    QLabel *image_ = nullptr;
    QLabel *previewState_ = nullptr;
    QLabel *format_ = nullptr;
    QLabel *projectTitle_ = nullptr;
    QLabel *saveState_ = nullptr;
    QLabel *clipCount_ = nullptr;
    QLabel *clipName_ = nullptr;
    QLabel *clipMeta_ = nullptr;
    QLabel *rotation_ = nullptr;
    QLabel *timelineSummary_ = nullptr;
    QLabel *time_ = nullptr;
    QSlider *seek_ = nullptr;
    QPushButton *play_ = nullptr;
    QPushButton *split_ = nullptr;
    QAction *splitAction_ = nullptr;
    QPushButton *remove_ = nullptr;
    QPushButton *up_ = nullptr;
    QPushButton *down_ = nullptr;
    QPushButton *rotateLeft_ = nullptr;
    QPushButton *rotateRight_ = nullptr;
    QPushButton *rotate180_ = nullptr;
    QPushButton *voiceClear_ = nullptr;
    QPushButton *musicClear_ = nullptr;
    QPushButton *export_ = nullptr;
    QProgressBar *progress_ = nullptr;
    TimelineWidget *visualTimeline_ = nullptr;
    QSplitter *workspaceSplitter_ = nullptr;
    QTabWidget *browserTabs_ = nullptr;
    QTabWidget *inspectorTabs_ = nullptr;
    QWidget *inspectorPanel_ = nullptr;
};
