#pragma once
#include "project.h"
#include <QObject>
#include <QImage>
#include <QVariantList>
#include <QVariantMap>
#include <QUndoStack>
#include <QPointer>
#include <QHash>
#include <QTimer>
#include <mlt++/Mlt.h>
#include <memory>
class Preview;
class PreviewCache;
class MediaCache;
class ExportJob;
class RecoveryStore;

class EditorController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList assets READ assets NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap currentSource READ currentSource NOTIFY sourceChanged)
    Q_PROPERTY(int selectedSource READ selectedSource NOTIFY sourceChanged)
    Q_PROPERTY(int sourcePosition READ sourcePosition NOTIFY sourceChanged)
    Q_PROPERTY(bool sourceMode READ sourceMode NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceTimeText READ sourceTimeText NOTIFY sourceChanged)
    Q_PROPERTY(QVariantList visualOverlays READ visualOverlays NOTIFY compositionChanged)
    Q_PROPERTY(QVariantList visualLanes READ visualLanes NOTIFY compositionChanged)
    Q_PROPERTY(int selectedOverlay READ selectedOverlay NOTIFY compositionChanged)
    Q_PROPERTY(QVariantMap currentTransform READ currentTransform NOTIFY compositionChanged)
    Q_PROPERTY(QVariantMap canvas READ canvas NOTIFY compositionChanged)
    Q_PROPERTY(bool transformMode READ transformMode WRITE setTransformMode NOTIFY compositionChanged)
    Q_PROPERTY(bool visualPreviewActive READ visualPreviewActive NOTIFY compositionChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY projectChanged)
    Q_PROPERTY(QVariantList captions READ captions NOTIFY projectChanged)
    Q_PROPERTY(QVariantList selectedCaptions READ selectedCaptions NOTIFY selectionChanged)
    Q_PROPERTY(int selectedCaption READ selectedCaption NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap currentCaption READ currentCaption NOTIFY selectionChanged)
    Q_PROPERTY(QStringList captionWarnings READ captionWarnings NOTIFY projectChanged)
    Q_PROPERTY(bool burnCaptions READ burnCaptions WRITE setBurnCaptions NOTIFY projectChanged)
    Q_PROPERTY(bool textGuides READ textGuides WRITE setTextGuides NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList textOverlays READ textOverlays NOTIFY projectChanged)
    Q_PROPERTY(int selectedText READ selectedText NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap currentText READ currentText NOTIFY selectionChanged)
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
    Q_PROPERTY(QVariantList selectedIndices READ selectedIndices NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap currentClip READ currentClip NOTIFY selectionChanged)
    Q_PROPERTY(int selectedClip READ selectedClip NOTIFY selectionChanged)
    Q_PROPERTY(int duration READ duration NOTIFY projectChanged)
    Q_PROPERTY(int position READ position NOTIFY positionChanged)
    Q_PROPERTY(QString timeText READ timeText NOTIFY positionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool canSplit READ canSplit NOTIFY stateChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY stateChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY stateChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY stateChanged)
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(bool hasProjectFile READ hasProjectFile NOTIFY stateChanged)
    Q_PROPERTY(QString format READ format NOTIFY stateChanged)
    Q_PROPERTY(QString previewStatus READ previewStatus NOTIFY stateChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY stateChanged)
    Q_PROPERTY(bool noticeCanUndo READ noticeCanUndo NOTIFY stateChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY stateChanged)
    Q_PROPERTY(QString exportLog READ exportLog NOTIFY stateChanged)
    Q_PROPERTY(int exportProgress READ exportProgress NOTIFY stateChanged)
    Q_PROPERTY(QString voicePath READ voicePath NOTIFY projectChanged)
    Q_PROPERTY(QString musicPath READ musicPath NOTIFY projectChanged)
    Q_PROPERTY(double voiceStart READ voiceStart NOTIFY projectChanged)
    Q_PROPERTY(double musicDb READ musicDb NOTIFY projectChanged)
    Q_PROPERTY(double crossfade READ crossfade NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap currentAudio READ currentAudio NOTIFY stateChanged)
    Q_PROPERTY(int audioLane READ audioLane NOTIFY stateChanged)
    Q_PROPERTY(int soloLane READ soloLane NOTIFY stateChanged)
    Q_PROPERTY(double masterPeak READ masterPeak NOTIFY stateChanged)
    Q_PROPERTY(bool masterClipped READ masterClipped NOTIFY stateChanged)
    Q_PROPERTY(QVariantList transitions READ transitions NOTIFY projectChanged)
    Q_PROPERTY(int selectedTransition READ selectedTransition NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap currentTransition READ currentTransition NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString recoveryStatus READ recoveryStatus NOTIFY stateChanged)
    Q_PROPERTY(QVariantList recoveryCandidates READ recoveryCandidates NOTIFY stateChanged)
    Q_PROPERTY(QVariantList missingMedia READ missingMedia NOTIFY projectChanged)
    Q_PROPERTY(QVariantList relinkProposals READ relinkProposals NOTIFY stateChanged)
    Q_PROPERTY(QString exportState READ exportState NOTIFY stateChanged)
    Q_PROPERTY(QString exportDestination READ exportDestination NOTIFY stateChanged)
    Q_PROPERTY(QString exportRevision READ exportRevision NOTIFY stateChanged)
    Q_PROPERTY(QString exportDetails READ exportDetails NOTIFY stateChanged)
public:
    explicit EditorController(QObject *parent=nullptr);
    ~EditorController() override;
    const Project &project() const { return project_; }
    QVector<double> sourceDurations() const;
    QVariantList assets() const;
    QVariantMap currentSource() const;
    int selectedSource() const { return selectedSource_; }
    int sourcePosition() const;
    bool sourceMode() const { return sourceMode_; }
    QString sourceTimeText() const;
    Q_INVOKABLE QVariantList filteredAssets(const QString &search,const QString &kind,const QString &usage) const;
    Q_INVOKABLE bool importMedia(const QStringList &paths);
    Q_INVOKABLE void importMediaUrls(const QVariantList &urls);
    Q_INVOKABLE void importAndAppendDialog();
    Q_INVOKABLE void selectSource(int index);
    Q_INVOKABLE void showProject();
    Q_INVOKABLE void seekSource(int frame);
    Q_INVOKABLE void stepSource(int frames);
    Q_INVOKABLE void beginSourceScrub();
    Q_INVOKABLE void finishSourceScrub(bool cancel=false);
    Q_INVOKABLE void markSourceIn();
    Q_INVOKABLE void markSourceOut();
    Q_INVOKABLE void setSourceRange(int first,int exclusiveEnd);
    Q_INVOKABLE void clearSourceRange();
    Q_INVOKABLE bool appendSource();
    Q_INVOKABLE bool insertSource();
    Q_INVOKABLE bool insertSourceAt(int frame);
    Q_INVOKABLE QVariantMap sourceInsertionPreview(int frame) const;
    Q_INVOKABLE bool dropMediaAt(const QStringList &paths,int frame);
    Q_INVOKABLE bool removeSource(int index,bool confirmed=false);
    Q_INVOKABLE void confirmRemoveSource(int index);
    Q_INVOKABLE void startSourceDrag(int index);
    double voiceDuration() const;
    MediaCache *mediaCache() const { return mediaCache_; }
    const QImage &frame() const { return frame_; }
    QVariantList visualOverlays() const;
    QVariantList visualLanes() const;
    int selectedOverlay() const {return selectedOverlay_;}
    QVariantMap currentTransform() const;
    QVariantMap canvas() const;
    bool transformMode() const {return transformMode_;}
    void setTransformMode(bool enabled);
    Q_INVOKABLE bool addVisualOverlay(const QString &path,int frame=-1,int lane=0,int sourceInFrame=0,int sourceOutFrame=-1);
    Q_INVOKABLE bool dropVisualMedia(const QVariantList &urls,int frame,int lane);
    bool visualPreviewActive() const { return visualBefore_.has_value(); }
    Q_INVOKABLE void chooseVisualOverlay();
    Q_INVOKABLE void selectVisualOverlay(int index);
    Q_INVOKABLE void removeVisualOverlay();
    Q_INVOKABLE void detachVisualOverlay();
    Q_INVOKABLE void detachSelectedHosts();
    Q_INVOKABLE void setVisualLaneVisible(int lane,bool visible);
    Q_INVOKABLE void setOverlayLane(int index,int lane);
    Q_INVOKABLE void setOverlayTiming(int index,int start,int sourceIn,int frames);
    Q_INVOKABLE void moveOverlayByPixels(int index,double delta,double pixelsPerSecond,int edge=0);
    Q_INVOKABLE void beginVisualGesture(int overlay=-1);
    Q_INVOKABLE void endVisualGesture(bool cancel=false);
    Q_INVOKABLE void setCanvas(int width,int height);
    Q_INVOKABLE void setTransform(const QVariantMap &fields);
    Q_INVOKABLE void resetTransform();
    Q_INVOKABLE void editKeyframe(const QString &property,const QString &action,int targetSourceFrame=-1);
    QVariantList clips() const;
    QVariantList captions() const;
    QVariantList selectedCaptions() const {return captionSelection_;}
    int selectedCaption() const {return selectedCaption_;}
    QVariantMap currentCaption() const;
    QStringList captionWarnings() const;
    bool burnCaptions() const {return project_.burnCaptions;}
    bool textGuides() const {return textGuides_;}
    void setTextGuides(bool enabled){textGuides_=enabled;emit selectionChanged();}
    Q_INVOKABLE bool importSrt(const QString &path);
    Q_INVOKABLE bool exportSrt(const QString &path);
    Q_INVOKABLE void importSrtDialog();
    Q_INVOKABLE void exportSrtDialog();
    Q_INVOKABLE void addCaption();
    Q_INVOKABLE void deleteCaptions();
    Q_INVOKABLE void selectCaption(int index,int modifiers=0);
    Q_INVOKABLE void navigateCaption(int direction);
    Q_INVOKABLE void previewCaption();
    Q_INVOKABLE void beginCaptionEdit(int index,int modifiers=0);
    Q_INVOKABLE void previewCaptionContent(const QString &text);
    Q_INVOKABLE void setCaptionTiming(int index,int start,int frames);
    Q_INVOKABLE void styleCaptions(const QVariantMap &fields,bool all=false);
    Q_INVOKABLE void chooseCaptionColor();
    Q_INVOKABLE void setBurnCaptions(bool enabled);
    Q_INVOKABLE void reviewCaptions();
    Q_INVOKABLE void attachTimedText(bool caption,int hostIndex);
    Q_INVOKABLE void detachTimedText(bool caption);
    Q_INVOKABLE void removeSelectedHosts(bool detach);
    QVariantList textOverlays() const;
    int selectedText() const { return selectedText_; }
    QVariantMap currentText() const;
    QStringList fontFamilies() const;
    Q_INVOKABLE void addText();
    Q_INVOKABLE void selectText(int index);
    Q_INVOKABLE void deleteText();
    Q_INVOKABLE void updateText(int index,const QVariantMap &fields);
    Q_INVOKABLE void chooseTextColor();
    Q_INVOKABLE void previewTextContent(const QString &text);
    Q_INVOKABLE void setTextTiming(int index,int start,int frames);
    Q_INVOKABLE void beginTextDrag(int index = -1);
    Q_INVOKABLE void dragTextTo(double x,double y);
    Q_INVOKABLE void endTextDrag(bool cancel=false);
    QVariantList selectedIndices() const { return selection_; }
    QVariantMap currentClip() const;
    int selectedClip() const { return primary_; }
    int duration() const { return project_.durationFrames(); }
    int position() const { return position_; }
    QString timeText() const;
    bool playing() const;
    bool canSplit() const;
    bool canUndo() const { return undo_.canUndo(); }
    bool canRedo() const { return undo_.canRedo(); }
    QString undoText() const { return undo_.undoText(); }
    QString redoText() const { return undo_.redoText(); }
    bool dirty() const { return sourceViewDirty_ || !undo_.isClean() || (textDragBefore_ && (project_.textOverlays!=textDragBefore_->project.textOverlays || project_.captions!=textDragBefore_->project.captions)); }
    QString title() const;
    bool hasProjectFile() const { return !path_.isEmpty(); }
    QString format() const { return format_; }
    QString previewStatus() const { return previewStatus_; }
    QString notice() const { return notice_; }
    bool noticeCanUndo() const { return noticeCanUndo_ && undo_.canUndo(); }
    bool exporting() const { return !exportJob_.isNull(); }
    QString exportLog() const { return exportLog_; }
    int exportProgress() const { return exportProgress_; }
    QString voicePath() const { return project_.voicePath; }
    QString musicPath() const { return project_.musicPath; }
    double voiceStart() const { return project_.voiceStartSeconds; }
    double musicDb() const { return project_.musicDb; }
    double crossfade() const { return project_.crossfadeFrames/60.0; }
    bool loading() const { return loading_; }
    QString recoveryStatus() const;
    QVariantList recoveryCandidates() const { return recoveryCandidates_; }
    QVariantList missingMedia() const;
    QVariantList relinkProposals() const { return relinkProposals_; }
    QString exportState() const { return exportState_; }
    QString exportDestination() const { return exportDestination_; }
    QString exportRevision() const { return exportRevision_; }
    QString exportDetails() const { return exportDetails_; }
    void enableRecovery(const QString &directory={});
    Q_INVOKABLE bool recoverSnapshot(const QString &path);
    Q_INVOKABLE void discardRecovery(const QString &path);
    Q_INVOKABLE void retryRecovery();
    Q_INVOKABLE bool relinkMedia(const QString &missing,const QString &replacement);
    Q_INVOKABLE void locateMissing(const QString &path);
    Q_INVOKABLE void chooseRelinkFolder();
    Q_INVOKABLE void searchRelinkFolder(const QString &folder);
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE bool retryExport();
    Q_INVOKABLE void revealExport();
    Q_INVOKABLE void openExport();
    Q_INVOKABLE void openExportLog();
    QVariantMap currentAudio() const;
    int audioLane() const {return audioLane_;}
    int soloLane() const {return soloLane_;}
    double masterPeak() const {return masterPeak_;}
    bool masterClipped() const {return masterClipped_;}
    QVariantList transitions() const;
    int selectedTransition() const {return transitionIndex_;}
    QVariantMap currentTransition() const;
    Q_INVOKABLE void selectAudio(int lane);
    Q_INVOKABLE void updateAudio(int lane,const QVariantMap &fields);
    Q_INVOKABLE void beginAudioGesture();
    Q_INVOKABLE void endAudioGesture(bool cancel=false);
    Q_INVOKABLE void setSolo(int lane);
    Q_INVOKABLE void resetClipping();
    Q_INVOKABLE void selectTransition(int index);
    Q_INVOKABLE void setTransition(int index,const QString &type,int frames);
    Q_INVOKABLE void removeTransition();
    Q_INVOKABLE void attachTimeline(QObject *timeline);
    Q_INVOKABLE bool importFiles(const QStringList &paths);
    Q_INVOKABLE void importUrls(const QVariantList &urls);
    Q_INVOKABLE void selectClip(int index,int modifiers=0);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectRange(int first,int last);
    Q_INVOKABLE void moveGroup(const QVariantList &indices,int boundary);
    Q_INVOKABLE void moveSelection(int direction);
    Q_INVOKABLE void removeSelection();
    Q_INVOKABLE bool trimClip(int index,double inSeconds,double outSeconds);
    Q_INVOKABLE void setTrimStart(double seconds);
    Q_INVOKABLE void setTrimLength(double seconds);
    Q_INVOKABLE void nudgeTrim(int edge,int frames);
    Q_INVOKABLE void split();
    Q_INVOKABLE void rotate(int degrees);
    Q_INVOKABLE void seek(int frame);
    Q_INVOKABLE void step(int frames);
    Q_INVOKABLE void beginScrub();
    Q_INVOKABLE void finishScrub(bool cancel=false);
    Q_INVOKABLE void skim(int frame);
    Q_INVOKABLE void endSkim();
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool saveTo(const QString &path);
    Q_INVOKABLE bool loadFrom(const QString &path);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs();
    Q_INVOKABLE void newProject();
    Q_INVOKABLE void openProject();
    Q_INVOKABLE void importDialog();
    Q_INVOKABLE void exportDialog();
    Q_INVOKABLE bool exportTo(const QString &path);
    Q_INVOKABLE bool requestClose();
    Q_INVOKABLE void chooseVoice();
    Q_INVOKABLE void chooseMusic();
    Q_INVOKABLE void setVoicePath(const QString &path);
    Q_INVOKABLE void setMusicPath(const QString &path);
    Q_INVOKABLE void setVoiceStart(double seconds);
    Q_INVOKABLE void setMusicDb(double db);
    Q_INVOKABLE void setCrossfade(double seconds);
    Q_INVOKABLE QVariantList recentMusic() const;
    Q_INVOKABLE void browseMusic();
    Q_INVOKABLE void dismissNotice();
    Q_INVOKABLE void retryPreviews();
signals:
    void compositionChanged();
    void projectChanged();
    void sourceChanged();
    void selectionChanged();
    void positionChanged();
    void stateChanged();
    void frameChanged();
private:
    struct State { Project project; QVariantList selection; int primary=-1; int text=-1; int overlay=-1; int caption=-1; QVariantList captions; };
    State state() const;
    void apply(const State &state,int position,bool forceRebuild=false);
    bool commit(const Project &next,const QVariantList &selection,int primary,const QString &label,bool force=false,int text=-2,int overlay=-2,int caption=-2,const QVariantList &captions={});
    void rebuild();
    void rebuildSource();
    bool addImported(Project &next,const QStringList &paths,bool append,QStringList &problems);
    void refreshPendingProxy();
    void scheduleRecovery();
    void prioritizeProxy(int frame);
    double sourceDuration(const QString &path) const;
    void restoreAudioDurations(Project &project) const;
    void report(const QString &message);
    bool maybeSave();
    int splitCandidate() const;
    bool saveDialog(bool choosePath);
    const Project &visibleProject() const {return audioPreview_?*audioPreview_:project_;}
    void presentAudioEdit(const Project &next,const QString &label);
    int audioPlayableFrames(const Project &project,int lane) const;
    std::optional<Project> audioPreview_;
    int audioLane_=1,soloLane_=-1,soloClip_=-1,transitionIndex_=-1;
    double masterPeak_=-60;
    bool masterClipped_=false,audioGestureWasPlaying_=false;
    VisualTransform *selectedTransform(Project &project) const;
    void cancelPendingVisual();
    int transformSourceFrame() const;
    void applyComposition(const Project &project,const QString &label);
    Project project_;
    int selectedOverlay_=-1;
    bool transformMode_=false;
    std::optional<State> visualBefore_;
    QString visualNoticeBefore_;
    int selectedText_=-1;
    int selectedCaption_=-1,captionAnchor_=-1;
    QVariantList captionSelection_;
    bool textGuides_=true;
    TextOverlay *selectedTimedText(Project &project) const;
    void updateTextPreview();
    std::optional<State> textDragBefore_;
    QVariantList selection_;
    int primary_=-1,anchor_=-1,position_=0;
    QUndoStack undo_;
    QString path_,format_="Auto resolution · 60 fps",previewStatus_="No media",notice_;
    bool noticeCanUndo_=false;
    mutable QHash<QString,double> durations_;
    int selectedSource_=-1;
    bool sourceMode_=false,sourceViewDirty_=false,sourceScrubbing_=false;
    int sourceScrubFrame_=0;
    Mlt::Profile sourceProfile_;
    std::unique_ptr<Mlt::Producer> sourceProducer_;
    std::unique_ptr<Preview> sourcePreview_;
    Mlt::Profile profile_;
    std::unique_ptr<Mlt::Tractor> timeline_;
    std::unique_ptr<Preview> preview_;
    PreviewCache *proxyCache_=nullptr;
    MediaCache *mediaCache_=nullptr;
    QHash<int,QString> proxyPaths_;
    QHash<int,QString> overlayProxyPaths_;
    int proxyPriorityStart_=-1,proxyPriorityEnd_=-1;
    QPointer<ExportJob> exportJob_;
    RecoveryStore *recovery_=nullptr;
    QVariantList recoveryCandidates_,relinkProposals_;
    QString exportState_,exportDestination_,exportRevision_,exportDetails_,exportLog_;
    std::optional<Project> retryExportProject_;
    bool startExport(const Project &,const QString &,const QString &revision);
    QImage frame_;
    int exportProgress_=0,scrubPosition_=0;
    bool scrubbing_=false,skim_=false,resumeAfterScrub_=false,resumeWhenSettled_=false,loading_=false,proxyPending_=false;
    QTimer loadingTimer_;
    bool rebuilding_=false;
};
