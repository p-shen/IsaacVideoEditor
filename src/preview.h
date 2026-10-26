#pragma once

#include <QImage>
#include <QObject>
#include <QString>

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
    explicit Preview(Mlt::Profile &profile, QObject *parent = nullptr);
    ~Preview() override;

    bool setProducer(Mlt::Producer *producer);
    void play();
    void pause();
    void seek(int frame);
    // Still decoding runs on a worker. Hover seeks leave position() unchanged.
    // Edits run before decoding; synchronous transport changes finish any pending edit.
    void requestSeek(int frame, bool commitPosition = true, std::function<void()> edit = {});
    void cancelPendingSeek();

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

    struct SeekRequest {
        int frame;
        int restorePosition;
        bool committed;
        std::uint64_t generation;
    };

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
