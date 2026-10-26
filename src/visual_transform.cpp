#include "visual_transform.h"
#include <QJsonArray>
#include <cmath>
#include <algorithm>
double TransformCurve::valueAt(int frame) const {
    if(keys.isEmpty())return value;
    if(frame<=keys.first().frame)return keys.first().value;
    for(int i=1;i<keys.size();++i)if(frame<=keys[i].frame){
        const auto &a=keys[i-1],&b=keys[i];double t=double(frame-a.frame)/(b.frame-a.frame);
        if(a.ease)t=t*t*(3-2*t);return a.value+(b.value-a.value)*t;
    }
    return keys.last().value;
}
void TransformCurve::setKey(int frame,double v,bool ease) {
    for(auto &k:keys)if(k.frame==frame){k.value=v;k.ease=ease;return;}
    keys.append({frame,v,ease});std::sort(keys.begin(),keys.end(),[](const auto &a,const auto &b){return a.frame<b.frame;});
}
void TransformCurve::edit(int frame,double v) {
    if(keys.isEmpty())value=v;
    else {bool ease=false;for(const auto &k:keys)if(k.frame<=frame)ease=k.ease;setKey(frame,v,ease);}
}
bool VisualTransform::operator==(const VisualTransform &o) const {
    return x==o.x&&y==o.y&&scale==o.scale&&rotation==o.rotation&&opacity==o.opacity&&framing==o.framing&&cropLeft==o.cropLeft&&cropTop==o.cropTop&&cropRight==o.cropRight&&cropBottom==o.cropBottom;
}
QString VisualTransform::validate() const {
    if(framing!="fit"&&framing!="fill"&&framing!="crop")return "Choose Fit, Fill, or Crop.";
    const TransformCurve *curves[]={&x,&y,&scale,&rotation,&opacity};
    const double minimum[]={-10,-10,.01,-36000,0},maximum[]={10,10,100,36000,1};
    for(int i=0;i<5;++i){const auto &c=*curves[i];auto valid=[&](double v){return std::isfinite(v)&&v>=minimum[i]&&v<=maximum[i];};
        if(!valid(c.value)||c.keys.size()>10000)return "A transform value is outside its supported range.";
        int prior=-1;for(const auto &k:c.keys){if(k.frame<=prior||k.frame>86400*60||!valid(k.value))return "Invalid transform keyframe.";prior=k.frame;}}
    for(double v:{cropLeft,cropTop,cropRight,cropBottom})if(!std::isfinite(v)||v<0||v>=1)return "Invalid crop edges.";
    if(cropLeft+cropRight>=.99||cropTop+cropBottom>=.99)return "Crop must retain part of the image.";
    return {};
}
QJsonObject transformJson(const VisualTransform &t) {
    QJsonObject j{{"framing",t.framing},{"cropLeft",t.cropLeft},{"cropTop",t.cropTop},{"cropRight",t.cropRight},{"cropBottom",t.cropBottom}};
    const TransformCurve *curves[]={&t.x,&t.y,&t.scale,&t.rotation,&t.opacity};const char *names[]={"x","y","scale","rotation","opacity"};
    for(int i=0;i<5;++i){QJsonArray keys;for(const auto &k:curves[i]->keys)keys.append(QJsonObject{{"frame",k.frame},{"value",k.value},{"ease",k.ease}});j.insert(names[i],QJsonObject{{"value",curves[i]->value},{"keys",keys}});}return j;
}
bool readTransform(const QJsonObject &j,VisualTransform &t) {
    t.framing=j.value("framing").toString();
    double *edges[]={&t.cropLeft,&t.cropTop,&t.cropRight,&t.cropBottom};const char *edgeNames[]={"cropLeft","cropTop","cropRight","cropBottom"};
    for(int i=0;i<4;++i){if(!j.value(edgeNames[i]).isDouble())return false;*edges[i]=j.value(edgeNames[i]).toDouble();}
    TransformCurve *curves[]={&t.x,&t.y,&t.scale,&t.rotation,&t.opacity};const char *names[]={"x","y","scale","rotation","opacity"};
    for(int i=0;i<5;++i){const auto c=j.value(names[i]).toObject();if(!c.value("value").isDouble()||!c.value("keys").isArray())return false;
        curves[i]->value=c.value("value").toDouble();curves[i]->keys.clear();
        for(const auto &entry:c.value("keys").toArray()){const auto k=entry.toObject();const double f=k.value("frame").toDouble(-1);
            if(!k.value("frame").isDouble()||f<0||f>86400*60||f!=std::floor(f)||!k.value("value").isDouble()||!k.value("ease").isBool())return false;
            curves[i]->keys.append({int(f),k.value("value").toDouble(),k.value("ease").toBool()});}}
    return t.validate().isEmpty();
}
