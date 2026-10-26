#include "preview.h"
#include "mlt_engine.h"

#include <QElapsedTimer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include <atomic>
#include <future>

namespace {
void configure(Mlt::Profile &profile, int width = 640, int height = 480)
{
    Media::configureOutputProfile(profile, width, height);
}

// A real color producer still renders its pixels. This pass-through MLT filter
// controls when a decode finishes, so tests never depend on decoder timing.
class DecodeGate {
public:
    DecodeGate(Mlt::Producer &source)
        : raw_(mlt_filter_new()), filter_(raw_)
    {
        mlt_filter_close(raw_); // The C++ wrapper retains its own reference.
        raw_->child = this;
        raw_->process = [](mlt_filter filter, mlt_frame frame) {
            auto *gate = static_cast<DecodeGate *>(filter->child);
            gate->calls.fetch_add(1);
            if (gate->armed.exchange(false)) {
                gate->thread = QThread::currentThread();
                gate->entered.release();
                gate->release.tryAcquire(1, 10000);
            }
            return frame;
        };
        source.attach(filter_);
    }
    ~DecodeGate() { release.release(); }

    QSemaphore entered;
    QSemaphore release;
    std::atomic<bool> armed{true};
    std::atomic<int> calls{0};
    QThread *thread = nullptr;
private:
    mlt_filter raw_;
    Mlt::Filter filter_;
};

class QueuedDeliveryObserver : public QObject {
public:
    int deliveries = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::MetaCall) ++deliveries;
        return false;
    }
};
}

class PreviewAsyncTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("SDL_AUDIODRIVER", "dummy"); Media::initializeMlt(); }
    void requestReturnsWhileDecoderIsBlocked();
    void burstPublishesOnlyNewestFrame();
    void hoverPreservesCommittedPosition();
    void canceledDecodeNeverPublishes();
    void replacingProducerQuiescesDecoder();
    void destructionQuiescesDecoder();
    void asyncSizeFollowsProducerProfile();
    void synchronousSeekSupersedesPendingRequest();
    void pendingSeekResumesPlayback();
    void cancelCannotResumePlayback();
    void coalescedEditsSurviveReplacementSeek();
    void canceledPendingEditNeverRuns();
};

void PreviewAsyncTest::coalescedEditsSurviveReplacementSeek() {
    Mlt::Profile profile;configure(profile);Mlt::Producer source(profile,"color","red");source.set_in_and_out(0,119);
    Preview preview(profile);QVERIFY(preview.setProducer(&source));DecodeGate gate(source);
    std::atomic<int> edits{0};QThread *editThread=nullptr;QSignalSpy settled(&preview,&Preview::seekSettled);
    preview.requestSeek(10);QVERIFY(gate.entered.tryAcquire(1,3000));
    preview.requestSeek(20,true,[&]{edits+=1;});
    preview.requestSeek(40,true,[&]{edits+=10;editThread=QThread::currentThread();});
    preview.requestSeek(90);QCOMPARE(edits.load(),0);gate.release.release();
    QTRY_COMPARE_WITH_TIMEOUT(settled.size(),1,3000);QCOMPARE(settled.first()[0].toInt(),90);
    QCOMPARE(edits.load(),10);QVERIFY(editThread!=QThread::currentThread());preview.setProducer(nullptr);
}

void PreviewAsyncTest::canceledPendingEditNeverRuns() {
    Mlt::Profile profile;configure(profile);Mlt::Producer source(profile,"color","red");source.set_in_and_out(0,119);
    Preview preview(profile);QVERIFY(preview.setProducer(&source));DecodeGate gate(source);std::atomic<int> edits{0};
    preview.requestSeek(10);QVERIFY(gate.entered.tryAcquire(1,3000));
    preview.requestSeek(20,true,[&]{++edits;});preview.cancelPendingSeek();gate.release.release();
    preview.setProducer(nullptr);QCOMPARE(edits.load(),0);
}

void PreviewAsyncTest::requestReturnsWhileDecoderIsBlocked()
{
    Mlt::Profile profile; configure(profile);
    Mlt::Producer source(profile, "color", "red"); source.set_in_and_out(0, 119);
    Preview preview(profile); QVERIFY(preview.setProducer(&source));
    DecodeGate gate(source);
    QSignalSpy frames(&preview, &Preview::frameReady);
    QSignalSpy settled(&preview, &Preview::seekSettled);
    QElapsedTimer elapsed; elapsed.start();
    preview.requestSeek(42);
    const auto requestNs = elapsed.nsecsElapsed();
    QVERIFY2(gate.entered.tryAcquire(1, 3000), "Worker never began the requested frame");
    QVERIFY(gate.thread != QThread::currentThread());
    QCOMPARE(frames.count(), 0);
    QVERIFY(preview.isSeeking());
    bool heartbeat = false;
    QTimer::singleShot(0, &preview, [&] { heartbeat = true; });
    QTRY_VERIFY_WITH_TIMEOUT(heartbeat, 1000);
    QCOMPARE(frames.count(), 0);
    qInfo() << "requestSeek returned in" << requestNs / 1000.0 << "microseconds; GUI heartbeat ran during decode";
    gate.release.release();
    QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 1, 3000);
    QCOMPARE(settled.first().at(0).toInt(), 42);
    QVERIFY(!preview.isSeeking());
    QCOMPARE(frames.count(), 1);
    preview.setProducer(nullptr);
}

void PreviewAsyncTest::burstPublishesOnlyNewestFrame()
{
    Mlt::Profile profile; configure(profile);
    Mlt::Producer red(profile, "color", "red"), blue(profile, "color", "blue");
    Mlt::Playlist source(profile);
    source.append(red, 0, 59);
    source.append(blue, 0, 59);
    Preview preview(profile); QVERIFY(preview.setProducer(&source));
    DecodeGate gate(source);
    QSignalSpy frames(&preview, &Preview::frameReady);
    QSignalSpy settled(&preview, &Preview::seekSettled);
    preview.requestSeek(10);
    QVERIFY(gate.entered.tryAcquire(1, 3000));
    for (int frame = 11; frame <= 90; ++frame) preview.requestSeek(frame);
    QCOMPARE(preview.position(), 90);
    gate.release.release();
    QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 1, 3000);
    QCOMPARE(settled.first().at(0).toInt(), 90);
    QCOMPARE(settled.first().at(1).toBool(), true);
    QCOMPARE(frames.count(), 1);
    const auto color = qvariant_cast<QImage>(frames.first().at(0)).pixelColor(20, 20);
    QVERIFY(color.blue() > 200 && color.red() < 20);
    QCOMPARE(gate.calls.load(), 2); // The blocked frame and one coalesced latest frame.
    preview.setProducer(nullptr);
}

void PreviewAsyncTest::hoverPreservesCommittedPosition()
{
    Mlt::Profile profile; configure(profile);
    Mlt::Producer source(profile, "color", "red"); source.set_in_and_out(0, 119);
    Preview preview(profile); QVERIFY(preview.setProducer(&source));
    preview.seek(30);
    QSignalSpy positions(&preview, &Preview::positionChanged);
    QSignalSpy settled(&preview, &Preview::seekSettled);
    preview.requestSeek(80, false);
    QCOMPARE(preview.position(), 30);
    QCOMPARE(positions.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 1, 3000);
    QCOMPARE(preview.position(), 30);
    QCOMPARE(positions.count(), 0);
    QCOMPARE(settled.first().at(0).toInt(), 80);
    QCOMPARE(settled.first().at(1).toBool(), false);
    settled.clear();
    preview.requestSeek(200);
    QCOMPARE(preview.position(), 119);
    QCOMPARE(positions.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 1, 3000);
    QCOMPARE(settled.first().at(0).toInt(), 119);
    preview.setProducer(nullptr);
}

void PreviewAsyncTest::canceledDecodeNeverPublishes()
{
    Mlt::Profile profile; configure(profile);
    Mlt::Producer source(profile, "color", "red"); source.set_in_and_out(0, 119);
    Preview preview(profile); QVERIFY(preview.setProducer(&source));
    DecodeGate gate(source);
    QueuedDeliveryObserver delivery;
    preview.installEventFilter(&delivery);
    QSignalSpy frames(&preview, &Preview::frameReady);
    QSignalSpy settled(&preview, &Preview::seekSettled);
    preview.requestSeek(50);
    QVERIFY(gate.entered.tryAcquire(1, 3000));
    preview.cancelPendingSeek();
    QVERIFY(!preview.isSeeking());
    gate.release.release();
    QTRY_COMPARE_WITH_TIMEOUT(delivery.deliveries, 1, 3000);
    QCOMPARE(frames.count(), 0);
    QCOMPARE(settled.count(), 0);
    preview.setProducer(nullptr);
}

void PreviewAsyncTest::replacingProducerQuiescesDecoder()
{
    Mlt::Profile initial; configure(initial);
    Preview preview(initial);
    auto profile = std::make_unique<Mlt::Profile>(); configure(*profile, 720, 1280);
    auto source = std::make_unique<Mlt::Producer>(*profile, "color", "red"); source->set_in_and_out(0, 119);
    QVERIFY(preview.setProducer(source.get()));
    DecodeGate gate(*source);
    QSignalSpy frames(&preview, &Preview::frameReady);
    QSignalSpy settled(&preview, &Preview::seekSettled);
    preview.requestSeek(50);
    QVERIFY(gate.entered.tryAcquire(1, 3000));
    auto release = std::async(std::launch::async, [&] { gate.release.release(); });
    QVERIFY(preview.setProducer(nullptr));
    release.get();
    source.reset(); profile.reset();
    Mlt::Profile replacement; configure(replacement, 160, 90);
    Mlt::Producer blue(replacement, "color", "blue"); blue.set_in_and_out(0, 119);
    QVERIFY(preview.setProducer(&blue));
    QCoreApplication::sendPostedEvents();
    QCOMPARE(frames.count(), 1);
    QCOMPARE(qvariant_cast<QImage>(frames.first().at(0)).size(), QSize(160, 90));
    QCOMPARE(settled.count(), 0);
    preview.setProducer(nullptr);
}

void PreviewAsyncTest::destructionQuiescesDecoder()
{
    Mlt::Profile profile; configure(profile);
    Mlt::Producer source(profile, "color", "red"); source.set_in_and_out(0, 119);
    auto preview = std::make_unique<Preview>(profile); QVERIFY(preview->setProducer(&source));
    DecodeGate gate(source);
    QSignalSpy frames(preview.get(), &Preview::frameReady);
    QSignalSpy settled(preview.get(), &Preview::seekSettled);
    preview->requestSeek(50);
    QVERIFY(gate.entered.tryAcquire(1, 3000));
    auto release = std::async(std::launch::async, [&] { gate.release.release(); });
    preview.reset();
    release.get();
    QCoreApplication::sendPostedEvents();
    QCOMPARE(frames.count(), 0);
    QCOMPARE(settled.count(), 0);
}

void PreviewAsyncTest::asyncSizeFollowsProducerProfile()
{
    Mlt::Profile initial; configure(initial);
    Preview preview(initial);
    for (const auto &entry : QList<QPair<QSize, QSize>>{
             {{160, 90}, {160, 90}}, {{640, 480}, {640, 480}},
             {{4096, 2160}, {960, 506}}, {{720, 1280}, {304, 540}}}) {
        Mlt::Profile profile; configure(profile, entry.first.width(), entry.first.height());
        Mlt::Producer source(profile, "color", "red"); source.set_in_and_out(0, 119);
        QVERIFY(preview.setProducer(&source));
        QSignalSpy frames(&preview, &Preview::frameReady);
        QSignalSpy settled(&preview, &Preview::seekSettled);
        preview.requestSeek(60);
        QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 1, 3000);
        QCOMPARE(frames.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(frames.first().at(0)).size(), entry.second);
        preview.setProducer(nullptr);
    }
}

void PreviewAsyncTest::synchronousSeekSupersedesPendingRequest()
{
    Mlt::Profile profile; configure(profile);
    Mlt::Producer source(profile, "color", "red"); source.set_in_and_out(0, 119);
    Preview preview(profile); QVERIFY(preview.setProducer(&source));
    QSignalSpy frames(&preview, &Preview::frameReady);
    QSignalSpy settled(&preview, &Preview::seekSettled);
    preview.requestSeek(50);
    preview.seek(20);
    QCoreApplication::sendPostedEvents();
    QCOMPARE(preview.position(), 20);
    QCOMPARE(frames.count(), 1);
    QCOMPARE(settled.count(), 0);
    preview.setProducer(nullptr);
}

void PreviewAsyncTest::pendingSeekResumesPlayback()
{
    Mlt::Profile profile; configure(profile);
    Mlt::Producer source(profile, "color", "red"); source.set_in_and_out(0, 599);
    Preview preview(profile); QVERIFY(preview.setProducer(&source));
    preview.play(); QVERIFY(preview.isPlaying());
    QSignalSpy settled(&preview, &Preview::seekSettled);
    preview.requestSeek(120);
    QTRY_COMPARE_WITH_TIMEOUT(settled.count(), 1, 3000);
    QVERIFY(preview.isPlaying());
    preview.pause();
    preview.setProducer(nullptr);
}

void PreviewAsyncTest::cancelCannotResumePlayback()
{
    Mlt::Profile profile; configure(profile);
    Mlt::Producer source(profile, "color", "red"); source.set_in_and_out(0, 599);
    Preview preview(profile); QVERIFY(preview.setProducer(&source));
    preview.play(); QVERIFY(preview.isPlaying());
    preview.requestSeek(120);
    preview.cancelPendingSeek();
    preview.pause();
    QCoreApplication::sendPostedEvents();
    QVERIFY(!preview.isPlaying());
    QVERIFY(!preview.isSeeking());
    preview.setProducer(nullptr);
}

QTEST_GUILESS_MAIN(PreviewAsyncTest)
#include "preview_async_test.moc"
