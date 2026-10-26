#include "main_window.h"

#include "clip_list.h"
#include "mlt_engine.h"
#include "preview.h"
#include "preview_cache.h"
#include "timeline_widget.h"

#include <QAction>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTabBar>
#include <QThread>
#include <QUndoCommand>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QPixmap>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <functional>

namespace {

QString moviesDirectory()
{
    return QDir::home().filePath("Movies");
}

QString clockText(int frame)
{
    const int seconds = std::max(0, frame) / Project::fps;
    return QString("%1:%2:%3").arg(seconds / 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0')).arg(std::max(0, frame) % Project::fps, 2, 10, QChar('0'));
}

class ProjectEdit final : public QUndoCommand {
public:
    ProjectEdit(const Project &before, const Project &after, int beforeSelection, int afterSelection, int position,
                const QString &label, std::function<void(const Project &, int, int)> apply)
        : QUndoCommand(label), before_(before), after_(after), beforeSelection_(beforeSelection), afterSelection_(afterSelection),
          position_(position), apply_(std::move(apply)) {}
    void undo() override { apply_(before_, beforeSelection_, position_); }
    void redo() override { apply_(after_, afterSelection_, position_); }
private:
    Project before_, after_;
    int beforeSelection_, afterSelection_, position_;
    std::function<void(const Project &, int, int)> apply_;
};

QString videoFilter()
{
    return "Video files (*.mp4 *.mov *.m4v *.mkv *.avi);;All files (*)";
}

QString audioFilter()
{
    return "Audio files (*.wav *.m4a *.mp3 *.aac *.flac *.aiff);;All files (*)";
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    Media::initializeMlt();
    Media::configureOutputProfile(profile_, 960, 540);
    undo_ = new QUndoStack(this);
    undo_->setUndoLimit(100);
    preview_ = std::make_unique<Preview>(profile_, this);
    makeUi();
    makeMenus();

    previewCache_ = new PreviewCache({}, this);
    connect(previewCache_, &PreviewCache::statusChanged, this, [this](const QString &message) {
        if (project_.clips.isEmpty()) return;
        previewState_->setText(message);
        previewState_->setToolTip(message);
        statusBar()->showMessage(message);
    });
    connect(previewCache_, &PreviewCache::failed, this, [this](const QString &, const QString &message) {
        previewState_->setText("Using original media");
        previewState_->setToolTip(message);
        statusBar()->showMessage("Preview copy unavailable; using original media. " + message, 15000);
    });
    connect(previewCache_, &PreviewCache::ready, this, [this](const QString &, const QString &path) {
        bool needsRefresh = false;
        for (int row = 0; row < project_.clips.size(); ++row) {
            const auto &clip = project_.clips[row];
            const auto copy = previewCache_->readyCopy(clip.path, qRound(clip.inSeconds * Project::fps),
                                                       qRound(clip.outSeconds * Project::fps));
            if (copy.path == path && previewPathsUsed_.value(row) != path) needsRefresh = true;
        }
        if (!needsRefresh) return;
        if (exportThread_) {
            previewRefreshPending_ = true;
            return;
        }
        const bool playing = preview_->isPlaying();
        const int position = preview_->position();
        rebuildTimeline(position);
        if (playing && play_->isEnabled()) preview_->play();
    });

    connect(preview_.get(), &Preview::frameReady, this, [this](const QImage &frame) {
        lastFrame_ = frame;
        showFrame();
    });
    connect(preview_.get(), &Preview::positionChanged, this, [this](int frame) {
        if (!seek_->isSliderDown()) {
            QSignalBlocker blocked(seek_);
            seek_->setValue(frame);
        }
        visualTimeline_->setPosition(frame);
        if (seek_->isEnabled()) prioritizePreview(frame);
        updateTime();
    });
    connect(preview_.get(), &Preview::playbackChanged, this, [this](bool playing) {
        play_->setText(playing ? "Pause" : "Play");
        play_->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    });
    connect(preview_.get(), &Preview::errorOccurred, this, [this](const QString &error) {
        statusBar()->showMessage(error);
    });

    updateAudioFields();
    updateClipList();
    rebuildTimeline();
    updateTitle();
    connect(undo_, &QUndoStack::cleanChanged, this, [this](bool clean) {
        dirty_ = !clean;
        updateTitle();
    });
    resize(1320, 860);
    setMinimumSize(760, 560);
    setAcceptDrops(true);
}

MainWindow::~MainWindow()
{
    disconnect(previewCache_, nullptr, this, nullptr);
    delete previewCache_;
    if (exportThread_) exportThread_->wait();
    if (preview_) preview_->setProducer(nullptr);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    const bool wide = event->size().width() >= 960;
    if (auto *brand = findChild<QLabel *>("brandTitle")) brand->setVisible(wide);
    if (format_) format_->setVisible(wide);
    if (timelineSummary_) timelineSummary_->setVisible(wide);
    updateWorkspaceLayout();
    QMainWindow::resizeEvent(event);
}

void MainWindow::makeUi()
{
    auto *shell = new QWidget(this);
    auto *shellLayout = new QVBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);

    auto *header = new QWidget(shell);
    header->setObjectName("appHeader");
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 10, 16, 10);
    headerLayout->setSpacing(12);
    auto *mark = new QLabel(header);
    mark->setPixmap(QPixmap(":/isaac-logo.png").scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    mark->setFixedSize(30, 30);
    headerLayout->addWidget(mark);
    auto *brand = new QLabel("IsaacVideoEditor", header);
    brand->setObjectName("brandTitle");
    headerLayout->addWidget(brand);
    auto *projectInfo = new QVBoxLayout;
    projectInfo->setSpacing(1);
    projectTitle_ = new QLabel("Untitled", header);
    projectTitle_->setObjectName("projectTitle");
    projectTitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    saveState_ = new QLabel("New project", header);
    saveState_->setObjectName("saveState");
    projectInfo->addWidget(projectTitle_);
    projectInfo->addWidget(saveState_);
    headerLayout->addLayout(projectInfo, 1);
    format_ = new QLabel(QString("Auto resolution · %1 fps").arg(Project::fps), header);
    format_->setObjectName("format");
    headerLayout->addWidget(format_);
    auto *import = new QPushButton("Import", header);
    import->setObjectName("secondaryButton");
    import->setToolTip("Add video clips to the edit (Ctrl+I / Command+I)");
    import->setShortcut(QKeySequence("Ctrl+I"));
    headerLayout->addWidget(import);
    export_ = new QPushButton("Export…", header);
    export_->setObjectName("primaryButton");
    export_->setEnabled(false);
    headerLayout->addWidget(export_);
    connect(import, &QPushButton::clicked, this, &MainWindow::addClips);
    connect(export_, &QPushButton::clicked, this, &MainWindow::exportVideo);
    shellLayout->addWidget(header);

    auto *workspaceScroll = new QScrollArea(shell);
    workspaceScroll->setFrameShape(QFrame::NoFrame);
    workspaceScroll->setWidgetResizable(true);
    workspaceScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *vertical = new QSplitter(Qt::Vertical, workspaceScroll);
    // Keep panels operable with large text; short windows scroll the workspace.
    vertical->setMinimumHeight(std::max(520, fontMetrics().height() * 18 + 120));
    vertical->setChildrenCollapsible(false);
    vertical->setHandleWidth(5);
    workspaceSplitter_ = new QSplitter(Qt::Horizontal, vertical);
    workspaceSplitter_->setObjectName("workspaceSplitter");
    workspaceSplitter_->setChildrenCollapsible(false);
    workspaceSplitter_->setHandleWidth(5);
    workspaceSplitter_->setMinimumHeight(std::max(320, fontMetrics().height() * 10 + 80));
    browserTabs_ = new QTabWidget(workspaceSplitter_);
    browserTabs_->setObjectName("mediaPanel");
    browserTabs_->setTabBarAutoHide(true);
    browserTabs_->setMinimumWidth(210);

    auto *media = new QWidget(browserTabs_);
    media->setObjectName("mediaPanel");
    auto *mediaLayout = new QVBoxLayout(media);
    mediaLayout->setContentsMargins(0, 0, 0, 0);
    mediaLayout->setSpacing(0);
    auto *mediaHeading = new QWidget(media);
    mediaHeading->setObjectName("panelHeader");
    auto *mediaHeadingLayout = new QHBoxLayout(mediaHeading);
    mediaHeadingLayout->setContentsMargins(14, 10, 14, 10);
    auto *clipsTitle = new QLabel("Clips", mediaHeading);
    clipsTitle->setObjectName("panelTitle");
    clipCount_ = new QLabel("0 clips", mediaHeading);
    clipCount_->setObjectName("panelHint");
    mediaHeadingLayout->addWidget(clipsTitle);
    mediaHeadingLayout->addStretch();
    mediaHeadingLayout->addWidget(clipCount_);
    mediaLayout->addWidget(mediaHeading);
    clips_ = new ClipListWidget(media);
    clips_->setObjectName("clipBrowser");
    clips_->setSelectionMode(QAbstractItemView::SingleSelection);
    clips_->setAccessibleName("Video clips in playback order");
    clips_->setAccessibleDescription("Select a clip to preview and edit it. Drag up or down, or use Move earlier and Move later.");
    clips_->setTextElideMode(Qt::ElideMiddle);
    clips_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    clips_->onFilesDropped = [this](const QStringList &paths) { addFiles(paths); };
    clips_->onReordered = [this](const QVector<int> &order) {
        const int selectedId = clips_->currentItem() ? clips_->currentItem()->data(Qt::UserRole).toInt() : -1;
        if (!project_.reorderClips(order)) return;
        updateClipList(order.indexOf(selectedId));
        changed("Reorder clips");
    };
    mediaLayout->addWidget(clips_, 1);
    auto *footer = new QWidget(media);
    footer->setObjectName("panelFooter");
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(10, 10, 10, 10);
    footerLayout->setSpacing(6);
    auto *clipButtons = new QGridLayout;
    clipButtons->setSpacing(6);
    up_ = new QPushButton("Earlier", footer);
    down_ = new QPushButton("Later", footer);
    up_->setAccessibleName("Move selected clip earlier");
    down_->setAccessibleName("Move selected clip later");
    up_->setToolTip("Move selected clip earlier in the edit");
    down_->setToolTip("Move selected clip later in the edit");
    remove_ = new QPushButton("Remove clip", footer);
    clipButtons->addWidget(up_, 0, 0);
    clipButtons->addWidget(down_, 0, 1);
    clipButtons->addWidget(remove_, 1, 0, 1, 2);
    footerLayout->addLayout(clipButtons);
    auto *mediaHint = new QLabel("Drop files to add · drag to reorder", footer);
    mediaHint->setObjectName("panelHint");
    mediaHint->setWordWrap(true);
    footerLayout->addWidget(mediaHint);
    mediaLayout->addWidget(footer);
    footer->setMinimumHeight(footerLayout->minimumSize().height());
    browserTabs_->addTab(media, "Clips");
    connect(remove_, &QPushButton::clicked, this, &MainWindow::removeClip);
    connect(up_, &QPushButton::clicked, this, [this] { moveClip(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { moveClip(1); });
    connect(clips_, &QListWidget::currentRowChanged, this, [this](int row) {
        selectionBeforeEdit_ = row;
        showSelectedClip();
        if (row >= 0 && seek_->isEnabled()) preview_->seek(visualTimeline_->clipStartFrame(row));
    });

    auto *viewer = new QWidget(workspaceSplitter_);
    viewer->setObjectName("viewerPanel");
    auto *viewerLayout = new QVBoxLayout(viewer);
    viewerLayout->setContentsMargins(0, 0, 0, 0);
    viewerLayout->setSpacing(0);
    auto *viewerHeading = new QWidget(viewer);
    viewerHeading->setObjectName("panelHeader");
    auto *viewerHeadingLayout = new QHBoxLayout(viewerHeading);
    viewerHeadingLayout->setContentsMargins(14, 10, 14, 10);
    auto *viewerTitle = new QLabel("Viewer", viewerHeading);
    viewerTitle->setObjectName("panelTitle");
    viewerHeadingLayout->addWidget(viewerTitle);
    viewerHeadingLayout->addStretch();
    previewState_ = new QLabel("No media", viewerHeading);
    previewState_->setObjectName("panelHint");
    previewState_->setAccessibleName("Preview media status");
    previewState_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    viewerHeadingLayout->addWidget(previewState_, 1);
    viewerLayout->addWidget(viewerHeading);
    image_ = new QLabel("Your story starts here\n\nImport or drop video clips to begin", viewer);
    image_->setObjectName("preview");
    image_->setAlignment(Qt::AlignCenter);
    image_->setWordWrap(true);
    image_->setMinimumSize(220, 100);
    image_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    image_->setAccessibleName("Video preview");
    image_->installEventFilter(this);
    viewerLayout->addWidget(image_, 1);
    auto *transport = new QWidget(viewer);
    transport->setObjectName("viewerToolbar");
    auto *transportLayout = new QGridLayout(transport);
    transportLayout->setContentsMargins(12, 8, 12, 8);
    transportLayout->setSpacing(6);
    play_ = new QPushButton("Play", transport);
    play_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    play_->setEnabled(false);
    play_->setShortcut(QKeySequence(Qt::Key_Space));
    play_->setToolTip("Play or pause (Space)");
    play_->setAccessibleDescription("Press Space to play or pause the video.");
    time_ = new QLabel("00:00:00 / 00:00:00", transport);
    time_->setToolTip("Minutes : seconds : frames at 60 fps");
    time_->setAccessibleName("Current time and total duration in minutes, seconds, and frames");
    seek_ = new QSlider(Qt::Horizontal, transport);
    seek_->setObjectName("videoPosition");
    seek_->setEnabled(false);
    seek_->setAccessibleName("Video position");
    seek_->setAccessibleDescription("Use arrow keys to move one frame or Page Up and Page Down to move one second.");
    seek_->setSingleStep(1);
    seek_->setPageStep(Project::fps);
    transportLayout->addWidget(play_, 0, 0);
    transportLayout->addWidget(time_, 0, 1, Qt::AlignRight);
    transportLayout->addWidget(seek_, 1, 0, 1, 2);
    transportLayout->setColumnStretch(1, 1);
    viewerLayout->addWidget(transport);
    connect(play_, &QPushButton::clicked, this, [this] {
        if (preview_->isPlaying()) preview_->pause(); else preview_->play();
    });
    connect(seek_, &QSlider::valueChanged, this, [this](int frame) {
        visualTimeline_->setPosition(frame);
        if (!seek_->isSliderDown() && seek_->isEnabled() && frame != preview_->position()) preview_->seek(frame);
        updateTime();
    });
    connect(seek_, &QSlider::sliderReleased, this, [this] {
        preview_->seek(seek_->value());
        updateTime();
    });

    inspectorPanel_ = new QWidget(workspaceSplitter_);
    inspectorPanel_->setObjectName("inspectorPanel");
    inspectorPanel_->setMinimumWidth(260);
    auto *inspectorLayout = new QVBoxLayout(inspectorPanel_);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(0);
    inspectorTabs_ = new QTabWidget(inspectorPanel_);
    inspectorTabs_->setAccessibleName("Inspector sections");
    inspectorLayout->addWidget(inspectorTabs_);
    const auto inspectorPage = [this](const QString &title) {
        auto *scroll = new QScrollArea(inspectorTabs_);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *page = new QWidget(scroll);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(12);
        layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
        scroll->setWidget(page);
        inspectorTabs_->addTab(scroll, title);
        return layout;
    };
    auto *clipLayout = inspectorPage("Clip");
    clipName_ = new QLabel("No clip selected");
    clipName_->setObjectName("selectedClipName");
    clipName_->setWordWrap(true);
    clipName_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    clipMeta_ = new QLabel("Select a clip in the browser or timeline");
    clipMeta_->setObjectName("selectedClipMeta");
    clipMeta_->setWordWrap(true);
    clipLayout->addWidget(clipName_);
    clipLayout->addWidget(clipMeta_);
    auto *editBox = new QGroupBox("Trim source");
    auto *editForm = new QFormLayout(editBox);
    editForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    in_ = new QDoubleSpinBox(editBox);
    out_ = new QDoubleSpinBox(editBox);
    length_ = new QDoubleSpinBox(editBox);
    in_->setObjectName("trimStart");
    out_->setObjectName("trimEnd");
    length_->setObjectName("clipLength");
    length_->setToolTip("Length of this piece, measured from Start. Rounded to the nearest video frame.");
    for (auto *spin : {in_, out_, length_}) {
        spin->setDecimals(3);
        spin->setSingleStep(1.0 / Project::fps);
        spin->setSuffix(" s");
        spin->setKeyboardTracking(false);
    }
    editForm->addRow("Start", in_);
    editForm->addRow("End", out_);
    editForm->addRow("Length", length_);
    clipLayout->addWidget(editBox);
    auto *rotationBox = new QGroupBox("Rotate");
    auto *rotationLayout = new QVBoxLayout(rotationBox);
    rotation_ = new QLabel("No clip selected", rotationBox);
    rotation_->setObjectName("clipRotation");
    rotation_->setAccessibleName("Selected clip rotation");
    rotation_->setWordWrap(true);
    rotationLayout->addWidget(rotation_);
    auto *rotationButtons = new QGridLayout;
    rotationButtons->setSpacing(6);
    rotateLeft_ = new QPushButton("Left 90°", rotationBox);
    rotateRight_ = new QPushButton("Right 90°", rotationBox);
    rotate180_ = new QPushButton("Turn 180°", rotationBox);
    rotateLeft_->setObjectName("rotateLeftButton");
    rotateRight_->setObjectName("rotateRightButton");
    rotate180_->setObjectName("rotate180Button");
    rotateLeft_->setAccessibleName("Rotate selected clip 90 degrees left");
    rotateRight_->setAccessibleName("Rotate selected clip 90 degrees right");
    rotate180_->setAccessibleName("Rotate selected clip 180 degrees");
    rotateLeft_->setToolTip("Turn this clip 90° counterclockwise.");
    rotateRight_->setToolTip("Turn this clip 90° clockwise.");
    rotate180_->setToolTip("Turn this clip upside down.");
    rotationButtons->addWidget(rotateLeft_, 0, 0);
    rotationButtons->addWidget(rotateRight_, 0, 1);
    rotationButtons->addWidget(rotate180_, 1, 0, 1, 2);
    rotationLayout->addLayout(rotationButtons);
    clipLayout->addWidget(rotationBox);
    connect(rotateLeft_, &QPushButton::clicked, this, [this] { rotateClip(-90); });
    connect(rotateRight_, &QPushButton::clicked, this, [this] { rotateClip(90); });
    connect(rotate180_, &QPushButton::clicked, this, [this] { rotateClip(180); });
    auto *trimHint = new QLabel("Start and Length choose the section to keep. Move the playhead inside a clip and press Split to create pieces.");
    trimHint->setObjectName("panelHint");
    trimHint->setWordWrap(true);
    clipLayout->addWidget(trimHint);
    clipLayout->addStretch();
    connect(in_, &QDoubleSpinBox::editingFinished, this, &MainWindow::commitTrim);
    connect(out_, &QDoubleSpinBox::editingFinished, this, &MainWindow::commitTrim);
    connect(length_, &QDoubleSpinBox::editingFinished, this, &MainWindow::commitLength);

    auto *audioLayout = inspectorPage("Audio");
    auto *voiceBox = new QGroupBox("Narration");
    auto *voiceForm = new QFormLayout(voiceBox);
    voiceForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto *voiceRow = new QWidget(voiceBox);
    auto *voiceLayout = new QVBoxLayout(voiceRow);
    voiceLayout->setContentsMargins(0, 0, 0, 0);
    voicePath_ = new QLineEdit(voiceRow);
    voicePath_->setReadOnly(true);
    voicePath_->setPlaceholderText("No narration");
    voicePath_->setAccessibleName("Narration audio file");
    voiceLayout->addWidget(voicePath_);
    auto *voiceButtons = new QHBoxLayout;
    auto *voiceChoose = new QPushButton("Choose", voiceRow);
    voiceChoose->setAccessibleName("Choose narration audio file");
    voiceClear_ = new QPushButton("Clear", voiceRow);
    voiceClear_->setAccessibleName("Clear narration audio file");
    voiceButtons->addWidget(voiceChoose);
    voiceButtons->addWidget(voiceClear_);
    voiceLayout->addLayout(voiceButtons);
    voiceForm->addRow(voiceRow);
    voiceStart_ = new QDoubleSpinBox(voiceBox);
    voiceStart_->setRange(0, 86400);
    voiceStart_->setDecimals(3);
    voiceStart_->setSingleStep(0.1);
    voiceStart_->setSuffix(" s");
    voiceStart_->setKeyboardTracking(false);
    voiceStart_->setToolTip("Where narration begins in the finished video.");
    voiceForm->addRow("Starts at", voiceStart_);
    audioLayout->addWidget(voiceBox);
    connect(voiceChoose, &QPushButton::clicked, this, &MainWindow::chooseVoice);
    connect(voiceClear_, &QPushButton::clicked, this, [this] {
        if (project_.voicePath.isEmpty()) return;
        project_.voicePath.clear();
        changed("Remove narration");
    });
    connect(voiceStart_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (qFuzzyCompare(project_.voiceStartSeconds + 1, voiceStart_->value() + 1)) return;
        project_.voiceStartSeconds = voiceStart_->value();
        changed("Move narration");
    });
    auto *musicBox = new QGroupBox("Background music");
    auto *musicForm = new QFormLayout(musicBox);
    musicForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto *mixkitBrowse = new QPushButton("Browse Mixkit", musicBox);
    mixkitBrowse->setObjectName("mixkitBrowse");
    mixkitBrowse->setAccessibleName("Browse free background music on Mixkit");
    mixkitBrowse->setToolTip("Open Mixkit's free music library in your browser.");
    musicForm->addRow(mixkitBrowse);
    auto *mixkitHint = new QLabel("Download a track from Mixkit, then select it in Recent downloads. Choose also opens any audio file.", musicBox);
    mixkitHint->setObjectName("panelHint");
    mixkitHint->setWordWrap(true);
    musicForm->addRow(mixkitHint);
    connect(mixkitBrowse, &QPushButton::clicked, this, [this] {
        const QUrl url("https://mixkit.co/free-stock-music/");
        if (!QDesktopServices::openUrl(url))
            QMessageBox::warning(this, "Could not open Mixkit",
                "Open " + url.toString() + " in your browser to download a track, then use Choose to add it.");
    });
    auto *recentMusic = new QPushButton("Recent downloads", musicBox);
    recentMusic->setObjectName("recentMusicDownloads");
    recentMusic->setAccessibleName("Add background music from recent downloads");
    recentMusic->setToolTip("Choose from the ten newest audio files in your Downloads folder.");
    auto *recentMenu = new QMenu(recentMusic);
    recentMenu->setToolTipsVisible(true);
    recentMusic->setMenu(recentMenu);
    musicForm->addRow(recentMusic);
    connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu] {
        recentMenu->clear();
        const auto downloads = Media::recentAudioFiles(
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
        for (const auto &path : downloads) {
            auto *action = recentMenu->addAction(QFileInfo(path).fileName().replace("&", "&&"),
                this, [this, path] { addMusic(path); });
            action->setToolTip(path);
        }
        if (downloads.isEmpty())
            recentMenu->addAction("No audio files found in Downloads")->setEnabled(false);
        recentMenu->addSeparator();
        recentMenu->addAction("Choose a file…", this, &MainWindow::chooseMusic);
    });
    auto *musicRow = new QWidget(musicBox);
    auto *musicLayout = new QVBoxLayout(musicRow);
    musicLayout->setContentsMargins(0, 0, 0, 0);
    musicPath_ = new QLineEdit(musicRow);
    musicPath_->setReadOnly(true);
    musicPath_->setPlaceholderText("No music");
    musicPath_->setAccessibleName("Background music audio file");
    musicLayout->addWidget(musicPath_);
    auto *musicButtons = new QHBoxLayout;
    auto *musicChoose = new QPushButton("Choose", musicRow);
    musicChoose->setAccessibleName("Choose background music file");
    musicClear_ = new QPushButton("Clear", musicRow);
    musicClear_->setAccessibleName("Clear background music file");
    musicButtons->addWidget(musicChoose);
    musicButtons->addWidget(musicClear_);
    musicLayout->addLayout(musicButtons);
    musicForm->addRow(musicRow);
    musicDb_ = new QDoubleSpinBox(musicBox);
    musicDb_->setObjectName("musicLevel");
    musicDb_->setRange(-60, 12);
    musicDb_->setDecimals(0);
    musicDb_->setSingleStep(1);
    musicDb_->setSuffix(" dB");
    musicDb_->setKeyboardTracking(false);
    musicDb_->setToolTip("Lower values make music quieter. −18 dB is a starting point under speech.");
    musicForm->addRow("Level", musicDb_);
    auto *loopHint = new QLabel("Music repeats until the video ends.", musicBox);
    loopHint->setObjectName("panelHint");
    loopHint->setWordWrap(true);
    musicForm->addRow(loopHint);
    audioLayout->addWidget(musicBox);
    audioLayout->addStretch();
    connect(musicChoose, &QPushButton::clicked, this, &MainWindow::chooseMusic);
    connect(musicClear_, &QPushButton::clicked, this, [this] {
        if (project_.musicPath.isEmpty()) return;
        project_.musicPath.clear();
        changed("Remove music");
    });
    connect(musicDb_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (qFuzzyCompare(project_.musicDb + 61, musicDb_->value() + 61)) return;
        project_.musicDb = musicDb_->value();
        changed("Adjust music level");
    });

    auto *transitionLayout = inspectorPage("Fades");
    auto *transitionBox = new QGroupBox("Between clips");
    auto *transitionForm = new QFormLayout(transitionBox);
    transitionForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    crossfade_ = new QDoubleSpinBox(transitionBox);
    crossfade_->setRange(0, 30);
    crossfade_->setDecimals(3);
    crossfade_->setSingleStep(0.1);
    crossfade_->setSuffix(" s");
    crossfade_->setKeyboardTracking(false);
    crossfade_->setToolTip("A crossfade at every cut; short clips automatically use a shorter fade.");
    transitionForm->addRow("Crossfade", crossfade_);
    transitionLayout->addWidget(transitionBox);
    auto *fadeHint = new QLabel("One fade length applies between imported clips. Split points stay as straight cuts. Set the length to 0 for no fades. Overlaps are shown on the timeline.");
    fadeHint->setObjectName("panelHint");
    fadeHint->setWordWrap(true);
    transitionLayout->addWidget(fadeHint);
    transitionLayout->addStretch();
    connect(crossfade_, &QDoubleSpinBox::editingFinished, this, [this] {
        const int frames = qRound(crossfade_->value() * Project::fps);
        if (frames == project_.crossfadeFrames) return;
        project_.crossfadeFrames = frames;
        changed("Adjust crossfades");
    });

    workspaceSplitter_->setStretchFactor(0, 0);
    workspaceSplitter_->setStretchFactor(1, 1);
    workspaceSplitter_->setStretchFactor(2, 0);
    workspaceSplitter_->setSizes({250, 750, 300});
    vertical->addWidget(workspaceSplitter_);
    auto *timelinePanel = new QWidget(vertical);
    timelinePanel->setObjectName("timelinePanel");
    auto *timelineLayout = new QVBoxLayout(timelinePanel);
    timelineLayout->setContentsMargins(0, 0, 0, 0);
    timelineLayout->setSpacing(0);
    auto *timelineToolbar = new QWidget(timelinePanel);
    timelineToolbar->setObjectName("timelineToolbar");
    auto *timelineTools = new QHBoxLayout(timelineToolbar);
    timelineTools->setContentsMargins(14, 6, 14, 6);
    timelineTools->setSpacing(6);
    auto *timelineTitle = new QLabel("Timeline", timelineToolbar);
    timelineTitle->setObjectName("panelTitle");
    timelineSummary_ = new QLabel("0 clips", timelineToolbar);
    timelineSummary_->setObjectName("panelHint");
    timelineTools->addWidget(timelineTitle);
    timelineTools->addWidget(timelineSummary_);
    timelineTools->addStretch();
    split_ = new QPushButton("Split", timelineToolbar);
    split_->setObjectName("splitClipButton");
    split_->setAccessibleName("Split selected clip at playhead");
    split_->setEnabled(false);
    timelineTools->addWidget(split_);
    connect(split_, &QPushButton::clicked, this, &MainWindow::splitClip);
    auto *undoButton = new QPushButton("Undo", timelineToolbar);
    auto *redoButton = new QPushButton("Redo", timelineToolbar);
    undoButton->setObjectName("quietButton");
    redoButton->setObjectName("quietButton");
    undoButton->setEnabled(false);
    redoButton->setEnabled(false);
    connect(undoButton, &QPushButton::clicked, undo_, &QUndoStack::undo);
    connect(redoButton, &QPushButton::clicked, undo_, &QUndoStack::redo);
    connect(undo_, &QUndoStack::canUndoChanged, undoButton, &QPushButton::setEnabled);
    connect(undo_, &QUndoStack::canRedoChanged, redoButton, &QPushButton::setEnabled);
    timelineTools->addWidget(undoButton);
    timelineTools->addWidget(redoButton);
    auto *zoomOut = new QPushButton("−", timelineToolbar);
    auto *zoomIn = new QPushButton("+", timelineToolbar);
    auto *fit = new QPushButton("Fit", timelineToolbar);
    zoomOut->setAccessibleName("Zoom out timeline");
    zoomIn->setAccessibleName("Zoom in timeline");
    fit->setAccessibleName("Fit entire edit in timeline");
    for (auto *button : {zoomOut, zoomIn, fit}) {
        button->setObjectName("quietButton");
        timelineTools->addWidget(button);
    }
    timelineLayout->addWidget(timelineToolbar);
    visualTimeline_ = new TimelineWidget(timelinePanel);
    visualTimeline_->setObjectName("timeline");
    timelineLayout->addWidget(visualTimeline_, 1);
    timelinePanel->setMinimumHeight(std::max(220, fontMetrics().height() * 6 + 54)
                                   + timelineToolbar->sizeHint().height());
    connect(zoomOut, &QPushButton::clicked, visualTimeline_, &TimelineWidget::zoomOut);
    connect(zoomIn, &QPushButton::clicked, visualTimeline_, &TimelineWidget::zoomIn);
    connect(fit, &QPushButton::clicked, visualTimeline_, &TimelineWidget::fitToWidth);
    connect(visualTimeline_, &TimelineWidget::filesDropped, this, &MainWindow::addFiles);
    connect(visualTimeline_, &TimelineWidget::seekRequested, this, [this](int frame) {
        if (seek_->isEnabled()) preview_->seek(frame);
    });
    connect(visualTimeline_, &TimelineWidget::clipSelected, this, [this](int row) {
        QSignalBlocker blocked(clips_);
        clips_->setCurrentRow(row);
        selectionBeforeEdit_ = row;
        showSelectedClip();
        inspectorTabs_->setCurrentIndex(0);
    });
    connect(visualTimeline_, &TimelineWidget::clipMoved, this, [this](int from, int to) {
        if (from < 0 || to < 0 || from >= project_.clips.size() || to >= project_.clips.size() || from == to) return;
        project_.clips.move(from, to);
        updateClipList(to);
        changed("Reorder clips");
    });
    connect(visualTimeline_, &TimelineWidget::trimRequested, this, [this](int row, double start, double end) {
        if (row < 0 || row >= project_.clips.size()) return;
        const auto &clip = project_.clips[row];
        const double source = std::max(clip.outSeconds, sourceDuration(clip.path));
        Clip edited = clip;
        edited.inSeconds = std::max(0.0, start);
        edited.outSeconds = std::min(source, end);
        if (project_.clipFrames(edited) < 1) return;
        project_.clips[row] = edited;
        project_.normalizeVisuals();
        updateClipList(row);
        changed("Trim clip");
    });
    connect(visualTimeline_, &TimelineWidget::audioSelected, this, [this](int) {
        inspectorTabs_->setCurrentIndex(1);
        if (browserTabs_->indexOf(inspectorPanel_) >= 0) browserTabs_->setCurrentWidget(inspectorPanel_);
    });
    vertical->addWidget(timelinePanel);
    vertical->setSizes({500, 270});
    vertical->setStretchFactor(0, 1);
    vertical->setStretchFactor(1, 0);
    vertical->setMinimumHeight(workspaceSplitter_->minimumHeight() + timelinePanel->minimumHeight() + 5);
    workspaceScroll->setWidget(vertical);
    shellLayout->addWidget(workspaceScroll, 1);
    setCentralWidget(shell);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setFixedWidth(160);
    progress_->hide();
    statusBar()->addPermanentWidget(progress_);
}


void MainWindow::makeMenus()
{
    auto *file = menuBar()->addMenu("File");
    auto *newAction = file->addAction("New project");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newProject);
    auto *openAction = file->addAction("Open project…");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openProject);
    auto *saveAction = file->addAction("Save project");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this] { save(false); });
    auto *saveAsAction = file->addAction("Save project as…");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this] { save(true); });
    file->addSeparator();
    auto *exportAction = file->addAction("Export MP4…");
    exportAction->setShortcut(QKeySequence("Ctrl+E"));
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportVideo);
    file->addSeparator();
    auto *quitAction = file->addAction("Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto *edit = menuBar()->addMenu("Edit");
    auto *undoAction = undo_->createUndoAction(this, "Undo");
    undoAction->setObjectName("undoAction");
    undoAction->setShortcut(QKeySequence::Undo);
    edit->addAction(undoAction);
    auto *redoAction = undo_->createRedoAction(this, "Redo");
    redoAction->setObjectName("redoAction");
    redoAction->setShortcut(QKeySequence::Redo);
    edit->addAction(redoAction);
    edit->addSeparator();
    splitAction_ = edit->addAction("Split clip at playhead");
    splitAction_->setObjectName("splitClipAction");
    splitAction_->setShortcut(QKeySequence("Ctrl+B"));
    splitAction_->setEnabled(false);
    connect(splitAction_, &QAction::triggered, this, &MainWindow::splitClip);
    auto *view = menuBar()->addMenu("View");
    auto *focusClips = view->addAction("Focus clips");
    focusClips->setShortcut(QKeySequence("Ctrl+1"));
    connect(focusClips, &QAction::triggered, this, [this] {
        browserTabs_->setCurrentIndex(0);
        clips_->setFocus();
    });
    auto *focusTimeline = view->addAction("Focus timeline");
    focusTimeline->setShortcut(QKeySequence("Ctrl+2"));
    connect(focusTimeline, &QAction::triggered, visualTimeline_, [this] { visualTimeline_->setFocus(); });
    auto *focusInspector = view->addAction("Focus inspector");
    focusInspector->setShortcut(QKeySequence("Ctrl+4"));
    connect(focusInspector, &QAction::triggered, this, [this] {
        if (browserTabs_->indexOf(inspectorPanel_) >= 0) browserTabs_->setCurrentWidget(inspectorPanel_);
        inspectorTabs_->setFocus();
    });
}

void MainWindow::updateClipList(int selected)
{
    QSignalBlocker blocked(clips_);
    clips_->clear();
    for (int i = 0; i < project_.clips.size(); ++i) {
        const auto &clip = project_.clips[i];
        clips_->addItem(QString("%1. %2\n%3")
                            .arg(i + 1)
                            .arg(QFileInfo(clip.path).fileName())
                            .arg(clockText(project_.clipFrames(clip))));
        clips_->item(i)->setToolTip(clip.path);
        clips_->item(i)->setData(Qt::UserRole, i);
    }
    if (selected >= 0 && selected < clips_->count()) clips_->setCurrentRow(selected);
    clipCount_->setText(QString("%1 clip%2").arg(clips_->count()).arg(clips_->count() == 1 ? "" : "s"));
    showSelectedClip();
    updateVisualTimeline();
    export_->setEnabled(!project_.clips.isEmpty());
}

void MainWindow::showSelectedClip()
{
    const int row = clips_->currentRow();
    const bool selected = row >= 0 && row < project_.clips.size();
    remove_->setEnabled(selected);
    up_->setEnabled(selected && row > 0);
    down_->setEnabled(selected && row < int(project_.clips.size()) - 1);
    in_->setEnabled(selected);
    out_->setEnabled(selected);
    length_->setEnabled(selected);
    for (auto *button : {rotateLeft_, rotateRight_, rotate180_}) button->setEnabled(selected);
    updateSplitAvailability();
    visualTimeline_->setSelectedClip(selected ? row : -1);
    if (!selected) {
        clipName_->setText("No clip selected");
        clipMeta_->setText("Select a clip in the browser or timeline");
        rotation_->setText("No clip selected");
        QSignalBlocker blockIn(in_), blockOut(out_), blockLength(length_);
        in_->setRange(0, 0);
        out_->setRange(0, 0);
        length_->setRange(0, 0);
        in_->setValue(0);
        out_->setValue(0);
        length_->setValue(0);
        return;
    }
    const auto &clip = project_.clips[row];
    rotation_->setText(clip.rotationDegrees == 0 ? "Original orientation"
                                              : QString("%1° clockwise").arg(clip.rotationDegrees));
    clipName_->setText(QFileInfo(clip.path).fileName());
    clipName_->setToolTip(clip.path);
    clipMeta_->setText(QString("Clip %1 of %2 · %3\nSource trim · 60 fps timeline")
        .arg(row + 1).arg(project_.clips.size()).arg(clockText(project_.clipFrames(clip))));
    const double sourceSeconds = sourceDuration(clip.path);
    const double end = std::max(clip.outSeconds, sourceSeconds);
    QSignalBlocker blockIn(in_), blockOut(out_), blockLength(length_);
    in_->setRange(0, std::max(0.0, clip.outSeconds - 1.0 / Project::fps));
    out_->setRange(clip.inSeconds + 1.0 / Project::fps, end);
    in_->setValue(clip.inSeconds);
    out_->setValue(clip.outSeconds);
    const int availableFrames = std::max(1, qRound(end * Project::fps) - qRound(clip.inSeconds * Project::fps));
    length_->setRange(1.0 / Project::fps, availableFrames / double(Project::fps));
    length_->setValue(project_.clipFrames(clip) / double(Project::fps));
}

void MainWindow::updateAudioFields()
{
    voicePath_->setText(QFileInfo(project_.voicePath).fileName());
    voicePath_->setToolTip(project_.voicePath);
    musicPath_->setText(QFileInfo(project_.musicPath).fileName());
    musicPath_->setToolTip(project_.musicPath);
    voiceClear_->setEnabled(!project_.voicePath.isEmpty());
    musicClear_->setEnabled(!project_.musicPath.isEmpty());
    voiceStart_->setEnabled(!project_.voicePath.isEmpty());
    QSignalBlocker voiceBlocked(voiceStart_), musicBlocked(musicDb_);
    voiceStart_->setValue(project_.voiceStartSeconds);
    musicDb_->setValue(project_.musicDb);
}

void MainWindow::updateTime()
{
    const int total = project_.durationFrames();
    const int now = seek_->isSliderDown() ? seek_->value() : preview_->position();
    time_->setText(clockText(now) + " / " + clockText(total));
    updateSplitAvailability();
}

void MainWindow::updateTitle()
{
    const QString name = projectPath_.isEmpty() ? "Untitled" : QFileInfo(projectPath_).fileName();
    setWindowTitle(name + "[*] — IsaacVideoEditor");
    setWindowModified(dirty_);
    projectTitle_->setText(name);
    projectTitle_->setToolTip(name);
    saveState_->setText(dirty_ ? "Unsaved changes" : (projectPath_.isEmpty() ? "New project" : "Saved locally"));
}

void MainWindow::showFrame()
{
    if (lastFrame_.isNull()) return;
    image_->setPixmap(QPixmap::fromImage(lastFrame_).scaled(
        image_->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

void MainWindow::rebuildTimeline(int position)
{
    previewPriorityStart_ = previewPriorityEnd_ = -1;
    preview_->setProducer(nullptr);
    timeline_.reset();
    lastFrame_ = QImage();
    image_->clear();
    image_->setText(project_.clips.isEmpty() ? "Your story starts here\n\nImport or drop video clips to begin" : "Loading preview…");
    play_->setText("Play");
    play_->setEnabled(false);
    seek_->setEnabled(false);
    {
        QSignalBlocker blocked(seek_);
        seek_->setRange(0, std::max(0, project_.durationFrames() - 1));
        seek_->setValue(0);
    }
    updateTime();
    updateVisualTimeline();
    if (project_.clips.isEmpty()) {
        previewCache_->setRequests({});
        previewPathsUsed_.clear();
        format_->setText(QString("Auto resolution · %1 fps").arg(Project::fps));
        previewState_->setText("No media");
        statusBar()->showMessage("Add video clips to start.");
        return;
    }
    QString error = project_.validate();
    if (!error.isEmpty()) {
        image_->setText(error);
        statusBar()->showMessage(error);
        return;
    }
    const auto output = Media::projectOutputSize(project_, &error);
    if (!output) {
        image_->setText(error);
        statusBar()->showMessage(error);
        format_->setText("Resolution unavailable");
        return;
    }
    format_->setText(QString("%1×%2 · %3 fps").arg(output->width).arg(output->height).arg(Project::fps));
    format_->setToolTip("Export uses the lowest native resolution in this edit, using original files.");
    const double scale = std::min({1.0, 960.0 / output->width, 540.0 / output->height});
    const int width = std::max(2, qRound(output->width * scale / 2) * 2);
    const int height = std::max(2, qRound(output->height * scale / 2) * 2);
    Media::configureOutputProfile(profile_, width, height);

    QStringList previewSources;
    QVector<PreviewCache::Request> requests;
    QVector<int> priority;
    for (int row = 0; row < project_.clips.size(); ++row) priority.append(row);
    const auto distance = [this, position](int row) {
        const int start = project_.clipStartFrame(row), end = start + project_.clipFrames(project_.clips[row]);
        return position < start ? start - position : (position < end ? 0 : position - end);
    };
    std::stable_sort(priority.begin(), priority.end(), [&](int a, int b) { return distance(a) < distance(b); });
    for (int row : priority) {
        const auto &clip = project_.clips[row];
        const auto info = Media::mediaInfo(clip.path);
        if (info && qint64(info->width) * info->height > 1920 * 1080) {
            previewSources.append(clip.path);
            requests.append({clip.path, qRound(info->durationSeconds * Project::fps),
                             qRound(clip.inSeconds * Project::fps), qRound(clip.outSeconds * Project::fps)});
        }
    }
    previewCache_->setRequests(requests);
    Project previewProject = project_;
    previewPathsUsed_.clear();
    int ready = 0;
    for (auto &clip : previewProject.clips) {
        const QString original = clip.path;
        if (previewSources.contains(original)) {
            const auto copy = previewCache_->readyCopy(original, qRound(clip.inSeconds * Project::fps),
                                                       qRound(clip.outSeconds * Project::fps));
            if (!copy.path.isEmpty()) {
                clip.path = copy.path;
                clip.inSeconds -= copy.startFrame / double(Project::fps);
                clip.outSeconds -= copy.startFrame / double(Project::fps);
                ++ready;
            }
        }
        previewPathsUsed_.append(clip.path);
    }
    previewState_->setText(previewSources.isEmpty() ? "Original media"
        : (ready == previewSources.size() ? "Smooth preview"
           : (previewCache_->isBusy() ? "Preparing smooth preview…" : "Using original media")));
    previewState_->setToolTip("Small preview copies make editing smoother. Export always reads the original files.");
    timeline_ = Media::buildTimeline(previewProject, profile_, &error);
    bool connected = timeline_ && preview_->setProducer(timeline_.get());
    if (!connected && ready > 0) {
        preview_->setProducer(nullptr);
        timeline_.reset();
        for (int row = 0; row < project_.clips.size(); ++row) {
            const auto &clip = project_.clips[row];
            if (previewPathsUsed_.value(row) != clip.path) previewCache_->discard(clip.path);
            previewPathsUsed_[row] = clip.path;
        }
        error.clear();
        timeline_ = Media::buildTimeline(project_, profile_, &error);
        connected = timeline_ && preview_->setProducer(timeline_.get());
        previewState_->setText("Using original media");
        previewState_->setToolTip("A preview copy could not be read. Editing is using the original files.");
    }
    if (!connected) {
        image_->setText(error.isEmpty() ? "Could not preview this project." : error);
        statusBar()->showMessage(image_->text());
        timeline_.reset();
        return;
    }
    play_->setEnabled(true);
    seek_->setEnabled(true);
    preview_->seek(std::clamp(position, 0, std::max(0, project_.durationFrames() - 1)));
    statusBar()->showMessage("Drag clips to reorder · drag selected edges to trim · Space to play");
}

void MainWindow::prioritizePreview(int position)
{
    if (position >= previewPriorityStart_ && position < previewPriorityEnd_) return;
    int start = 0;
    const int overlap = project_.effectiveCrossfadeFrames();
    for (int row = 0; row < project_.clips.size(); ++row) {
        const auto &clip = project_.clips[row];
        const int end = start + project_.clipFrames(clip);
        if (position >= start && position < end) {
            previewPriorityStart_ = start;
            previewPriorityEnd_ = end;
            previewCache_->prioritize(clip.path, qRound(clip.inSeconds * Project::fps),
                                      qRound(clip.outSeconds * Project::fps));
            return;
        }
        start = end - (row + 1 < project_.clips.size() && !project_.clips[row + 1].hardCutBefore ? overlap : 0);
    }
}

void MainWindow::changed(const QString &label)
{
    undo_->push(new ProjectEdit(committedProject_, project_, selectionBeforeEdit_, clips_->currentRow(), preview_->position(), label,
        [this](const Project &project, int selected, int position) { applySnapshot(project, selected, position); }));
}

void MainWindow::applySnapshot(const Project &project, int selected, int position)
{
    project_ = project;
    committedProject_ = project;
    const int row = project_.clips.isEmpty() || selected < 0 ? -1 : std::min(selected, int(project_.clips.size()) - 1);
    updateClipList(row);
    selectionBeforeEdit_ = row;
    updateAudioFields();
    {
        QSignalBlocker blocked(crossfade_);
        crossfade_->setValue(project_.crossfadeFrames / double(Project::fps));
    }
    rebuildTimeline(position);
}

double MainWindow::sourceDuration(const QString &path)
{
    if (path.isEmpty()) return 0;
    return Media::mediaDurationSeconds(path);
}

void MainWindow::updateVisualTimeline()
{
    QVector<double> durations;
    durations.reserve(project_.clips.size());
    for (const auto &clip : project_.clips) durations.append(std::max(clip.outSeconds, sourceDuration(clip.path)));
    visualTimeline_->setProject(project_, durations, sourceDuration(project_.voicePath));
    visualTimeline_->setSelectedClip(clips_->currentRow());
    visualTimeline_->setPosition(preview_->position());
    timelineSummary_->setText(QString("%1 clips · %2").arg(project_.clips.size()).arg(clockText(project_.durationFrames())));
}

void MainWindow::updateWorkspaceLayout()
{
    if (!browserTabs_ || !inspectorPanel_) return;
    const bool compact = width() < std::max(1040, fontMetrics().horizontalAdvance("M") * 90);
    const int inspectorWidth = std::max(260, fontMetrics().horizontalAdvance("Choose Clear") + 144);
    inspectorPanel_->setMinimumWidth(inspectorWidth);
    browserTabs_->setMinimumWidth(compact ? inspectorWidth : 210);
    const int tab = browserTabs_->indexOf(inspectorPanel_);
    if (compact && tab < 0) {
        browserTabs_->addTab(inspectorPanel_, "Inspector");
        workspaceSplitter_->setSizes({inspectorWidth, std::max(240, width() - inspectorWidth)});
    } else if (!compact && tab >= 0) {
        browserTabs_->removeTab(tab);
        workspaceSplitter_->addWidget(inspectorPanel_);
        inspectorPanel_->show();
        workspaceSplitter_->setSizes({250, std::max(300, width() - 550), 300});
    }
}

bool MainWindow::maybeSave()
{
    if (!dirty_) return true;
    const auto answer = QMessageBox::question(this, "Save project?",
        "Save your changes before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    return answer == QMessageBox::Discard || save(false);
}

bool MainWindow::save(bool choosePath)
{
    QString path = projectPath_;
    if (path.isEmpty() || choosePath) {
        path = QFileDialog::getSaveFileName(this, "Save project",
            QDir(moviesDirectory()).filePath(QFileInfo(path).fileName()),
            "IsaacVideoEditor projects (*.isaac.json)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".isaac.json", Qt::CaseInsensitive)) {
            if (path.endsWith(".json", Qt::CaseInsensitive)) path.chop(5);
            path += ".isaac.json";
        }
    }
    QString error;
    if (!saveProject(project_, path, &error)) {
        QMessageBox::warning(this, "Could not save project", error);
        return false;
    }
    projectPath_ = path;
    undo_->setClean();
    dirty_ = false;
    updateTitle();
    statusBar()->showMessage("Project saved.", 5000);
    return true;
}

void MainWindow::newProject()
{
    if (!maybeSave()) return;
    project_ = Project{};
    committedProject_ = project_;
    selectionBeforeEdit_ = -1;
    undo_->clear();
    projectPath_.clear();
    dirty_ = false;
    updateClipList();
    updateAudioFields();
    crossfade_->setValue(0);
    rebuildTimeline();
    updateTitle();
}

void MainWindow::openProject()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, "Open project", moviesDirectory(),
        "IsaacVideoEditor projects (*.isaac.json);;JSON files (*.json)");
    if (path.isEmpty()) return;
    QString error;
    auto loaded = loadProject(path, &error);
    if (!loaded) {
        QMessageBox::warning(this, "Could not open project", error);
        return;
    }
    project_ = std::move(*loaded);
    committedProject_ = project_;
    undo_->clear();
    projectPath_ = path;
    dirty_ = false;
    updateClipList(project_.clips.isEmpty() ? -1 : 0);
    selectionBeforeEdit_ = clips_->currentRow();
    updateAudioFields();
    crossfade_->setValue(project_.crossfadeFrames / double(Project::fps));
    rebuildTimeline();
    updateTitle();
}

void MainWindow::addClips()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Add video clips", moviesDirectory(), videoFilter());
    addFiles(paths);
}

void MainWindow::addFiles(const QStringList &paths)
{
    if (paths.isEmpty()) return;
    const int previousCount = project_.clips.size();
    QStringList problems;
    const QSet<QString> extensions = {"mp4", "mov", "m4v", "mkv", "avi", "mts", "webm"};
    for (const auto &path : paths) {
        const QFileInfo file(path);
        if (!file.isFile() || !extensions.contains(file.suffix().toLower())) {
            problems << file.fileName() + ": Not a supported video file.";
            continue;
        }
        QString error;
        const double seconds = Media::mediaDurationSeconds(path, &error);
        if (seconds < 1.0 / Project::fps) {
            problems << QFileInfo(path).fileName() + ": " +
                (error.isEmpty() ? "No usable video duration." : error);
            continue;
        }
        project_.clips.append({path, 0, seconds});
    }
    if (project_.clips.size() > previousCount) {
        updateClipList(project_.clips.size() - 1);
        changed("Import clips");
    }
    if (!problems.isEmpty()) QMessageBox::warning(this, "Some clips were not added", problems.join("\n"));
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!localFilePaths(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (!localFilePaths(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    QMainWindow::dragMoveEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const auto files = localFilePaths(event->mimeData());
    if (!files.isEmpty()) {
        addFiles(files);
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    QMainWindow::dropEvent(event);
}

void MainWindow::removeClip()
{
    const int row = clips_->currentRow();
    if (row < 0 || row >= project_.clips.size()) return;
    project_.removeClip(row);
    updateClipList(std::min(row, int(project_.clips.size()) - 1));
    changed("Remove clip");
}

void MainWindow::moveClip(int direction)
{
    const int row = clips_->currentRow();
    const int target = row + direction;
    if (row < 0 || target < 0 || target >= project_.clips.size()) return;
    project_.clips.move(row, target);
    project_.normalizeVisuals();
    updateClipList(target);
    changed("Reorder clips");
}

void MainWindow::commitTrim()
{
    const int row = clips_->currentRow();
    if (row < 0 || row >= project_.clips.size()) return;
    Clip edited = project_.clips[row];
    edited.inSeconds = qRound(in_->value() * Project::fps) / double(Project::fps);
    edited.outSeconds = qRound(out_->value() * Project::fps) / double(Project::fps);
    if (project_.clipFrames(edited) < 1) {
        QMessageBox::warning(this, "Invalid trim", "The clip must contain at least one frame.");
        showSelectedClip();
        return;
    }
    if (qFuzzyCompare(edited.inSeconds + 1, project_.clips[row].inSeconds + 1) &&
        qFuzzyCompare(edited.outSeconds + 1, project_.clips[row].outSeconds + 1)) {
        showSelectedClip();
        return;
    }
    project_.clips[row] = edited;
    project_.normalizeVisuals();
    updateClipList(row);
    changed("Trim clip");
}

void MainWindow::commitLength()
{
    const int row = clips_->currentRow();
    if (row < 0 || row >= project_.clips.size()) return;
    const int start = qRound(project_.clips[row].inSeconds * Project::fps);
    const int frames = std::max(1, qRound(length_->value() * Project::fps));
    out_->setValue((start + frames) / double(Project::fps));
    commitTrim();
}

void MainWindow::rotateClip(int degrees)
{
    const int row = clips_->currentRow();
    if (!project_.rotateClip(row, degrees)) return;
    updateClipList(row);
    changed("Rotate clip");
}

int MainWindow::splitOffset() const
{
    const int row = clips_->currentRow();
    if (row < 0 || row >= project_.clips.size() || !seek_->isEnabled()) return -1;
    const int offset = preview_->position() - project_.clipStartFrame(row);
    const int incoming = project_.transitionFramesBefore(row);
    const int outgoing = project_.transitionFramesBefore(row + 1);
    return offset > incoming && offset < project_.clipFrames(project_.clips[row]) - outgoing ? offset : -1;
}

void MainWindow::updateSplitAvailability()
{
    if (!split_) return;
    const bool canSplit = splitOffset() >= 0;
    split_->setEnabled(canSplit);
    if (splitAction_) splitAction_->setEnabled(canSplit);
    split_->setToolTip(canSplit ? "Split this clip at the playhead (Command+B on Mac, Ctrl+B on Windows/Linux)."
                               : "Select a clip and place the playhead inside it, away from its ends and crossfades.");
}

void MainWindow::splitClip()
{
    const int row = clips_->currentRow();
    const int offset = splitOffset();
    if (offset < 0) return;
    preview_->pause();
    if (!project_.splitClip(row, offset)) return;
    updateClipList(row + 1);
    changed("Split clip");
}

void MainWindow::chooseVoice()
{
    const QString path = QFileDialog::getOpenFileName(this, "Choose narration", moviesDirectory(), audioFilter());
    if (path.isEmpty() || path == project_.voicePath) return;
    QString error;
    if (Media::mediaDurationSeconds(path, &error) <= 0) {
        QMessageBox::warning(this, "Could not add narration", error);
        return;
    }
    project_.voicePath = path;
    updateAudioFields();
    changed("Add narration");
}

void MainWindow::chooseMusic()
{
    const QString path = QFileDialog::getOpenFileName(this, "Choose background music", moviesDirectory(), audioFilter());
    addMusic(path);
}

void MainWindow::addMusic(const QString &path)
{
    if (path.isEmpty() || path == project_.musicPath) return;
    QString error;
    if (Media::mediaDurationSeconds(path, &error) <= 0) {
        QMessageBox::warning(this, "Could not add music", error);
        return;
    }
    project_.musicPath = path;
    updateAudioFields();
    changed("Add music");
}

void MainWindow::exportVideo()
{
    if (exportThread_ || project_.clips.isEmpty()) return;
    QString error = project_.validate();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, "Cannot export", error);
        return;
    }
    const auto outputSize = Media::projectOutputSize(project_, &error);
    if (!outputSize) {
        QMessageBox::warning(this, "Cannot export", error);
        return;
    }
    QString path = QFileDialog::getSaveFileName(this,
        QString("Export MP4 · %1×%2").arg(outputSize->width).arg(outputSize->height), moviesDirectory(), "MP4 video (*.mp4)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".mp4", Qt::CaseInsensitive)) path += ".mp4";
    const QString output = QFileInfo(path).absoluteFilePath();
    const auto sameFile = [&output](const QString &source) {
        if (source.isEmpty()) return false;
        const QFileInfo info(source);
        const QString normalized = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
        const QFileInfo outputInfo(output);
        const QString normalizedOutput = outputInfo.exists()
            ? outputInfo.canonicalFilePath() : outputInfo.absoluteFilePath();
        return normalized.compare(normalizedOutput, Qt::CaseInsensitive) == 0;
    };
    for (const auto &clip : project_.clips) {
        if (sameFile(clip.path)) {
            QMessageBox::warning(this, "Cannot export", "Choose a different file from your source clips.");
            return;
        }
    }
    if (sameFile(project_.voicePath) || sameFile(project_.musicPath)) {
        QMessageBox::warning(this, "Cannot export", "Choose a different file from your audio sources.");
        return;
    }

    preview_->pause();
    previewCache_->setPaused(true);
    const Project snapshot = project_;
    auto result = std::make_shared<std::pair<bool, QString>>();
    progress_->setValue(0);
    progress_->show();
    centralWidget()->setEnabled(false);
    menuBar()->setEnabled(false);
    statusBar()->showMessage("Exporting MP4…");
    exportThread_ = QThread::create([this, snapshot, output, result] {
        int lastPercent = -1;
        result->first = Media::exportMp4(snapshot, output, [this, &lastPercent](int percent) {
            if (percent == lastPercent) return;
            lastPercent = percent;
            QMetaObject::invokeMethod(this, [this, percent] { progress_->setValue(percent); },
                                      Qt::QueuedConnection);
        }, &result->second);
    });
    exportThread_->setParent(this);
    connect(exportThread_, &QThread::finished, this, [this, output, result] {
        exportThread_ = nullptr;
        centralWidget()->setEnabled(true);
        menuBar()->setEnabled(true);
        if (previewRefreshPending_) {
            previewRefreshPending_ = false;
            rebuildTimeline(preview_->position());
        }
        previewCache_->setPaused(false);
        if (result->first) {
            progress_->setValue(100);
            statusBar()->showMessage("Exported " + QFileInfo(output).fileName(), 10000);
        } else {
            progress_->hide();
            QMessageBox::warning(this, "Export failed", result->second);
            statusBar()->showMessage("Export failed.");
        }
    });
    connect(exportThread_, &QThread::finished, exportThread_, &QObject::deleteLater);
    exportThread_->start();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (exportThread_ && exportThread_->isRunning()) {
        QMessageBox::information(this, "Export in progress", "Please wait for the MP4 export to finish.");
        event->ignore();
        return;
    }
    if (maybeSave()) event->accept();
    else event->ignore();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == image_ && event->type() == QEvent::Resize) showFrame();
    return QMainWindow::eventFilter(watched, event);
}
