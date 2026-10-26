#include "preview.h"
#include "mlt_engine.h"
#include <QImage>
#include <QSignalSpy>
#include <QtTest>

namespace {
void configure(Mlt::Profile &profile, int width, int height) {
    profile.set_explicit(1);
    profile.set_width(width);
    profile.set_height(height);
    profile.set_frame_rate(60, 1);
    profile.set_progressive(1);
    profile.set_sample_aspect(1, 1);
    profile.set_display_aspect(width, height);
    profile.set_colorspace(709);
}
}

class PreviewProfileTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("SDL_AUDIODRIVER", "dummy"); Media::initializeMlt(); }
    void stillSizeFollowsProducerProfile();
    void playbackUsesChangedAspect();
};

void PreviewProfileTest::stillSizeFollowsProducerProfile() {
    Mlt::Profile initial;
    configure(initial, 1920, 1080);
    Preview preview(initial);
    QSignalSpy frames(&preview, &Preview::frameReady);
    const QList<QPair<QSize, QSize>> cases = {
        {{160, 90}, {160, 90}}, {{640, 480}, {640, 480}},
        {{1920, 1080}, {960, 540}}, {{4096, 2160}, {960, 506}},
        {{720, 1280}, {304, 540}}
    };
    for (const auto &entry : cases) {
        preview.setProducer(nullptr);
        Mlt::Profile profile;
        configure(profile, entry.first.width(), entry.first.height());
        Mlt::Producer source(profile, "color", "red");
        source.set_in_and_out(0, 119);
        frames.clear();
        QVERIFY(preview.setProducer(&source));
        QVERIFY(!frames.isEmpty());
        QCOMPARE(qvariant_cast<QImage>(frames.last().at(0)).size(), entry.second);
        preview.setProducer(nullptr);
    }
}

void PreviewProfileTest::playbackUsesChangedAspect() {
    Mlt::Profile initial;
    configure(initial, 1920, 1080);
    Preview preview(initial);
    Mlt::Profile changed;
    configure(changed, 640, 480);
    Mlt::Producer source(changed, "color", "blue");
    source.set_in_and_out(0, 119);
    QSignalSpy frames(&preview, &Preview::frameReady);
    QVERIFY(preview.setProducer(&source));
    frames.clear();
    preview.play();
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 3, 3000);
    preview.pause();
    for (const auto &frame : frames) QCOMPARE(qvariant_cast<QImage>(frame.at(0)).size(), QSize(640, 480));
    preview.setProducer(nullptr);
}

QTEST_GUILESS_MAIN(PreviewProfileTest)
#include "preview_profile_test.moc"
