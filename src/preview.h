#pragma once

#include "performance.h"

#include <QImage>
#include <QCache>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QSize>

#include <mlt++/Mlt.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <deque>
#include <chrono>
#include <map>

// Plays an MLT timeline with sound and sends video frames to the Qt interface.
// Borrowed producers must outlive a synchronous detach. Async handoffs retain
// their lifetime token until the native consumer and decoder release them.
class Preview final : public QObject
{
    Q_OBJECT

public:
    explicit Preview(Mlt::Profile &profile, QObject *parent = nullptr, const Performance::Budget &budget = Performance::budget());
    ~Preview() override;

    bool setProducer(Mlt::Producer *producer, bool renderInitialStill = true);
    void play();
    void pause(bool renderPausedStill = true);
    void seek(int frame);
    void requestPlay();
    void requestPause(bool renderPausedStill = true);
    // Completion runs on the GUI thread; false also means a superseded handoff.
    // A null producer's token may own the retiring, previously borrowed producer.
    quint64 requestProducer(Mlt::Producer *producer, std::shared_ptr<void> lifetime = {},
                            std::function<void(bool)> settled = {});
    // Still decoding runs on a worker. Hover seeks leave position() unchanged.
    // Edits run before decoding; synchronous transport changes finish any pending edit.
    // A nonnegative key coalesces one incremental text edit after the latest full edit.
    void requestSeek(int frame, bool commitPosition = true, std::function<void()> edit = {}, int editKey = -1);
    void cancelPendingSeek();
    // Call before changing filters outside requestSeek's edit callback.
    void invalidateFrames();
    void setQualityLimit(const QSize &size);
    QVariantMap diagnostics() const;

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
    void haltConsumer();
    bool connectProducer(Mlt::Producer *producer, QString &error);
    void startSeekWorker(); // seekMutex_ must be held.
    void requestTransport(bool play, bool renderPausedStill);
    std::function<void()> takePendingEdits(); // seekMutex_ must be held.
    void publishFrame(mlt_frame frame, std::uint64_t generation);
    void seekLoop();
    void quiesceSeek(bool discardEdit = false);
    std::uint64_t frameRevision();
    QImage cachedFrame(int frame, std::uint64_t revision);
    void cacheFrame(int frame, const QImage &image, std::uint64_t revision);
    void refreshCacheBudget();
    std::uint64_t refreshSourceIdentity(std::uint64_t revision);
    struct SeekRequest;
    void queuePrefetch(const SeekRequest &request);

    struct SeekRequest {
        int frame;
        int restorePosition;
        bool committed;
        std::uint64_t generation;
        std::uint64_t frameRevision;
        bool prefetch=false;
        int direction=1;
        std::chrono::steady_clock::time_point queuedAt=std::chrono::steady_clock::now();
    };
    struct ProducerRequest {
        Mlt::Producer *producer;
        std::shared_ptr<void> lifetime;
        std::function<void(bool)> settled;
        quint64 ticket;
    };
    struct TransportRequest {
        bool play;
        int position;
        std::uint64_t generation;
    };

    Performance::Budget budget_;
    mutable std::mutex frameCacheMutex_;
    QCache<int,QImage> frameCache_;
    std::uint64_t frameRevision_ = 0;
    QElapsedTimer memoryCheck_;
    QStringList sourcePaths_;
    QByteArray sourceIdentity_;
    Mlt::Profile outputProfile_;
    std::unique_ptr<Mlt::Consumer> consumer_;
    std::unique_ptr<Mlt::Event> frameEvent_;
    std::unique_ptr<Mlt::Event> stoppedEvent_;
    Mlt::Producer *producer_ = nullptr;
    bool hasProducer_ = false; // GUI thread: includes a queued producer handoff.
    std::shared_ptr<void> producerLifetime_; // Worker, released after native disconnect.
    std::shared_ptr<void> retiringLifetime_; // seekMutex_; protects a borrowed producer during async detach.
    std::atomic<quint64> producerTicket_{0};
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> consumerGeneration_{0};
    std::atomic<bool> deliveryPending_{false};
    std::atomic<bool> playing_{false};
    int position_ = 0; // GUI thread only
    std::atomic<int> renderWidth_{960};
    std::atomic<int> renderHeight_{540};
    QSize qualityLimit_;
    int producerLength_ = 0;
    bool seeking_ = false; // GUI thread only
    bool resumeAfterSeek_ = false;
    std::thread seekWorker_;
    std::mutex seekMutex_;
    std::condition_variable seekWake_;
    std::optional<SeekRequest> pendingSeek_;
    std::optional<ProducerRequest> pendingProducer_;
    std::optional<TransportRequest> pendingTransport_;
    std::optional<QSize> pendingQuality_;
    std::deque<SeekRequest> prefetch_;
    int lastRequestedFrame_=-1, seekDirection_=1;
    std::atomic<quint64> cacheHits_{0},cacheMisses_{0},preloadedFrames_{0};
    std::atomic<quint64> deliveredFrames_{0},skippedFrames_{0},receivedFrames_{0},seekCount_{0},decodeMicros_{0};
    std::atomic<qint64> lastSeekMicros_{0};
    std::function<void()> pendingEdit_; // Guarded by seekMutex_; retained across superseded seeks.
    std::map<int, std::function<void()>> pendingKeyedEdits_; // Latest incremental edit for each text.
    bool seekBusy_ = false; // Guarded by seekMutex_.
    bool stopSeekWorker_ = false;
};
