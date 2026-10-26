#include "text_style.h"
#include <QFontDatabase>
#include <cmath>
bool setTextFields(TextOverlay &t,const QVariantMap &fields,QString *error) {
    const auto fail=[&](const QString &message){if(error)*error=message;return false;};
    if(fields.contains("style")){
        t.style=fields["style"].toString();
        if(t.style=="playful"){t.fontFamily=QFontDatabase::hasFamily("Arial Rounded MT Bold")?"Arial Rounded MT Bold":"Arial";t.color="#ffe395";t.bold=true;t.shadow=true;}
        else if(t.style=="clean"){t.fontFamily="Arial";t.color="#ffffff";t.shadow=true;}
        else if(t.style=="box"){t.color="#ffffff";t.shadow=false;}
    }
    for(auto it=fields.cbegin();it!=fields.cend();++it){const auto &key=it.key();const auto &v=it.value();
        if(key=="text")t.text=v.toString();else if(key=="fontFamily")t.fontFamily=v.toString();else if(key=="color")t.color=v.toString();else if(key=="style"){}
        else if(key=="bold"||key=="shadow"||key=="fade"){
            if(v.metaType().id()!=QMetaType::Bool)return fail("Text switches need a true or false value.");
            if(key=="bold")t.bold=v.toBool();else if(key=="shadow")t.shadow=v.toBool();else t.fade=v.toBool();
        }else if(key=="fontSize"||key=="durationFrames"||key=="x"||key=="y"){
            bool ok=false;const double n=v.toDouble(&ok);
            if(!ok||!std::isfinite(n)||std::abs(n)>86400*60||((key=="fontSize"||key=="durationFrames")&&n!=std::floor(n)))return fail("Invalid text size, duration, or position.");
            if(key=="fontSize")t.fontSize=int(n);else if(key=="durationFrames")t.durationFrames=int(n);else if(key=="x")t.x=n;else t.y=n;
        }else return fail("Unknown text setting.");
    }
    return true;
}
QVariantMap timedTextInfo(const Project &project,int index,bool caption) {
    const auto &list=caption?project.captions:project.textOverlays;if(index<0||index>=list.size())return {};
    const auto &t=list[index];const int frames=project.textFrames(index,caption);
    return {{"index",index},{"text",t.text},{"fontFamily",t.fontFamily},{"style",t.style},{"color",t.color},
        {"fontSize",t.fontSize},{"bold",t.bold},{"shadow",t.shadow},{"fade",t.fade},{"startFrame",project.textStartFrame(index,caption)},
        {"durationFrames",t.durationFrames},{"durationSeconds",t.durationFrames/60.0},{"effectiveFrames",frames},{"x",t.x},{"y",t.y},
        {"attached",!t.hostId.isEmpty()},{"hostId",t.hostId},{"hostSourceFrame",t.hostSourceFrame},{"timingNeedsReview",t.timingNeedsReview},
        {"clamped",frames<t.durationFrames}};
}
