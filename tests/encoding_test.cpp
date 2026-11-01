#include "encoding.h"
#include <QtTest>

class EncodingTest : public QObject {
    Q_OBJECT
private slots:
    void recognizesHardwareAndNamesIt_data() {
        QTest::addColumn<QString>("encoder");QTest::addColumn<bool>("hardware");QTest::addColumn<QString>("label");
        QTest::newRow("NVIDIA")<<QString("h264_nvenc")<<true<<QString("NVIDIA hardware encoding");
        QTest::newRow("Apple")<<QString("h264_videotoolbox")<<true<<QString("Apple hardware encoding");
        QTest::newRow("software")<<QString("libx264")<<false<<QString("software encoding");
        QTest::newRow("unrecognized")<<QString("h264_nvenc_unknown")<<false<<QString("software encoding");
    }
    void recognizesHardwareAndNamesIt() {
        QFETCH(QString,encoder);QFETCH(bool,hardware);QFETCH(QString,label);
        QCOMPARE(Media::isHardwareH264Encoder(encoder),hardware);
        QCOMPARE(Media::h264EncoderLabel(encoder),label);
    }
    void choosesPlatformCandidate() {
#ifdef Q_OS_MACOS
        QCOMPARE(Media::hardwareH264Encoder(),QString("h264_videotoolbox"));
#elif defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        QCOMPARE(Media::hardwareH264Encoder(),QString("h264_nvenc"));
#else
        QVERIFY(Media::hardwareH264Encoder().isEmpty());
#endif
    }
};
QTEST_GUILESS_MAIN(EncodingTest)
#include "encoding_test.moc"
