#include "project.h"

#include <QFile>
#include <QSet>
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

bool MediaAsset::operator==(const MediaAsset &o) const {
    return path==o.path && kind==o.kind && durationFrames==o.durationFrames &&
        cursorFrame==o.cursorFrame && inFrame==o.inFrame && outFrame==o.outFrame;
}

void Project::deriveAssets() {
    const auto add=[this](const QString &path,const QString &kind,int frames) {
        if(path.isEmpty()) return;
        for(auto &a:assets) if(a.path==path) { a.durationFrames=std::max(a.durationFrames,frames); a.outFrame=a.durationFrames; return; }
        assets.append({path,kind,std::max(1,frames),0,0,std::max(1,frames)});
    };
    for(const auto &c:clips) add(c.path,"video",qRound(c.outSeconds*fps));
    add(voicePath,"audio",1); add(musicPath,"audio",1);
}

int Project::insertClipAtFrame(const Clip &clip,int frame) {
    if(frame<0 || frame>durationFrames() || clipFrames(clip)<1) return -1;
    Project next=*this;
    int boundary=-1;
    if(frame==0) boundary=0;
    else if(frame==durationFrames()) boundary=clips.size();
    else {
        // Both the beginning and end of an active overlap are excluded, as in Split.
        for(int i=1;i<clips.size();++i) {
            const int overlap=transitionFramesBefore(i),start=clipStartFrame(i);
            if(overlap>0 && frame>=start && frame<=start+overlap) return -1;
            if(overlap==0 && frame==start) boundary=i;
        }
        if(boundary<0) for(int i=0;i<clips.size();++i) {
            const int offset=frame-clipStartFrame(i);
            if(offset>0 && offset<clipFrames(clips[i])) {
                if(!next.splitClip(i,offset)) return -1;
                boundary=i+1; break;
            }
        }
    }
    if(boundary<0) return -1;
    Clip instance=clip; instance.hardCutBefore=true;
    if(boundary<next.clips.size()) next.clips[boundary].hardCutBefore=true;
    next.clips.insert(boundary,instance);
    next.normalizeVisuals();
    if(!next.validate().isEmpty()) return -1;
    *this=next; return boundary;
}

bool AudioControls::operator==(const AudioControls &o) const {
    return gainDb==o.gainDb && muted==o.muted && fadeInFrames==o.fadeInFrames && fadeOutFrames==o.fadeOutFrames;
}
AudioControls Project::audioControls(int lane,int index) const {
    if(lane==1)return voiceAudio;
    if(lane==2)return {musicDb,musicMuted,musicFadeInFrames,musicFadeOutFrames};
    return index>=0&&index<clips.size()?clips[index].audio:AudioControls{};
}
void Project::setAudioControls(int lane,const AudioControls &a,int index) {
    if(lane==1)voiceAudio=a;
    else if(lane==2){musicDb=a.gainDb;musicMuted=a.muted;musicFadeInFrames=a.fadeInFrames;musicFadeOutFrames=a.fadeOutFrames;}
    else if(index>=0&&index<clips.size())clips[index].audio=a;
}
namespace {
bool validAudio(const AudioControls &a) {
    return std::isfinite(a.gainDb)&&a.gainDb>=-60&&a.gainDb<=12&&a.fadeInFrames>=0&&a.fadeOutFrames>=0&&
        a.fadeInFrames<=86400*60&&a.fadeOutFrames<=86400*60;
}
QJsonObject audioJson(const AudioControls &a) {
    return {{"gainDb",a.gainDb},{"muted",a.muted},{"fadeInFrames",a.fadeInFrames},{"fadeOutFrames",a.fadeOutFrames}};
}
bool readAudio(const QJsonValue &v,AudioControls &a) {
    if(!v.isObject())return false;const auto o=v.toObject();
    if(!o["gainDb"].isDouble()||!o["muted"].isBool())return false;
    for(const auto key:{"fadeInFrames","fadeOutFrames"}){const auto n=o[key];if(!n.isDouble()||n.toDouble()<0||n.toDouble()>86400*60||std::floor(n.toDouble())!=n.toDouble())return false;}
    a={o["gainDb"].toDouble(),o["muted"].toBool(),o["fadeInFrames"].toInt(),o["fadeOutFrames"].toInt()};return validAudio(a);
}
}

bool TextOverlay::operator==(const TextOverlay &o) const {
    return text==o.text && fontFamily==o.fontFamily && style==o.style && color==o.color &&
        fontSize==o.fontSize && bold==o.bold && shadow==o.shadow && fade==o.fade &&
        startFrame==o.startFrame && durationFrames==o.durationFrames && x==o.x && y==o.y && hostId==o.hostId && hostSourceFrame==o.hostSourceFrame && timingNeedsReview==o.timingNeedsReview;
}

int Project::clipFrames(const Clip &clip) const {
    return qMax(0, qRound(clip.outSeconds * fps) - qRound(clip.inSeconds * fps));
}

int Project::effectiveCrossfadeFrames() const {
    if (clips.size() < 2 || crossfadeFrames <= 0) return 0;
    int overlap = crossfadeFrames;
    bool hasFade = false;
    for (int i = 0; i < clips.size(); ++i) {
        const int fades = int(i > 0 && !clips[i].hardCutBefore) +
            int(i + 1 < clips.size() && !clips[i + 1].hardCutBefore);
        if (fades == 0) continue;
        hasFade = true;
        const int frames = clipFrames(clips[i]);
        const int available = (frames - 1) / fades;
        overlap = qMin(overlap, qMax(0, available));
    }
    return hasFade ? overlap : 0;
}

int Project::transitionFramesBefore(int index) const {
    if(index<=0||index>=clips.size()||clips[index].hardCutBefore||clips[index].transitionType=="cut")return 0;
    const bool local=std::any_of(clips.cbegin(),clips.cend(),[](const Clip &c){return c.transitionFrames>=0;});
    if(!local)return effectiveCrossfadeFrames();
    auto requested=[&](int i){return i>0&&i<clips.size()&&!clips[i].hardCutBefore&&clips[i].transitionType!="cut"
        ?std::max(0,clips[i].transitionFrames>=0?clips[i].transitionFrames:crossfadeFrames):0;};
    auto bounded=[&](int i){return i>0&&i<clips.size()?std::max(0,std::min({requested(i),clipFrames(clips[i-1])-1,clipFrames(clips[i])-1})):0;};
    const int desired=bounded(index);
    int result=desired;
    for(int neighbor:{index-1,index+1}){
        const int shared=neighbor<index?index-1:index;
        const int available=clipFrames(clips[shared])-1;
        const int combined=desired+bounded(neighbor);
        if(combined>available&&combined>0)result=std::min(result,int(qint64(desired)*available/combined));
    }
    return result;
}
bool Project::setTransition(int index,const QString &type,int frames) {
    if(index<=0||index>=clips.size()||(type!="cut"&&type!="dissolve")||frames<0||frames>600)return false;
    if(transitionFramesBefore(index)==(type=="cut"?0:frames) && ((type=="cut")== (transitionFramesBefore(index)==0)))return false;
    const auto before=clips;
    // Freeze inherited values before the first local edit, preserving distant legacy cuts.
    QVector<int> inherited;for(int i=0;i<clips.size();++i)inherited.append(transitionFramesBefore(i));
    for(int i=1;i<clips.size();++i)if(clips[i].transitionFrames<0)clips[i].transitionFrames=inherited[i];
    clips[index].transitionType=type;clips[index].hardCutBefore=type=="cut";
    clips[index].transitionFrames=type=="cut"?0:frames;
    // Existing adjacent overlaps have priority. New requests cannot steal their frames.
    if(type!="cut") {
        const int left=std::max(0,clipFrames(clips[index-1])-1-inherited[index-1]);
        const int right=std::max(0,clipFrames(clips[index])-1-(index+1<clips.size()?inherited[index+1]:0));
        clips[index].transitionFrames=std::min({frames,left,right});
    }
    bool timingChanged=false;for(int i=1;i<clips.size();++i)timingChanged|=transitionFramesBefore(i)!=inherited[i];
    if(!timingChanged){clips=before;return false;}
    return true;
}

int Project::clipStartFrame(int index) const {
    if (index < 0 || index >= clips.size()) return -1;
    int start = 0;
    for (int i = 0; i < index; ++i)
        start += clipFrames(clips[i]) - transitionFramesBefore(i+1);
    return start;
}

int Project::durationFrames() const {
    int total = 0;
    for (const auto &clip : clips) total += clipFrames(clip);
    for (int i = 1; i < clips.size(); ++i) total -= transitionFramesBefore(i);
    return total;
}

bool Project::splitClip(int index, int offsetFrames) {
    if (index < 0 || index >= clips.size() || !validate().isEmpty()) return false;
    const Clip original = clips[index];
    const int frames = clipFrames(original);
    // Keep both pieces outside the neighboring overlaps so a split changes no timing.
    if (offsetFrames <= transitionFramesBefore(index) ||
        offsetFrames >= frames - transitionFramesBefore(index + 1)) return false;
    const int first = qRound(original.inSeconds * fps);
    const int last = qRound(original.outSeconds * fps);
    Clip left = original;
    Clip right = original;
    left.inSeconds = first / double(fps);
    left.outSeconds = right.inSeconds = (first + offsetFrames) / double(fps);
    right.outSeconds = last / double(fps);
    left.audio.fadeOutFrames=0;right.audio.fadeInFrames=0;
    left.audio.fadeInFrames=std::min(left.audio.fadeInFrames,offsetFrames);right.audio.fadeOutFrames=std::min(right.audio.fadeOutFrames,frames-offsetFrames);
    right.hardCutBefore = true;
    right.transitionFrames = -1; right.transitionType = "cut";
    right.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    for(auto &o:visualOverlays)if(o.hostId==original.id && o.hostSourceFrame>=first+offsetFrames)o.hostId=right.id;
    for(auto *list:{&textOverlays,&captions})for(auto &t:*list)if(t.hostId==original.id&&t.hostSourceFrame>=first+offsetFrames)t.hostId=right.id;
    clips[index] = left;
    clips.insert(index + 1, right);
    return true;
}

bool Project::reorderClips(const QVector<int> &order) {
    if (order.size() != clips.size()) return false;
    QVector<bool> seen(clips.size(), false);
    bool changed = false;
    for (int i = 0; i < order.size(); ++i) {
        const int index = order[i];
        if (index < 0 || index >= clips.size() || seen[index]) return false;
        seen[index] = true;
        changed |= index != i;
    }
    if (!changed) return false;
    QVector<Clip> sorted;
    sorted.reserve(clips.size());
    for (int index : order) sorted.append(clips[index]);
    clips.swap(sorted);
    normalizeVisuals();
    return true;
}

bool Project::rotateClip(int index, int deltaDegrees) {
    if (index < 0 || index >= clips.size() || deltaDegrees % 90 != 0) return false;
    int &rotation = clips[index].rotationDegrees;
    if (rotation < 0 || rotation >= 360 || rotation % 90 != 0) return false;
    const int updated = (rotation + deltaDegrees % 360 + 360) % 360;
    if (updated == rotation) return false;
    rotation = updated;
    return true;
}

QString Project::validate() const {
    QStringList paths;
    for(const auto &a:assets) {
        if(a.path.isEmpty() || paths.contains(a.path) || (a.kind!="video" && a.kind!="audio") ||
            a.durationFrames<1 || a.durationFrames>86400*fps || a.cursorFrame<0 || a.cursorFrame>=a.durationFrames ||
            a.inFrame<0 || a.inFrame>=a.durationFrames || a.outFrame<1 || a.outFrame>a.durationFrames)
            return "A media asset has invalid identity, duration, cursor, or range.";
        paths.append(a.path);
    }
    if((canvasWidth!=0||canvasHeight!=0) && (canvasWidth<2||canvasHeight<2||canvasWidth>8192||canvasHeight>8192||canvasWidth%2||canvasHeight%2))return "Canvas dimensions must be even numbers from 2 to 8192.";
    if(visualLaneVisible.size()!=3)return "Three visual lanes are required.";
    QSet<QString> ids;
    for(const auto &c:clips){if(c.id.isEmpty()||ids.contains(c.id))return "Clip instance IDs must be unique.";ids.insert(c.id);const auto e=c.transform.validate();if(!e.isEmpty())return e;}
    for(const auto &o:visualOverlays){
        if(o.path.isEmpty()||o.sourceInFrame<0||o.durationFrames<1||qint64(o.sourceInFrame)+o.durationFrames>86400*fps||o.startFrame<0||o.startFrame>86400*fps||o.lane<0||o.lane>=3||o.hostSourceFrame<0||o.hostSourceFrame>86400*fps||(!o.hostId.isEmpty()&&!ids.contains(o.hostId)))return "An overlay has invalid timing, lane, or connection.";
        const auto e=o.transform.validate();if(!e.isEmpty())return e;
    }
    if(captions.size()>10000)return "A project supports up to 10000 captions.";
    for (const auto *list : {&textOverlays,&captions})for(const auto &t:*list) {
        if (t.text.size()>2000 || t.fontFamily.isEmpty() || t.fontFamily.size()>200 ||
            (t.style!="clean" && t.style!="playful" && t.style!="box") || !QColor(t.color).isValid() ||
            t.fontSize<12 || t.fontSize>200 || t.startFrame<0 || t.startFrame>86400*fps ||
            t.durationFrames<1 || t.durationFrames>86400*fps-t.startFrame || t.hostSourceFrame<0 || t.hostSourceFrame>86400*fps || (!t.hostId.isEmpty()&&!ids.contains(t.hostId)) ||
            !std::isfinite(t.x) || !std::isfinite(t.y) || t.x<0 || t.x>1 || t.y<0 || t.y>1)
            return "A text overlay has invalid content, style, position, or timing.";
    }
    if(!validAudio(voiceAudio)||!validAudio(audioControls(2)))return "Audio gain or fades are invalid.";
    if(voiceDurationFrames<0||voiceDurationFrames>86400*fps||musicDurationFrames<0||musicDurationFrames>86400*fps)return "Audio source duration is invalid.";
    if (crossfadeFrames < 0) return "Crossfade cannot be negative.";
    if (!std::isfinite(voiceStartSeconds) || voiceStartSeconds < 0)
        return "Narration start must be zero or later.";
    if (!std::isfinite(musicDb) || musicDb < -60 || musicDb > 12)
        return "Music level must be between -60 and +12 dB.";
    for (int i = 0; i < clips.size(); ++i) {
        const auto &clip = clips[i];
        if(!validAudio(clip.audio)||clip.transitionFrames < -1||clip.transitionFrames>600||
            (clip.transitionType!="cut"&&clip.transitionType!="dissolve"))return "A clip audio or transition setting is invalid.";
        if (clip.path.isEmpty()) return QString("Clip %1 has no file.").arg(i + 1);
        if (clip.rotationDegrees < 0 || clip.rotationDegrees >= 360 || clip.rotationDegrees % 90 != 0)
            return QString("Clip %1 needs a rotation of 0, 90, 180, or 270 degrees.").arg(i + 1);
        if (!std::isfinite(clip.inSeconds) || !std::isfinite(clip.outSeconds) ||
            clip.inSeconds < 0 || clip.outSeconds > 86400 || clipFrames(clip) < 1)
            return QString("Clip %1 needs a valid start and end.").arg(i + 1);
    }
    return {};
}

bool saveProject(const Project &project, const QString &path, QString *error) {
    const QString problem = project.validate();
    if (!problem.isEmpty()) {
        if (error) *error = problem;
        return false;
    }
    QJsonArray clips;
    for (const auto &clip : project.clips) {
        QJsonObject item{{"path", clip.path}, {"in", clip.inSeconds}, {"out", clip.outSeconds}};
        if (clip.hardCutBefore) item.insert("hardCutBefore", true);
        if (clip.rotationDegrees != 0) item.insert("rotationDegrees", clip.rotationDegrees);
        if(!(clip.audio==AudioControls{}))item.insert("audio",audioJson(clip.audio));
        if(clip.transitionFrames>=0){item.insert("transitionFrames",clip.transitionFrames);item.insert("transitionType",clip.transitionType);}
        item.insert("id",clip.id);item.insert("transform",transformJson(clip.transform));
        clips.append(item);
    }
    QJsonObject data{{"version", 7}, {"clips", clips},
                           {"crossfadeSeconds", project.crossfadeFrames / double(Project::fps)},
                           {"voicePath", project.voicePath},
                           {"voiceDurationFrames", project.voiceDurationFrames},
                           {"voiceStartSeconds", project.voiceStartSeconds},
                           {"musicPath", project.musicPath}, {"musicDurationFrames", project.musicDurationFrames}, {"musicDb", project.musicDb}};
    {
        QJsonArray assets;
        for(const auto &a:project.assets) assets.append(QJsonObject{{"path",a.path},{"kind",a.kind},
            {"durationFrames",a.durationFrames},{"cursorFrame",a.cursorFrame},{"inFrame",a.inFrame},{"outFrame",a.outFrame}});
        data.insert("assets",assets);
    }
    data.insert("canvasWidth",project.canvasWidth);data.insert("canvasHeight",project.canvasHeight);
    QJsonArray visible;for(bool v:project.visualLaneVisible)visible.append(v);data.insert("visualLaneVisible",visible);
    QJsonArray overlays;for(const auto &o:project.visualOverlays)overlays.append(QJsonObject{{"path",o.path},{"sourceInFrame",o.sourceInFrame},{"durationFrames",o.durationFrames},{"startFrame",o.startFrame},{"lane",o.lane},{"hostId",o.hostId},{"hostSourceFrame",o.hostSourceFrame},{"transform",transformJson(o.transform)}});
    data.insert("visualOverlays",overlays);
    for(bool caption:{false,true}) {
        QJsonArray texts;
        for (const auto &t:caption?project.captions:project.textOverlays){
            QJsonObject item{{"text",t.text},{"fontFamily",t.fontFamily},{"style",t.style},
                {"color",t.color},{"fontSize",t.fontSize},{"bold",t.bold},{"shadow",t.shadow},{"fade",t.fade},
                {"startFrame",t.startFrame},{"durationFrames",t.durationFrames},{"x",t.x},{"y",t.y}};
            if(!t.hostId.isEmpty()){item.insert("hostId",t.hostId);item.insert("hostSourceFrame",t.hostSourceFrame);}
            if(t.timingNeedsReview)item.insert("timingNeedsReview",true);
            texts.append(item);
        }
        data.insert(caption?"captions":"textOverlays",texts);
    }
    data.insert("burnCaptions",project.burnCaptions);
    {data.insert("voiceAudio",audioJson(project.voiceAudio));data.insert("musicAudio",audioJson(project.audioControls(2)));data.insert("musicLoopToEnd",project.musicLoopToEnd);}
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(data).toJson(QJsonDocument::Indented)) < 0 ||
        !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

std::optional<Project> loadProject(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return std::nullopt;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = "This project file is not valid JSON.";
        return std::nullopt;
    }
    const auto data = document.object();
    const int version = data.value("version").toInt();
    if ((version < 1 || version > 7) || !data.value("clips").isArray()) {
        if (error) *error = "This project format is not supported.";
        return std::nullopt;
    }
    Project project;
    if (version>=6 && data.contains("assets")) {
        if(!data.value("assets").isArray()) { if(error)*error="The media library is invalid.";return std::nullopt; }
        for(const auto &value:data.value("assets").toArray()) {
            const auto item=value.toObject();bool valid=value.isObject() && item["path"].isString() && item["kind"].isString();
            for(const auto &key:{"durationFrames","cursorFrame","inFrame","outFrame"}) {
                const auto n=item[key].toDouble(-1); valid &= item[key].isDouble() && std::isfinite(n) && n>=0 && n<=86400*Project::fps && n==std::floor(n);
            }
            if(!valid) { if(error)*error="A media library entry is invalid.";return std::nullopt; }
            project.assets.append({item["path"].toString(),item["kind"].toString(),item["durationFrames"].toInt(),
                item["cursorFrame"].toInt(),item["inFrame"].toInt(),item["outFrame"].toInt()});
        }
    }
    if(version>=6 && (data.contains("canvasWidth")||data.contains("canvasHeight")||data.contains("visualLaneVisible")||data.contains("visualOverlays"))){
        auto integer=[](const QJsonValue &v,int low,int high){const double n=v.toDouble(-1);return v.isDouble()&&std::isfinite(n)&&n>=low&&n<=high&&n==std::floor(n);};
        bool valid=integer(data["canvasWidth"],0,8192)&&integer(data["canvasHeight"],0,8192)&&data["visualLaneVisible"].isArray()&&data["visualOverlays"].isArray();
        project.canvasWidth=data["canvasWidth"].toInt();project.canvasHeight=data["canvasHeight"].toInt();project.visualLaneVisible.clear();
        for(const auto &v:data["visualLaneVisible"].toArray()){valid &= v.isBool();project.visualLaneVisible.append(v.toBool());}
        for(const auto &v:data["visualOverlays"].toArray()){const auto j=v.toObject();VisualOverlay o;
            valid &= v.isObject()&&j["path"].isString()&&j["hostId"].isString();
            for(const auto &key:{"sourceInFrame","durationFrames","startFrame","lane","hostSourceFrame"})valid &= integer(j[key],0,86400*60);
            o.path=j["path"].toString();o.hostId=j["hostId"].toString();o.sourceInFrame=j["sourceInFrame"].toInt();o.durationFrames=j["durationFrames"].toInt();o.startFrame=j["startFrame"].toInt();o.lane=j["lane"].toInt();o.hostSourceFrame=j["hostSourceFrame"].toInt();
            valid &= readTransform(j["transform"].toObject(),o.transform);project.visualOverlays.append(o);
        }
        if(!valid){if(error)*error="Invalid canvas or overlay data.";return std::nullopt;}
    }
    for(bool caption:{false,true}) {
        const auto key=caption?"captions":"textOverlays";
        if(caption&&version<7)continue;
        if(!caption&&(version<5||(version>5&&!data.contains(key))))continue;
        if (!data.value(key).isArray()) {
            if(error)*error="The text overlay list is invalid.";
            return std::nullopt;
        }
        for (const auto &value:data.value(key).toArray()) {
            const auto item=value.toObject();
            bool valid=value.isObject();
            for (const auto &key:{"text","fontFamily","style","color"}) valid &= item.value(key).isString();
            for (const auto &key:{"bold","shadow","fade"}) valid &= item.value(key).isBool();
            for (const auto &key:{"fontSize","startFrame","durationFrames","x","y"}) valid &= item.value(key).isDouble();
            for (const auto &key:{"fontSize","startFrame","durationFrames"}) {
                const double n=item.value(key).toDouble(-1);
                valid &= std::isfinite(n) && n>=0 && n<=86400*Project::fps && n==std::floor(n);
            }
            if (!valid) { if(error)*error="A text overlay entry is invalid."; return std::nullopt; }
            TextOverlay t;
            t.text=item["text"].toString(); t.fontFamily=item["fontFamily"].toString();
            t.style=item["style"].toString(); t.color=item["color"].toString();t.fontSize=item["fontSize"].toInt();
            t.bold=item["bold"].toBool();t.shadow=item["shadow"].toBool();t.fade=item["fade"].toBool();
            t.startFrame=item["startFrame"].toInt();t.durationFrames=item["durationFrames"].toInt();
            t.x=item["x"].toDouble();t.y=item["y"].toDouble();
            if(version>=7){
                if(item.contains("hostId")){
                    const auto n=item["hostSourceFrame"].toDouble(-1);
                    valid &= item["hostId"].isString()&&item["hostSourceFrame"].isDouble()&&std::isfinite(n)&&n>=0&&n<=86400*Project::fps&&n==std::floor(n);
                    t.hostId=item["hostId"].toString();t.hostSourceFrame=item["hostSourceFrame"].toInt();
                }else if(item.contains("hostSourceFrame"))valid=false;
                if(item.contains("timingNeedsReview")){valid &= item["timingNeedsReview"].isBool();t.timingNeedsReview=item["timingNeedsReview"].toBool();}
                if(!valid){if(error)*error="Invalid text connection or review state.";return std::nullopt;}
            }
            (caption?project.captions:project.textOverlays).append(t);
        }
    }
    if(version>=7){if(!data["burnCaptions"].isBool()){if(error)*error="Invalid caption burn option.";return std::nullopt;}project.burnCaptions=data["burnCaptions"].toBool();}
    for (const auto &value : data.value("clips").toArray()) {
        if (!value.isObject()) {
            if (error) *error = "A clip entry is invalid.";
            return std::nullopt;
        }
        const auto item = value.toObject();
        if (version >= 3 && item.contains("hardCutBefore") && !item.value("hardCutBefore").isBool()) {
            if (error) *error = "A clip transition setting is invalid.";
            return std::nullopt;
        }
        int rotation = 0;
        if (version >= 4 && item.contains("rotationDegrees")) {
            const auto value = item.value("rotationDegrees");
            const double angle = value.toDouble(-1);
            if (!value.isDouble() || !std::isfinite(angle) ||
                (angle != 0 && angle != 90 && angle != 180 && angle != 270)) {
                if (error) *error = "A clip rotation setting is invalid.";
                return std::nullopt;
            }
            rotation = int(angle);
        }
        project.clips.append({item.value("path").toString(),
                              item.value("in").toDouble(-1),
                              item.value("out").toDouble(-1),
                              version >= 3 && item.value("hardCutBefore").toBool(), rotation});
        auto &clip=project.clips.last();
        if(item.contains("audio")&&!readAudio(item["audio"],clip.audio)){if(error)*error="Invalid clip audio controls.";return std::nullopt;}
        if(item.contains("transitionFrames")){
            const auto n=item["transitionFrames"];const double frames=n.toDouble(-2);
            if(!n.isDouble()||!std::isfinite(frames)||frames<0||frames>600||frames!=std::floor(frames)||!item["transitionType"].isString()){
                if(error)*error="Invalid cut transition.";return std::nullopt;
            }
            clip.transitionFrames=int(frames);clip.transitionType=item["transitionType"].toString();
        }

        if(version>=6 && (item.contains("id")||item.contains("transform"))){auto &clip=project.clips.last();clip.id=item["id"].toString();if(!readTransform(item["transform"].toObject(),clip.transform)){if(error)*error="Invalid transform data.";return std::nullopt;}}
    }
    // Version 1 stored transition frames on the original 30 fps timeline.
    const double crossfadeSeconds = version == 1
        ? data.value("crossfadeFrames").toDouble() / 30.0
        : data.value("crossfadeSeconds").toDouble();
    if (!std::isfinite(crossfadeSeconds) || crossfadeSeconds < 0 ||
        crossfadeSeconds > std::numeric_limits<int>::max() / double(Project::fps)) {
        if (error) *error = "The project crossfade duration is invalid.";
        return std::nullopt;
    }
    project.crossfadeFrames = qRound(crossfadeSeconds * Project::fps);
    project.voicePath = data.value("voicePath").toString();
    project.voiceStartSeconds = data.value("voiceStartSeconds").toDouble();
    project.musicPath = data.value("musicPath").toString();
    project.musicDb = data.value("musicDb").toDouble(-18);
    for(const auto &key:{"voiceDurationFrames","musicDurationFrames"})if(data.contains(key)){
        const auto n=data[key].toDouble(-1);
        if(!data[key].isDouble()||!std::isfinite(n)||n<0||n>86400*Project::fps||n!=std::floor(n)){if(error)*error="Invalid audio source duration.";return std::nullopt;}
    }
    project.voiceDurationFrames=data["voiceDurationFrames"].toInt();project.musicDurationFrames=data["musicDurationFrames"].toInt();
    if(version<6 || !data.contains("assets")) project.deriveAssets();
    if(data.contains("voiceAudio")&&!readAudio(data["voiceAudio"],project.voiceAudio)){if(error)*error="Invalid narration controls.";return std::nullopt;}
    if(data.contains("musicAudio")){AudioControls a;if(!readAudio(data["musicAudio"],a)){if(error)*error="Invalid music controls.";return std::nullopt;}project.setAudioControls(2,a);}
    if(data.contains("musicLoopToEnd")){if(!data["musicLoopToEnd"].isBool()){if(error)*error="Invalid music loop setting.";return std::nullopt;}project.musicLoopToEnd=data["musicLoopToEnd"].toBool();}
    const QString problem = project.validate();
    if (!problem.isEmpty()) {
        if (error) *error = problem;
        return std::nullopt;
    }
    return project;
}
