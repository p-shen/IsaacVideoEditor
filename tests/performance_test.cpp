#include "performance.h"
#include <QtTest>
#include <limits>

namespace { constexpr quint64 GiB=quint64(1024)*1024*1024; }

class PerformanceTest : public QObject {
    Q_OBJECT
private slots:
    void unknownHardwareIsConservative() {
        const auto b=Performance::budgetFor({1,0,0});
        QCOMPARE(b.renderThreads,1);QCOMPARE(b.proxyJobs,1);QCOMPARE(b.mediaJobs,1);
        QCOMPARE(b.previewWidth,960);QCOMPARE(b.previewHeight,540);
        QVERIFY(b.frameCacheBytes<=32*1024*1024);QVERIFY(!b.prepareDuringExport);
    }
    void workstationUsesAvailableResources() {
        const auto b=Performance::budgetFor({32,168*GiB,128*GiB});
        QCOMPARE(b.renderThreads,8);QCOMPARE(b.proxyJobs,2);QCOMPARE(b.mediaJobs,2);
        QCOMPARE(b.backgroundThreads,4);QCOMPARE(b.previewWidth,1920);QCOMPARE(b.previewHeight,1080);
        QCOMPARE(b.thumbnailBytes,256*1024*1024);QCOMPARE(b.waveformBytes,64*1024*1024);
        QCOMPARE(b.frameCacheBytes,qsizetype(std::min(8*GiB,quint64(std::numeric_limits<qsizetype>::max()))));QVERIFY(b.prepareDuringExport);
    }
    void lowMemoryOverridesLargeCoreCount() {
        const auto b=Performance::budgetFor({64,168*GiB,GiB/2});
        QCOMPARE(b.renderThreads,1);QCOMPARE(b.proxyJobs,1);QCOMPARE(b.mediaJobs,1);
        QCOMPARE(b.backgroundThreads,1);QCOMPARE(b.previewWidth,960);
        QVERIFY(quint64(b.frameCacheBytes)*2<=GiB/8);QVERIFY(!b.prepareDuringExport);
    }
    void ordinaryDesktopKeepsPreviewLight() {
        const auto b=Performance::budgetFor({8,16*GiB,4*GiB});
        QCOMPARE(b.renderThreads,4);QCOMPARE(b.proxyJobs+b.mediaJobs,2);
        QCOMPARE(b.previewWidth,960);QCOMPARE(b.frameCacheBytes,qsizetype(GiB/4));
    }
    void workstationHasRoomForParallel4kFrames() {
        const auto b=Performance::budgetFor({32,168*GiB,128*GiB});
        const quint64 workingFrameBytes=quint64(3840)*2160*16;
        QVERIFY2(b.exportQueueBytes/workingFrameBytes>=16,"Abundant RAM should permit eight render workers and their queued 4K frames");
        QVERIFY(b.exportQueueBytes<=2*GiB);
    }
    void invalidAndExtremeInputsStayBounded() {
        for(const auto &r:{Performance::Resources{-3,0,0},Performance::Resources{4096,168*GiB,64*GiB},
                           Performance::Resources{64,GiB,1024},Performance::Resources{64,GiB,128*GiB}}){
            const auto b=Performance::budgetFor(r);
            QVERIFY(b.renderThreads>=1&&b.renderThreads<=8);QVERIFY(b.proxyJobs+b.mediaJobs<=4);
            QVERIFY(b.backgroundThreads>=1&&b.backgroundThreads<=8);QVERIFY(b.frameCacheBytes>=0);
            const quint64 available=r.totalMemoryBytes?std::min(r.availableMemoryBytes,r.totalMemoryBytes):r.availableMemoryBytes;
            if(available)QVERIFY(quint64(b.frameCacheBytes)<=available/16);
        }
    }
};
QTEST_GUILESS_MAIN(PerformanceTest)
#include "performance_test.moc"
