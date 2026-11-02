#pragma once
#include "project.h"
#include <QHash>
#include <QJsonObject>
#include <QRect>
#include <QSize>
#include <functional>

// Optional FFmpeg CUDA export. CPU MLT remains the renderer for unsupported projects.
namespace GpuPipeline {
struct Source {
    int width=0,height=0,frames=0;
    bool still=false;
};
struct Layer {QString path;int inFrame=0;QRect rectangle;bool still=false;VisualTransform transform;};
struct Segment {int start=0,frames=0;QVector<Layer> layers;};
struct Plan {QSize size;QVector<Segment> segments;QString reason;bool supported() const {return reason.isEmpty()&&!segments.isEmpty();}};
using Cancel=std::function<bool()>;
using Diagnostic=std::function<void(const QString&)>;
// Compiled capabilities are distinct from the source-based runtime render probe.
QJsonObject capabilities();
QString projectSupportReason(const Project &project);
std::optional<Source> sourceFromProbe(const QByteArray &json,QString *reason=nullptr);
Plan plan(const Project &project,const QSize &size,const QHash<QString,Source> &sources);
QStringList segmentArguments(const Segment &segment,const QSize &size,const QString &output,bool iphone=false,int frameLimit=0);
enum class Result {Unavailable,Failed,Canceled,Completed};
// Outputs are caller-owned staging paths; exportMp4 validates and publishes the final MP4.
Result renderVideo(const Project &project,const QSize &size,const QString &output,
                   Cancel canceled={},std::function<void(int)> progress={},Diagnostic diagnostic={},
                   bool iphone=false,QString *reason=nullptr);
// Render the frozen project's audio with the same MLT mixer as the normal export.
bool renderAudio(const Project &project,const QSize &size,const QString &output,
                 Cancel canceled={},std::function<void(int)> progress={},QString *error=nullptr,Diagnostic diagnostic={});
bool muxAudio(const QString &video,const QString &audio,const QString &output,int frames,
              Cancel canceled={},QString *error=nullptr);
}
