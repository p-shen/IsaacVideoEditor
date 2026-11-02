#include "performance.h"
#include <QFile>
#include <QThread>
#include <algorithm>
#include <limits>
#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif
#if defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#include <mach/mach.h>
#elif defined(Q_OS_WIN)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_LINUX)
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace Performance {
namespace { constexpr quint64 MiB=quint64(1024)*1024,GiB=1024*MiB; }

Resources resources(){
    Resources r;r.logicalCpus=std::max(1,QThread::idealThreadCount());
#if defined(Q_OS_MACOS)
    size_t size=sizeof(r.totalMemoryBytes);
    if(sysctlbyname("hw.memsize",&r.totalMemoryBytes,&size,nullptr,0)!=0)r.totalMemoryBytes=0;
    const auto host=mach_host_self();
    vm_statistics64_data_t stats{};mach_msg_type_number_t count=HOST_VM_INFO64_COUNT;
    vm_size_t pageSize=0;
    if(host_page_size(host,&pageSize)==KERN_SUCCESS&&host_statistics64(host,HOST_VM_INFO64,
        reinterpret_cast<host_info64_t>(&stats),&count)==KERN_SUCCESS)
        r.availableMemoryBytes=(quint64(stats.free_count)+stats.inactive_count+stats.purgeable_count)*pageSize;
    mach_port_deallocate(mach_task_self(),host);
#elif defined(Q_OS_WIN)
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
    if(GlobalMemoryStatusEx(&memory)){r.totalMemoryBytes=memory.ullTotalPhys;r.availableMemoryBytes=memory.ullAvailPhys;}
#elif defined(Q_OS_LINUX)
    struct sysinfo info{};
    if(sysinfo(&info)==0){r.totalMemoryBytes=quint64(info.totalram)*info.mem_unit;r.availableMemoryBytes=quint64(info.freeram)*info.mem_unit;}
    QFile meminfo("/proc/meminfo");
    if(meminfo.open(QIODevice::ReadOnly)){
        for(const auto &line:meminfo.readAll().split('\n'))if(line.startsWith("MemAvailable:")){
            const auto fields=line.simplified().split(' ');bool ok=false;
            const quint64 kib=fields.value(1).toULongLong(&ok);
            if(ok&&kib<=std::numeric_limits<quint64>::max()/1024)r.availableMemoryBytes=kib*1024;
            break;
        }
    }
#endif
    if(r.totalMemoryBytes)r.availableMemoryBytes=std::min(r.availableMemoryBytes,r.totalMemoryBytes);
    return r;
}

Budget budgetFor(const Resources &r){
    Budget b;
    const int cores=std::max(1,r.logicalCpus);
    const quint64 available=r.totalMemoryBytes?std::min(r.availableMemoryBytes,r.totalMemoryBytes):r.availableMemoryBytes;
    const bool known=available>0;
    b.renderThreads=std::clamp(cores/2,1,known?8:4);
    if(known&&available<2*GiB)b.renderThreads=1;
    if(cores>=16&&available>=8*GiB){b.proxyJobs=2;b.mediaJobs=2;}
    b.backgroundThreads=std::clamp(cores/(2*(b.proxyJobs+b.mediaJobs)),1,8);
    if(known&&available<2*GiB)b.backgroundThreads=1;
    if(cores>=12&&available>=8*GiB){b.previewWidth=1920;b.previewHeight=1080;}
    if(known){
        const quint64 total=r.totalMemoryBytes?r.totalMemoryBytes:available;
        b.thumbnailBytes=int(std::min({std::clamp(total/512,16*MiB,256*MiB),available/32,quint64(std::numeric_limits<int>::max())}));
        b.waveformBytes=int(std::min(std::clamp(total/2048,8*MiB,64*MiB),available/64));
        b.frameCacheBytes=qsizetype(std::min({available/16,8*GiB,quint64(std::numeric_limits<qsizetype>::max())}));
        b.exportQueueBytes=std::min(available/8,2*GiB);
    }
    b.prepareDuringExport=cores>=16&&available>=8*GiB;
    if(cores>=12&&available>=8*GiB)b.prefetchFrames=16;
    return b;
}
Budget budget(){return budgetFor(resources());}
ProcessMemory processMemory(){
    ProcessMemory result;
#if defined(Q_OS_MACOS)
    mach_task_basic_info_data_t info{};mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
    if(task_info(mach_task_self(),MACH_TASK_BASIC_INFO,reinterpret_cast<task_info_t>(&info),&count)==KERN_SUCCESS)
        result.residentBytes=info.resident_size;
    rusage usage{};if(getrusage(RUSAGE_SELF,&usage)==0)result.peakResidentBytes=quint64(usage.ru_maxrss);
#elif defined(Q_OS_LINUX)
    QFile statm("/proc/self/statm");
    if(statm.open(QIODevice::ReadOnly)){bool ok=false;const auto pages=statm.readAll().simplified().split(' ').value(1).toULongLong(&ok);
        if(ok)result.residentBytes=pages*quint64(sysconf(_SC_PAGESIZE));}
    rusage usage{};if(getrusage(RUSAGE_SELF,&usage)==0)result.peakResidentBytes=quint64(usage.ru_maxrss)*1024;
#elif defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS counters{};
    if(GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters))){result.residentBytes=counters.WorkingSetSize;result.peakResidentBytes=counters.PeakWorkingSetSize;}
#endif
    result.peakResidentBytes=std::max(result.peakResidentBytes,result.residentBytes);return result;
}
}
