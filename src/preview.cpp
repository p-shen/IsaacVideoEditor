#include "preview.h"
#include "visual_renderer.h"
#include "mlt_engine.h"

#include <QMetaObject>
#include <QtMath>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QDateTime>

#include <algorithm>
#include <limits>

namespace {
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

Preview::Preview(Mlt::Profile &profile, QObject *parent, const Performance::Budget &budget)
    : QObject(parent)
    , budget_(budget)
    , consumer_(std::make_unique<Mlt::Consumer>(profile, "sdl2_audio"))
{
    qualityLimit_ = QSize(budget.previewWidth, budget.previewHeight);
    refreshCacheBudget();
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
    if (consumer_ && consumer_->is_valid()) consumer_->disconnect_all_producers();
    producer_ = nullptr;
    producerLifetime_.reset();
    stoppedEvent_.reset();
    frameEvent_.reset();
}

bool Preview::setProducer(Mlt::Producer *producer, bool renderInitialStill)
{
    ++producerTicket_;
    quiesceSeek(true);
    stopConsumer();
    invalidateFrames();
    sourcePaths_.clear();
    if (playing_.exchange(false)) {
        emit playbackChanged(false);
    }
    position_ = 0;
    QString error;
    const bool connected = connectProducer(producer, error);
    producerLength_ = connected && producer ? producer->get_playtime() : 0;
    producerLifetime_.reset();
    {
        const std::lock_guard<std::mutex> lock(seekMutex_);
        retiringLifetime_.reset();
    }
    hasProducer_ = connected && producer;
    if (!connected) { emit errorOccurred(error); return false; }
    if (producer && renderInitialStill && !renderStill()) {
        producer_ = nullptr;
        hasProducer_ = false;
        return false;
    }
    return true;
}

bool Preview::connectProducer(Mlt::Producer *producer, QString &error)
{
    producer_ = nullptr;
    sourcePaths_.clear();
    if (consumer_ && consumer_->is_valid()) consumer_->disconnect_all_producers();
    if (!producer) return true;
    if (!consumer_ || !consumer_->is_valid()) {
        error = tr("Audio preview could not start (MLT sdl2_audio is unavailable).");
        return false;
    }
    if (!producer->is_valid()) {
        error = tr("Could not connect the timeline to the preview.");
        return false;
    }
    const auto profile = producer->get_profile();
    if (!profile || profile->width <= 0 || profile->height <= 0) {
        error = tr("The preview video profile is invalid.");
        return false;
    }
    updateRenderProfile(profile);
    if (consumer_->connect(*producer) != 0) {
        error = tr("Could not connect the timeline to the preview.");
        return false;
    }
    producer_ = producer;
    sourcePaths_=Media::sourceFiles(*producer);
    return true;
}

void Preview::updateRenderProfile(mlt_profile profile)
{
    const double scale = std::min({1.0, std::clamp(budget_.previewWidth,2,3840) / double(profile->width),
                                   std::clamp(budget_.previewHeight,2,2160) / double(profile->height)});
    renderWidth_ = std::max(1, qRound(profile->width * scale));
    renderHeight_ = std::max(1, qRound(profile->height * scale));
    // Consumer properties update its profile; never shrink the caller's source profile.
    outputProfile_.set_explicit(1);outputProfile_.set_width(renderWidth_);outputProfile_.set_height(renderHeight_);
    outputProfile_.set_frame_rate(profile->frame_rate_num,profile->frame_rate_den);
    outputProfile_.set_progressive(profile->progressive);outputProfile_.set_colorspace(profile->colorspace);
    outputProfile_.set_sample_aspect(profile->sample_aspect_num,profile->sample_aspect_den);
    outputProfile_.set_display_aspect(profile->display_aspect_num,profile->display_aspect_den);
    consumer_->set_profile(outputProfile_.get_profile());
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
    if (playing_ && consumerGeneration_ == generation_.load()) return;
    quiesceSeek();
    if (!producer_) {
        return;
    }
    haltConsumer();
    if (producer_->get_playtime() > 0 && position_ >= producer_->get_playtime() - 1) {
        position_ = 0;
    }
    generation_.fetch_add(1);
    producer_->seek(position_);
    producer_->set_speed(1.0);
    playing_ = true;
    consumerGeneration_ = generation_.load();
    if (consumer_->start() != 0) {
        playing_ = false;
        emit errorOccurred(tr("Could not start video and audio preview."));
        return;
    }
    emit playbackChanged(true);
}

void Preview::pause(bool renderPausedStill)
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
    if(renderPausedStill)renderStill();
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

void Preview::startSeekWorker()
{
    if (!seekWorker_.joinable()) seekWorker_ = std::thread(&Preview::seekLoop, this);
}

quint64 Preview::requestProducer(Mlt::Producer *producer, std::shared_ptr<void> lifetime,
                                std::function<void(bool)> settled)
{
    cancelPendingSeek();
    invalidateFrames();
    const bool wasPlaying = playing_.exchange(false);
    hasProducer_ = producer != nullptr;
    producerLength_ = producer ? producer->get_playtime() : 0;
    position_ = 0;
    const auto ticket = ++producerTicket_;
    std::function<void(bool)> superseded;
    {
        const std::lock_guard<std::mutex> lock(seekMutex_);
        if (pendingProducer_) superseded = std::move(pendingProducer_->settled);
        // A controller can transfer a previously borrowed producer into this
        // token on detach. Keep the first token until native disconnect ends.
        if (!producer && lifetime && !retiringLifetime_) retiringLifetime_ = lifetime;
        pendingProducer_ = ProducerRequest{producer, std::move(lifetime), std::move(settled), ticket};
        pendingTransport_.reset();
        startSeekWorker();
    }
    seekWake_.notify_one();
    if (wasPlaying) emit playbackChanged(false);
    if (superseded) QMetaObject::invokeMethod(this, [callback = std::move(superseded)] {
        callback(false);
    }, Qt::QueuedConnection);
    return ticket;
}

void Preview::requestPlay() { requestTransport(true, false); }

void Preview::requestPause(bool renderPausedStill) { requestTransport(false, renderPausedStill); }

void Preview::requestTransport(bool play, bool renderPausedStill)
{
    if (!hasProducer_ && play) return;
    const auto generation = generation_.fetch_add(1) + 1;
    const bool wasPlaying = playing_.exchange(play && hasProducer_);
    resumeAfterSeek_ = false;
    if (play && producerLength_ > 0 && position_ >= producerLength_ - 1) position_ = 0;
    {
        const std::lock_guard<std::mutex> lock(seekMutex_);
        pendingSeek_.reset();
        prefetch_.clear();
        pendingTransport_ = TransportRequest{play, position_, generation};
        startSeekWorker();
    }
    seekWake_.notify_one();
    if (seeking_) { seeking_ = false; emit seekingChanged(false); }
    if (wasPlaying != playing_.load()) emit playbackChanged(playing_);
    if (!play && renderPausedStill && hasProducer_) requestSeek(position_);
}

void Preview::requestSeek(int frame, bool commitPosition, std::function<void()> edit, int editKey)
{
    if (!hasProducer_) {
        return;
    }
    refreshCacheBudget();
    if(edit)invalidateFrames();
    const bool wasPlaying = playing_.exchange(false);
    resumeAfterSeek_ = resumeAfterSeek_ || wasPlaying;
    const auto generation = generation_.fetch_add(1) + 1;
    frame = std::clamp(frame, 0, std::max(0, producerLength_ - 1));
    if(lastRequestedFrame_>=0 && frame!=lastRequestedFrame_)seekDirection_=frame>lastRequestedFrame_?1:-1;
    lastRequestedFrame_=frame;
    if (commitPosition) {
        position_ = frame;
    }
    {
        const std::lock_guard<std::mutex> lock(seekMutex_);
        // A newer seek still needs the latest edit that has not reached the worker.
        if (edit) {
            if (editKey >= 0) pendingKeyedEdits_[editKey] = std::move(edit);
            else { pendingEdit_ = std::move(edit); pendingKeyedEdits_.clear(); }
        }
        pendingTransport_.reset();
        prefetch_.clear();
        pendingSeek_ = SeekRequest{frame, position_, commitPosition, generation, frameRevision(),false,seekDirection_};
        startSeekWorker();
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
    const auto generation = generation_.fetch_add(1) + 1;
    {
        const std::lock_guard<std::mutex> lock(seekMutex_);
        pendingSeek_.reset();
        prefetch_.clear();
        pendingEdit_ = {};
        pendingKeyedEdits_.clear();
        if (seeking_ && !playing_) {
            pendingTransport_ = TransportRequest{false, position_, generation};
            startSeekWorker();
        }
    }
    seekWake_.notify_one();
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
        if(!discardEdit)edit=takePendingEdits();
        pendingTransport_.reset();
    }
    cancelPendingSeek();
    std::unique_lock<std::mutex> lock(seekMutex_);
    pendingTransport_.reset();
    seekWake_.wait(lock, [this] { return !seekBusy_ && !pendingProducer_ && !pendingQuality_; });
    lock.unlock();
    // Apply filter settings only after the worker releases the producer.
    if(edit) { haltConsumer(); edit(); }
}

std::function<void()> Preview::takePendingEdits()
{
    auto full = std::move(pendingEdit_);
    auto keyed = std::move(pendingKeyedEdits_);
    pendingKeyedEdits_.clear();
    if (!full && keyed.empty()) return {};
    return [full = std::move(full), keyed = std::move(keyed)] {
        if (full) full();
        for (const auto &entry : keyed) entry.second();
    };
}

void Preview::seekLoop()
{
    for (;;) {
        std::optional<ProducerRequest> handoff;
        std::optional<TransportRequest> transport;
        std::optional<QSize> quality;
        std::optional<SeekRequest> seek;
        std::shared_ptr<void> retiring;
        {
            std::unique_lock<std::mutex> lock(seekMutex_);
            seekWake_.wait(lock, [this] {
                return stopSeekWorker_ || pendingProducer_ || pendingTransport_ || pendingQuality_ || pendingSeek_ || !prefetch_.empty();
            });
            if (stopSeekWorker_) return;
            if (pendingProducer_) {
                handoff = std::move(pendingProducer_); pendingProducer_.reset();
                retiring = std::move(retiringLifetime_);
            }
            else if (pendingQuality_) { quality = pendingQuality_; pendingQuality_.reset(); }
            else if (pendingTransport_) { transport = pendingTransport_; pendingTransport_.reset(); }
            else if (pendingSeek_) { seek = pendingSeek_; pendingSeek_.reset(); }
            else { seek = prefetch_.front(); prefetch_.pop_front(); }
            seekBusy_ = true;
        }
        if (handoff) {
            haltConsumer();
            QString error;
            const bool connected = connectProducer(handoff->producer, error);
            // Native callbacks and frame decoding have finished before owners
            // release profiles, producers, filters, and preview-copy leases.
            producerLifetime_ = connected && handoff->producer ? std::move(handoff->lifetime) : nullptr;
            retiring.reset();
            const auto ticket = handoff->ticket;
            QMetaObject::invokeMethod(this, [this, ticket, connected, error, callback = std::move(handoff->settled)] {
                const bool current = ticket == producerTicket_.load();
                if (current && !connected) {
                    hasProducer_ = false;
                    cancelPendingSeek();
                    requestPause(false);
                    emit errorOccurred(error);
                }
                if (callback) callback(current && connected);
            }, Qt::QueuedConnection);
        } else if (quality) {
            haltConsumer();
            budget_.previewWidth = quality->width(); budget_.previewHeight = quality->height();
            if (producer_) updateRenderProfile(producer_->get_profile());
        } else if (transport) {
            if (transport->generation == generation_.load()) {
                haltConsumer();
                std::function<void()> edit;
                {
                    const std::lock_guard<std::mutex> lock(seekMutex_);
                    if (transport->generation == generation_.load()) edit = takePendingEdits();
                }
                if (edit) edit();
                if (transport->generation == generation_.load() && producer_) {
                    producer_->seek(transport->position);
                    producer_->set_speed(transport->play ? 1.0 : 0.0);
                    if (transport->play) {
                        consumerGeneration_ = transport->generation;
                        if (consumer_->start() != 0) QMetaObject::invokeMethod(this, [this, generation = transport->generation] {
                            if (generation != generation_.load()) return;
                            if (playing_.exchange(false)) emit playbackChanged(false);
                            emit errorOccurred(tr("Could not start video and audio preview."));
                        }, Qt::QueuedConnection);
                    }
                }
            }
        } else if (seek) {
            auto request = *seek;
            QImage image;
            QString decodeError;
            if (request.generation == generation_.load()) {
                // stop() can wait for native decoder/audio work; it belongs on
                // this worker just as get_frame() does.
                haltConsumer();
                std::function<void()> edit;
                {
                    const std::lock_guard<std::mutex> lock(seekMutex_);
                    if (request.generation == generation_.load()) edit = takePendingEdits();
                }
                if (edit) edit();
                if (request.generation == generation_.load() && producer_) {
                    const auto revision = request.prefetch ? request.frameRevision : refreshSourceIdentity(request.frameRevision);
                    request.frameRevision = revision;
                    image = cachedFrame(request.frame, revision);
                    if (image.isNull()) {
                        const auto decodeStarted = std::chrono::steady_clock::now();
                        producer_->seek(request.frame);
                        std::unique_ptr<Mlt::Frame> frame(producer_->get_frame());
                        if (frame && frame->is_valid()) image = imageFromFrame(frame->get_frame(), renderWidth_, renderHeight_);
                        decodeMicros_ += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - decodeStarted).count();
                        if (request.prefetch && !image.isNull()) ++preloadedFrames_;
                    }
                    decodeError = VisualRendering::renderError(*producer_);
                    if (decodeError.isEmpty()) cacheFrame(request.frame, image, revision);
                    else {
                        image = {};
                        // This request carries its error to the GUI. Later
                        // preview frames must not inherit a stale failure.
                        VisualRendering::discardSpeculativeError(*producer_, decodeError);
                    }
                    producer_->seek(request.restorePosition);
                }
            }
            if (!request.prefetch) QMetaObject::invokeMethod(this, [this, request, image, decodeError] {
                if (request.generation != generation_.load() || !hasProducer_) return;
                const bool resume = resumeAfterSeek_;
                ++seekCount_; lastSeekMicros_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - request.queuedAt).count();
                resumeAfterSeek_ = false;
                seeking_ = false;
                emit seekingChanged(false);
                if (request.generation != generation_.load()) return;
                if (image.isNull()) emit errorOccurred(decodeError.isEmpty() ? tr("Could not decode this preview frame.") : decodeError);
                else {
                    emit frameReady(image);
                    ++deliveredFrames_;
                    if (request.generation == generation_.load()) emit seekSettled(request.frame, request.committed);
                }
                if (resume && request.generation == generation_.load()) requestPlay();
                else if (!image.isNull() && request.generation == generation_.load() && !playing_) queuePrefetch(request);
            }, Qt::QueuedConnection);
        }
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
    haltConsumer();
}

void Preview::haltConsumer()
{
    if (consumer_ && consumer_->is_valid() && !consumer_->is_stopped()) {
        consumer_->stop();
    }
}

bool Preview::renderStill()
{
    if (!producer_) {
        return false;
    }
    refreshCacheBudget();
    const auto revision=refreshSourceIdentity(frameRevision());
    auto image=cachedFrame(position_,revision);
    if(image.isNull()){
        producer_->seek(position_);
        std::unique_ptr<Mlt::Frame> frame(producer_->get_frame());
        if (!frame || !frame->is_valid()) {
            emit errorOccurred(tr("Could not render this preview frame."));
            return false;
        }
        image=imageFromFrame(frame->get_frame(),renderWidth_,renderHeight_);
        producer_->seek(position_); // get_frame advances the producer.
    }
    const auto error=VisualRendering::renderError(*producer_);
    if(!error.isEmpty()){emit errorOccurred(error);return false;}
    cacheFrame(position_,image,revision);
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
    if (!self || !self->playing_.load())return;
    ++self->receivedFrames_;
    if (self->deliveryPending_.exchange(true)) {
        ++self->skippedFrames_;
        return;
    }
    self->publishFrame(mlt_event_data_to_frame(data), self->consumerGeneration_.load());
}

void Preview::publishFrame(mlt_frame frame, std::uint64_t generation)
{
    const auto revision=frameRevision();
    const auto image = imageFromFrame(frame, renderWidth_, renderHeight_);
    double peak=0;
    if(frame){const auto props=MLT_FRAME_PROPERTIES(frame);
        peak=std::max(mlt_properties_get_double(props,"meta.media.audio_level.0"),mlt_properties_get_double(props,"meta.media.audio_level.1"));}
    const int framePosition = frame ? mlt_frame_get_position(frame) : 0;
    const auto error=producer_?VisualRendering::renderError(*producer_):QString{};
    if(!error.isEmpty()){
        VisualRendering::discardSpeculativeError(*producer_, error);
        QMetaObject::invokeMethod(this,[this,generation,error]{
            deliveryPending_=false;
            if(generation!=generation_.load())return;
            requestPause(false);
            emit errorOccurred(error);
        },Qt::QueuedConnection);
        return;
    }
    if (image.isNull()) {
        deliveryPending_ = false;
        return;
    }
    if(generation==generation_.load())cacheFrame(framePosition,image,revision);
    QMetaObject::invokeMethod(this, [this, image, framePosition, generation, peak] {
        deliveryPending_ = false;
        if (generation != generation_.load() || !playing_) {
            return;
        }
        refreshCacheBudget();
        position_ = framePosition;
        ++deliveredFrames_;
        emit frameReady(image);
        emit audioPeak(peak);
        emit positionChanged(position_);
        if (generation == generation_.load() && producerLength_ > 0 &&
            position_ >= producerLength_ - 1) {
            requestPause(false);
        }
    }, Qt::QueuedConnection);
}

void Preview::invalidateFrames(){
    const std::lock_guard<std::mutex> lock(frameCacheMutex_);
    ++frameRevision_;frameCache_.clear();
}

std::uint64_t Preview::frameRevision(){
    const std::lock_guard<std::mutex> lock(frameCacheMutex_);
    return frameRevision_;
}

QImage Preview::cachedFrame(int frame,std::uint64_t revision){
    const std::lock_guard<std::mutex> lock(frameCacheMutex_);
    if(revision!=frameRevision_)return {};
    const auto *image=frameCache_.object(frame);
    if(image)++cacheHits_;else ++cacheMisses_;
    return image?*image:QImage{};
}

void Preview::queuePrefetch(const SeekRequest &request) {
    if(budget_.prefetchFrames<=0 || request.generation!=generation_.load() || playing_)return;
    const qint64 frameKiB=(qint64(renderWidth_)*renderHeight_*4+1023)/1024;
    int count=0;
    {const std::lock_guard<std::mutex> lock(frameCacheMutex_);
        if(frameKiB>0)count=int(std::clamp(qint64(frameCache_.maxCost())/frameKiB-2,qint64(0),qint64(budget_.prefetchFrames)));}
    {const std::lock_guard<std::mutex> lock(seekMutex_);
        if(pendingSeek_ || request.generation!=generation_.load())return;
        prefetch_.clear();
        for(int i=1;i<=count;++i){const int frame=request.frame+i*request.direction;if(frame<0||frame>=producerLength_)break;
            auto next=request;next.frame=frame;next.prefetch=true;prefetch_.push_back(next);}}
    seekWake_.notify_one();
}

void Preview::setQualityLimit(const QSize &size) {
    const QSize bounded(std::clamp(size.width(),2,3840),std::clamp(size.height(),2,2160));
    if(qualityLimit_ == bounded)return;
    qualityLimit_ = bounded;
    invalidateFrames();
    {
        const std::lock_guard<std::mutex> lock(seekMutex_);
        pendingQuality_ = bounded;
        startSeekWorker();
    }
    if(hasProducer_)requestSeek(position_);
    seekWake_.notify_one();
}

QVariantMap Preview::diagnostics() const {
    const std::lock_guard<std::mutex> lock(frameCacheMutex_);
    return {{"cacheHits",qulonglong(cacheHits_)},{"cacheMisses",qulonglong(cacheMisses_)},
        {"preloadedFrames",qulonglong(preloadedFrames_)},{"receivedFrames",qulonglong(receivedFrames_)},
        {"deliveredFrames",qulonglong(deliveredFrames_)},{"skippedFrames",qulonglong(skippedFrames_)},
        {"seekCount",qulonglong(seekCount_)},{"lastSeekMs",lastSeekMicros_.load()/1000.0},
        {"decodeMs",decodeMicros_.load()/1000.0},{"cachedBytes",qint64(frameCache_.totalCost())*1024},
        {"cacheLimitBytes",qint64(frameCache_.maxCost())*1024},{"width",renderWidth_.load()},{"height",renderHeight_.load()}};
}

void Preview::cacheFrame(int frame,const QImage &image,std::uint64_t revision){
    if(image.isNull())return;
    const auto cost=(image.sizeInBytes()+1023)/1024;
    const std::lock_guard<std::mutex> lock(frameCacheMutex_);
    if(revision==frameRevision_&&cost<=frameCache_.maxCost())
        frameCache_.insert(frame,new QImage(image),int(cost));
}

void Preview::refreshCacheBudget(){
    if(memoryCheck_.isValid()&&memoryCheck_.elapsed()<1000)return;
    memoryCheck_.start();
    auto bytes=std::max(qsizetype(0),budget_.frameCacheBytes);
    const auto available=Performance::resources().availableMemoryBytes;
    if(available)bytes=std::min(bytes,qsizetype(std::min(available/16,quint64(std::numeric_limits<qsizetype>::max()))));
    const std::lock_guard<std::mutex> lock(frameCacheMutex_);
    frameCache_.setMaxCost(int(std::min(bytes/1024,qsizetype(std::numeric_limits<int>::max()))));
}

std::uint64_t Preview::refreshSourceIdentity(std::uint64_t revision){
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(const auto &path:sourcePaths_){
        const QFileInfo file(path);
        const auto canonical=file.canonicalFilePath();
        hash.addData((canonical.isEmpty()?file.absoluteFilePath():canonical).toUtf8());hash.addData(QByteArray(1,'\0'));
        hash.addData(QByteArray::number(file.size()));hash.addData(QByteArray(1,'\0'));
        hash.addData(QByteArray::number(file.lastModified().toMSecsSinceEpoch()));hash.addData(QByteArray(1,'\0'));
    }
    const auto identity=hash.result();
    const std::lock_guard<std::mutex> lock(frameCacheMutex_);
    if(revision!=frameRevision_)return revision;
    if(identity!=sourceIdentity_){sourceIdentity_=identity;frameCache_.clear();++frameRevision_;}
    return frameRevision_;
}

void Preview::consumerStopped(mlt_properties, void *context, mlt_event_data)
{
    auto *self = static_cast<Preview *>(context);
    if (!self || !self->playing_) {
        return;
    }
    const auto generation = self->consumerGeneration_.load();
    QMetaObject::invokeMethod(self, [self, generation] {
        if (generation != self->generation_.load() || !self->playing_.exchange(false)) {
            return;
        }
        emit self->playbackChanged(false);
    }, Qt::QueuedConnection);
}
