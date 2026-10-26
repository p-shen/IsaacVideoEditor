#include "mlt_engine.h"

#include <QImage>
#include <QtTest>
#include <algorithm>
#include <array>

namespace {
QImage imageAt(Mlt::Tractor &timeline, int position) {
    timeline.seek(position);
    std::unique_ptr<Mlt::Frame> frame(timeline.get_frame());
    if (!frame || !frame->is_valid()) return {};
    auto format = mlt_image_rgba;
    int width = 160, height = 90;
    std::uint8_t *pixels = nullptr;
    if (mlt_frame_get_image(frame->get_frame(), &pixels, &format, &width, &height, 0) != 0 ||
        !pixels || format != mlt_image_rgba) return {};
    return QImage(pixels, width, height, width * 4, QImage::Format_RGBA8888).copy();
}

QRect contentBounds(const QImage &image) {
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const auto color = image.pixelColor(x, y);
            if (std::max({color.red(), color.green(), color.blue()}) > 70)
                bounds |= QRect(x, y, 1, 1);
        }
    }
    return bounds;
}

char quadrantColor(const QColor &color) {
    if (color.red() > 180 && color.green() < 80 && color.blue() < 80) return 'R';
    if (color.green() > 180 && color.red() < 80 && color.blue() < 80) return 'G';
    if (color.blue() > 180 && color.red() < 80 && color.green() < 80) return 'B';
    if (color.red() > 180 && color.green() > 180 && color.blue() < 80) return 'Y';
    return '?';
}

QByteArray corners(const QImage &image, const QRect &bounds) {
    QByteArray result;
    for (const QPoint point : {QPoint(1, 1), QPoint(3, 1), QPoint(1, 3), QPoint(3, 3)}) {
        const int x = bounds.left() + bounds.width() * point.x() / 4;
        const int y = bounds.top() + bounds.height() * point.y() / 4;
        result += quadrantColor(image.pixelColor(x, y));
    }
    return result;
}

std::unique_ptr<Mlt::Tractor> timelineFor(const QString &path, Mlt::Profile &profile, QString *error) {
    Project project;
    for (const int degrees : {0, 90, 180, 270}) {
        Clip clip{path, 0, 0.25};
        clip.rotationDegrees = degrees;
        project.clips.append(clip);
    }
    return Media::buildTimeline(project, profile, error);
}
}

class RotationEngineTest : public QObject {
    Q_OBJECT
private slots:
    void rotatesAndFitsEachInstance();
    void addsToCameraOrientation();
};

void RotationEngineTest::rotatesAndFitsEachInstance() {
    Mlt::Profile profile;
    Media::configureOutputProfile(profile);
    profile.set_width(160);
    profile.set_height(90);
    const QString path = QFINDTESTDATA("fixtures/rotation.mp4");
    QVERIFY(!path.isEmpty());
    QString error;
    auto timeline = timelineFor(path, profile, &error);
    QVERIFY2(timeline, qPrintable(error));
    QCOMPARE(timeline->get_playtime(), 60);
    const std::array<QByteArray, 4> expected{"RGBY", "BRYG", "YBGR", "GYRB"};
    for (int i = 0; i < 4; ++i) {
        const QImage image = imageAt(*timeline, i * 15 + 7);
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), QSize(160, 90));
        const QRect bounds = contentBounds(image);
        QCOMPARE(corners(image, bounds), expected[i]);
        const double aspect = bounds.width() / double(bounds.height());
        QVERIFY(qAbs(aspect - (i % 2 ? 9.0 / 16.0 : 16.0 / 9.0)) < 0.04);
        QVERIFY(qAbs(bounds.center().x() - image.rect().center().x()) <= 1);
        QCOMPARE(bounds.height(), 90);
    }
}

void RotationEngineTest::addsToCameraOrientation() {
    Mlt::Profile profile;
    Media::configureOutputProfile(profile);
    profile.set_width(160);
    profile.set_height(90);
    const QString path = QFINDTESTDATA("fixtures/rotation-metadata.mp4");
    QVERIFY(!path.isEmpty());
    QString error;
    auto timeline = timelineFor(path, profile, &error);
    QVERIFY2(timeline, qPrintable(error));
    // The display matrix requests a counterclockwise turn before the user's rotation.
    const std::array<QByteArray, 4> expected{"GYRB", "RGBY", "BRYG", "YBGR"};
    for (int i = 0; i < 4; ++i) {
        const QImage image = imageAt(*timeline, i * 15 + 7);
        QVERIFY(!image.isNull());
        QCOMPARE(corners(image, contentBounds(image)), expected[i]);
    }
}

QTEST_GUILESS_MAIN(RotationEngineTest)
#include "rotation_engine_test.moc"
