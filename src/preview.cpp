#include "preview.h"

#include <QMetaObject>
#include <QtMath>

#include <algorithm>

namespace {
constexpr int previewWidth = 960;
constexpr int previewHeight = 540;

QImage imageFromFrame(mlt_frame frame, int width, int height)
{
    if (!frame) {
        return {};
    }
    auto format = mlt_image_rgba;
    std::uint8_t *pixels = nullptr;
    if (mlt_frame_get_image(frame, &pixels, &format, &width, &height, 0) != 0 ||
        !pixels || width <= 0 || height <= 0 || format != mlt_image_rgba) {
        return {};
    }
    // MLT owns pixels and releases them with the frame. Qt needs its own copy.
    return QImage(pixels, width, height, width * 4, QImage::Format_RGBA8888).copy();
}
} // namespace

Preview::Preview(Mlt::Profile &profile, QObject *parent)
    : QObject(parent)
    , consumer_(std::make_unique<Mlt::Consumer>(profile, "sdl2_audio"))
{
    if (!consumer_->is_valid()) {
        return;
    }
    consumer_->set("real_time", 1);
    updateRenderProfile(profile.get_profile());
    consumer_->set("rescale", "bilinear");
    consumer_->set("mlt_image_format", "rgba");
    frameEvent_.reset(consumer_->listen("consumer-frame-show", this, &Preview::frameShown));
    stoppedEvent_.reset(consumer_->listen("consumer-stopped", this, &Preview::consumerStopped));
}

Preview::~Preview()
{
    cancelPendingSeek();
    {
        const std::lock_guard<std::mutex> lock(seekMutex_);
        stopSeekWorker_ = true;
    }
    seekWake_.notify_one();
    if (seekWorker_.joinable()) {
        seekWorker_.join();
    }
    stopConsumer();
    stoppedEvent_.reset();
    frameEvent_.reset();
}

bool Preview::setProducer(Mlt::Producer *producer)
{
    quiesceSeek(true);
    stopConsumer();
    if (playing_.exchange(false)) {
        emit playbackChanged(false);
    }
    producer_ = nullptr;
    producerLength_ = 0;
    if (consumer_ && consumer_->is_valid()) {
        consumer_->disconnect_all_producers();
    }
    position_ = 0;
    if (!producer) {
        return true;
    }
    if (!consumer_ || !consumer_->is_valid()) {
        emit errorOccurred(tr("Audio preview could not start (MLT sdl2_audio is unavailable)."));
        return false;
    }
    if (!producer->is_valid()) {
        emit errorOccurred(tr("Could not connect the timeline to the preview."));
        return false;
    }
    const auto profile = producer->get_profile();
    if (!profile || profile->width <= 0 || profile->height <= 0) {
        emit errorOccurred(tr("The preview video profile is invalid."));
        return false;
    }
    updateRenderProfile(profile);
    if (consumer_->connect(*producer) != 0) {
        emit errorOccurred(tr("Could not connect the timeline to the preview."));
        return false;
    }
    producer_ = producer;
    producerLength_ = producer->get_playtime();
    if (!renderStill()) {
        producer_ = nullptr;
        return false;
    }
    return true;
}

void Preview::updateRenderProfile(mlt_profile profile)
{
    const double scale = std::min({1.0, previewWidth / double(profile->width),
                                   previewHeight / double(profile->height)});
    renderWidth_ = std::max(1, qRound(profile->width * scale));
    renderHeight_ = std::max(1, qRound(profile->height * scale));
    consumer_->set_profile(profile);
    // Consumer properties override its profile and retain constructor values.
    consumer_->set("width", renderWidth_);
    consumer_->set("height", renderHeight_);
    consumer_->set("frame_rate_num", profile->frame_rate_num);
    consumer_->set("frame_rate_den", profile->frame_rate_den);
    consumer_->set("fps", mlt_profile_fps(profile));
    consumer_->set("progressive", profile->progressive);
    consumer_->set("sample_aspect_num", profile->sample_aspect_num);
    consumer_->set("sample_aspect_den", profile->sample_aspect_den);
    consumer_->set("aspect_ratio", mlt_profile_sar(profile));
    consumer_->set("display_aspect_num", profile->display_aspect_num);
    consumer_->set("display_aspect_den", profile->display_aspect_den);
    consumer_->set("display_ratio", mlt_profile_dar(profile));
}

void Preview::play()
{
    quiesceSeek();
    if (!producer_ || playing_) {
        return;
    }
    if (producer_->get_playtime() > 0 && position_ >= producer_->get_playtime() - 1) {
        position_ = 0;
    }
    generation_.fetch_add(1);
    producer_->seek(position_);
    producer_->set_speed(1.0);
    playing_ = true;
    if (consumer_->start() != 0) {
        playing_ = false;
        emit errorOccurred(tr("Could not start video and audio preview."));
        return;
    }
    emit playbackChanged(true);
}

void Preview::pause()
{
    quiesceSeek();
    if (!producer_) {
        return;
    }
    const bool wasPlaying = playing_.exchange(false);
    stopConsumer();
    if (wasPlaying) {
        emit playbackChanged(false);
    }
    renderStill();
}

void Preview::seek(int frame)
{
    quiesceSeek();
    if (!producer_) {
        return;
    }
    const bool resume = playing_.exchange(false);
    stopConsumer();
    position_ = std::clamp(frame, 0, std::max(0, producer_->get_playtime() - 1));
    renderStill();
    if (resume) {
        play();
    }
}

void Preview::requestSeek(int frame, bool commitPosition, std::function<void()> edit)
{
    if (!producer_) {
        return;
    }
    const bool wasPlaying = playing_.exchange(false);
    resumeAfterSeek_ = resumeAfterSeek_ || wasPlaying;
    // The consumer must release the producer before the still worker uses it.
    stopConsumer();
    const auto generation = generation_.load();
    frame = std::clamp(frame, 0, std::max(0, producerLength_ - 1));
    if (commitPosition) {
        position_ = frame;
    }
    {
        const std::lock_guard<std::mutex> lock(seekMutex_);
        // A newer seek still needs the latest edit that has not reached the worker.
        if (edit) pendingEdit_ = std::move(edit);
        pendingSeek_ = SeekRequest{frame, position_, commitPosition, generation};
        if (!seekWorker_.joinable()) {
            seekWorker_ = std::thread(&Preview::seekLoop, this);
        }
    }
    seekWake_.notify_one();
    const bool wasSeeking = seeking_;
    seeking_ = true;
    if (wasPlaying) {
        emit playbackChanged(false);
    }
    if (!wasSeeking && generation == generation_.load()) {
        emit seekingChanged(true);
    }
    if (commitPosition && generation == generation_.load()) {
        emit positionChanged(position_);
    }
}

void Preview::cancelPendingSeek()
{
    generation_.fetch_add(1);
    {
        const std::lock_guard<std::mutex> lock(seekMutex_);
        pendingSeek_.reset();
        pendingEdit_ = {};
    }
    resumeAfterSeek_ = false;
    if (seeking_) {
        seeking_ = false;
        emit seekingChanged(false);
    }
}

void Preview::quiesceSeek(bool discardEdit)
{
    std::function<void()> edit;
    {
        const std::lock_guard<std::mutex> lock(seekMutex_);
        if(!discardEdit)edit=std::move(pendingEdit_);
    }
    cancelPendingSeek();
    std::unique_lock<std::mutex> lock(seekMutex_);
    seekWake_.wait(lock, [this] { return !seekBusy_; });
    lock.unlock();
    // Apply filter settings only after the worker releases the producer.
    if(edit)edit();
}

void Preview::seekLoop()
{
    for (;;) {
        SeekRequest request;
        {
            std::unique_lock<std::mutex> lock(seekMutex_);
            seekWake_.wait(lock, [this] { return stopSeekWorker_ || pendingSeek_.has_value(); });
            if (stopSeekWorker_) {
                return;
            }
            request = *pendingSeek_;
            pendingSeek_.reset();
            seekBusy_ = true;
        }
        QImage image;
        if (request.generation == generation_.load()) {
            std::function<void()> edit;
            {
                const std::lock_guard<std::mutex> lock(seekMutex_);
                if(request.generation==generation_.load())edit=std::move(pendingEdit_);
            }
            if (edit) edit();
            producer_->seek(request.frame);
            {
                std::unique_ptr<Mlt::Frame> frame(producer_->get_frame());
                if (frame && frame->is_valid()) {
                    image = imageFromFrame(frame->get_frame(), renderWidth_, renderHeight_);
                }
            }
            producer_->seek(request.restorePosition);
        }
        QMetaObject::invokeMethod(this, [this, request, image] {
            if (request.generation != generation_.load() || !producer_) {
                return;
            }
            const bool resume = resumeAfterSeek_;
            resumeAfterSeek_ = false;
            seeking_ = false;
            emit seekingChanged(false);
            if (request.generation != generation_.load()) {
                return;
            }
            if (image.isNull()) {
                emit errorOccurred(tr("Could not decode this preview frame."));
            } else {
                emit frameReady(image);
                if (request.generation == generation_.load()) {
                    emit seekSettled(request.frame, request.committed);
                }
            }
            if (resume && request.generation == generation_.load()) {
                play();
            }
        }, Qt::QueuedConnection);
        {
            const std::lock_guard<std::mutex> lock(seekMutex_);
            seekBusy_ = false;
        }
        seekWake_.notify_one();
    }
}

void Preview::stopConsumer()
{
    generation_.fetch_add(1);
    if (consumer_ && consumer_->is_valid() && !consumer_->is_stopped()) {
        consumer_->stop();
    }
}

bool Preview::renderStill()
{
    if (!producer_) {
        return false;
    }
    producer_->seek(position_);
    std::unique_ptr<Mlt::Frame> frame(producer_->get_frame());
    if (!frame || !frame->is_valid()) {
        emit errorOccurred(tr("Could not render this preview frame."));
        return false;
    }
    const auto image = imageFromFrame(frame->get_frame(), renderWidth_, renderHeight_);
    producer_->seek(position_); // get_frame advances the producer.
    if (image.isNull()) {
        emit errorOccurred(tr("Could not decode this preview frame."));
        return false;
    }
    emit frameReady(image);
    emit positionChanged(position_);
    return true;
}

void Preview::frameShown(mlt_properties, void *context, mlt_event_data data)
{
    auto *self = static_cast<Preview *>(context);
    if (!self || !self->playing_.load() || self->deliveryPending_.exchange(true)) {
        return;
    }
    self->publishFrame(mlt_event_data_to_frame(data), self->generation_.load());
}

void Preview::publishFrame(mlt_frame frame, std::uint64_t generation)
{
    const auto image = imageFromFrame(frame, renderWidth_, renderHeight_);
    double peak=0;
    if(frame){const auto props=MLT_FRAME_PROPERTIES(frame);
        peak=std::max(mlt_properties_get_double(props,"meta.media.audio_level.0"),mlt_properties_get_double(props,"meta.media.audio_level.1"));}
    const int framePosition = frame ? mlt_frame_get_position(frame) : 0;
    if (image.isNull()) {
        deliveryPending_ = false;
        return;
    }
    QMetaObject::invokeMethod(this, [this, image, framePosition, generation, peak] {
        deliveryPending_ = false;
        if (generation != generation_.load() || !playing_) {
            return;
        }
        position_ = framePosition;
        emit frameReady(image);
        emit audioPeak(peak);
        emit positionChanged(position_);
        if (producer_ && producer_->get_playtime() > 0 &&
            position_ >= producer_->get_playtime() - 1) {
            pause();
        }
    }, Qt::QueuedConnection);
}

void Preview::consumerStopped(mlt_properties, void *context, mlt_event_data)
{
    auto *self = static_cast<Preview *>(context);
    if (!self || !self->playing_) {
        return;
    }
    const auto generation = self->generation_.load();
    QMetaObject::invokeMethod(self, [self, generation] {
        if (generation != self->generation_.load() || !self->playing_.exchange(false)) {
            return;
        }
        emit self->playbackChanged(false);
    }, Qt::QueuedConnection);
}
