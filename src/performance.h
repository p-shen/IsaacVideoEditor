#pragma once

#include <QtGlobal>

namespace Performance {
struct Resources { int logicalCpus=1; quint64 totalMemoryBytes=0, availableMemoryBytes=0; };
struct Budget {
    int renderThreads=1, proxyJobs=1, mediaJobs=1, backgroundThreads=1;
    int previewWidth=960, previewHeight=540;
    int thumbnailBytes=16*1024*1024, waveformBytes=8*1024*1024;
    qsizetype frameCacheBytes=32*1024*1024;
    quint64 exportQueueBytes=64*1024*1024;
    bool prepareDuringExport=false;
    int prefetchFrames=0;
};
Resources resources();
struct ProcessMemory {quint64 residentBytes=0,peakResidentBytes=0;};
ProcessMemory processMemory();
Budget budgetFor(const Resources &resources);
Budget budget();
}
