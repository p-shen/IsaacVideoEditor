#pragma once

#include "performance.h"

#include <QImage>
#include <QCache>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <mlt++/Mlt.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

// Plays an MLT timeline with sound and sends video frames to the Qt interface.
// The caller owns the producer and must keep it alive until setProducer(nullptr)
// or this object is destroyed.
class Preview final : public QObject
{
    Q_OBJECT

public:
    explicit Preview(Mlt::Profile &profile, QObject *parent = nullptr, const Performance::Budget &budget = Performance::budget());
    ~Preview() override;

    bool setProducer(Mlt::Producer *producer);
    void play();
    void pause();
    void seek(int frame);
    // Still decoding runs on a worker. Hover seeks leave position() unchanged.
    // Edits run before decoding; synchronous transport changes finish any pending edit.
    void requestSeek(int frame, bool commitPosition = true, std::function<void()> edit = {});
    void cancelPendingSeek();
    // Call before changing filters outside requestSeek's edit callback.
    void invalidateFrames();

    bool isPlaying() const { return playing_; }
    bool isSeeking() const { return seeking_; }
    int position() const { return position_; }

signals:
    void audioPeak(double peak);
    void frameReady(const QImage &image);
    void positionChanged(int frame);
    void playbackChanged(bool playing);
    void seekSettled(int frame, bool committed);
    void seekingChanged(bool seeking);
    void errorOccurred(const QString &message);

private:
    static void frameShown(mlt_properties, void *context, mlt_event_data data);
    static void consumerStopped(mlt_properties, void *context, mlt_event_data);
    bool renderStill();
    void updateRenderProfile(mlt_profile profile);
    void stopConsumer();
    void publishFrame(mlt_frame frame, std::uint64_t generation);
    void seekLoop();
    void quiesceSeek(bool discardEdit = false);
    std::uint64_t frameRevision();
    QImage cachedFrame(int frame, std::uint64_t revision);
    void cacheFrame(int frame, const QImage &image, std::uint64_t revision);
    void refreshCacheBudget();
    std::uint64_t refreshSourceIdentity(std::uint64_t revision);

    struct SeekRequest {
        int frame;
        int restorePosition;
        bool committed;
        std::uint64_t generation;
        std::uint64_t frameRevision;
    };

    Performance::Budget budget_;
    std::mutex frameCacheMutex_;
    QCache<int,QImage> frameCache_;
    std::uint64_t frameRevision_ = 0;
    QElapsedTimer memoryCheck_;
    QStringList sourcePaths_;
    QByteArray sourceIdentity_;
    std::unique_ptr<Mlt::Consumer> consumer_;
    std::unique_ptr<Mlt::Event> frameEvent_;
    std::unique_ptr<Mlt::Event> stoppedEvent_;
    Mlt::Producer *producer_ = nullptr;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<bool> deliveryPending_{false};
    std::atomic<bool> playing_{false};
    int position_ = 0; // GUI thread only
    int renderWidth_ = 960;
    int renderHeight_ = 540;
    int producerLength_ = 0;
    bool seeking_ = false; // GUI thread only
    bool resumeAfterSeek_ = false;
    std::thread seekWorker_;
    std::mutex seekMutex_;
    std::condition_variable seekWake_;
    std::optional<SeekRequest> pendingSeek_;
    std::function<void()> pendingEdit_; // Guarded by seekMutex_; retained across superseded seeks.
    bool seekBusy_ = false; // Guarded by seekMutex_.
    bool stopSeekWorker_ = false;
};
