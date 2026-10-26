#pragma once

#include <QString>
#include <QVector>
#include <optional>
#include <QUuid>
#include "visual_transform.h"

struct AudioControls {
    double gainDb = 0;
    bool muted = false;
    int fadeInFrames = 0, fadeOutFrames = 0;
    bool operator==(const AudioControls &other) const;
};

struct Clip {
    QString path;
    double inSeconds = 0;
    double outSeconds = 0; // Exclusive end, on a 60 fps timeline.
    bool hardCutBefore = false;
    int rotationDegrees = 0;
    AudioControls audio;
    int transitionFrames = -1;
    QString transitionType = "dissolve";
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    VisualTransform transform;
    int sourceFrameOffset = 0; // Runtime proxy origin, never persisted.
};

struct MediaAsset {
    QString path;
    QString kind = "video";
    int durationFrames = 1;
    int cursorFrame = 0;
    int inFrame = 0;
    int outFrame = 1;
    bool operator==(const MediaAsset &other) const;
};

struct VisualOverlay {
    QString path;
    int sourceInFrame=0, durationFrames=300, startFrame=0, lane=0;
    QString hostId;
    int hostSourceFrame=0;
    VisualTransform transform;
    int sourceFrameOffset=0; // Runtime proxy origin, never persisted.
    bool operator==(const VisualOverlay &o) const;
};

struct TextOverlay {
    QString text = "New text";
    QString fontFamily = "Arial";
    QString style = "clean";
    QString color = "#ffffff";
    int fontSize = 84;
    bool bold = true, shadow = true, fade = false;
    int startFrame = 0, durationFrames = 300;
    double x = .5, y = .45;
    QString hostId;
    int hostSourceFrame = 0;
    bool timingNeedsReview = false;
    bool operator==(const TextOverlay &other) const;
};

struct Project {
    static constexpr int fps = 60;
    QVector<Clip> clips;
    QVector<MediaAsset> assets;
    QVector<TextOverlay> textOverlays;
    QVector<TextOverlay> captions;
    bool burnCaptions = true;
    int attachedFrame(const QString &hostId,int sourceFrame,int fallback) const;
    int textStartFrame(int index,bool caption=false) const;
    int textFrames(int index,bool caption=false) const;
    QVector<TextOverlay> renderedTexts() const;
    bool attachText(int index,bool caption,int hostIndex);
    bool setTextTiming(int index,bool caption,int start,int frames);
    bool detachText(int index,bool caption);
    void reviewCaptionTimingAfter(const Project &before);
    QVector<VisualOverlay> visualOverlays;
    QVector<bool> visualLaneVisible{true,true,true};
    int canvasWidth=0,canvasHeight=0; // Both zero preserves legacy auto resolution.
    int overlayStartFrame(int index) const;
    int overlayFrames(int index) const;
    void normalizeVisuals();
    bool detachOverlay(int index);
    bool removeClip(int index);
    int crossfadeFrames = 0;
    QString voicePath;
    int voiceDurationFrames = 0; // Last known source duration, retained while offline.
    double voiceStartSeconds = 0;
    QString musicPath;
    int musicDurationFrames = 0;
    double musicDb = -18;
    AudioControls voiceAudio;
    bool musicMuted = false, musicLoopToEnd = true;
    int musicFadeInFrames = 0, musicFadeOutFrames = 0;

    AudioControls audioControls(int lane, int clipIndex = -1) const;
    void setAudioControls(int lane, const AudioControls &controls, int clipIndex = -1);
    bool setTransition(int index, const QString &type, int frames);

    int clipFrames(const Clip &clip) const;
    int effectiveCrossfadeFrames() const;
    int transitionFramesBefore(int index) const;
    int clipStartFrame(int index) const;
    int durationFrames() const;
    bool splitClip(int index, int offsetFrames);
    int insertClipAtFrame(const Clip &clip, int frame);
    void deriveAssets();
    bool rotateClip(int index, int deltaDegrees);
    bool reorderClips(const QVector<int> &order);
    QString validate() const;
};

bool saveProject(const Project &project, const QString &path, QString *error = nullptr);
std::optional<Project> loadProject(const QString &path, QString *error = nullptr);
