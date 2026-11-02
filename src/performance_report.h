#pragma once
#include <QString>

// A local, repeatable editing workload; writes diagnostics only to the chosen report.
int runPerformanceReport(const QString &projectPath,const QString &reportPath);
int runBenchmarkExport(const QString &projectPath,const QString &outputPath,bool softwareOnly,bool iphone);
