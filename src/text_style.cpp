#include "text_style.h"
#include <QFontDatabase>
#include <cmath>
bool validTextStyle(const QString &style) {
    return QStringList{"clean","playful","box","classic-outline","rounded-label","bold-highlight","karaoke-highlight","word-pop","typewriter","neon-glow","editorial-serif","handwritten-note","comic-sticker"}.contains(style);
}
bool validTextAnimation(const QString &animation) {
    return QStringList{"none","fade","slide","pop","bounce","typewriter","karaoke","word-pop","pulse"}.contains(animation);
}
QVariantList textPresetOptions() {
    const auto font=[](const QStringList &families){for(const auto &family:families)if(QFontDatabase::hasFamily(family))return family;return families.last();};
    const auto sans=font({"Arial","DejaVu Sans","Sans Serif"});
    const auto heavy=font({"Impact","Arial Black","DejaVu Sans","Sans Serif"});
    const auto mono=font({"Courier New","DejaVu Sans Mono","Monospace"});
    const auto serif=font({"Georgia","DejaVu Serif","Serif"});
    const auto hand=font({"Chalkboard SE","Comic Sans MS","Comic Sans","Cursive"});
    const auto rounded=font({"Arial Rounded MT Bold","DejaVu Sans","Sans Serif"});
    const auto option=[](const QString &id,const QString &name,const QString &family,int size,const QString &color,const QString &motion,bool bold=true,bool shadow=true,bool italic=false,bool uppercase=false){
        return QVariantMap{{"id",id},{"name",name},{"fontFamily",family},{"fontSize",size},{"color",color},{"animation",motion},{"bold",bold},{"shadow",shadow},{"italic",italic},{"uppercase",uppercase},{"fade",false}};
    };
    return {option("classic-outline","Classic Outline",sans,72,"#ffffff","none"),
        option("rounded-label","Rounded Label",sans,64,"#17171c","slide",true,false),
        option("bold-highlight","Bold Highlight",heavy,88,"#ffffff","pop",true,true,false,true),
        option("karaoke-highlight","Karaoke Highlight",sans,70,"#ffffff","karaoke",true,false),
        option("word-pop","Word Pop",heavy,104,"#ffffff","word-pop"),
        option("typewriter","Typewriter",mono,64,"#ffffff","typewriter",false),
        option("neon-glow","Neon Glow",sans,80,"#74f7ff","pulse",true,false),
        option("editorial-serif","Editorial Serif",serif,80,"#fff3df","fade",false,true,true),
        option("handwritten-note","Handwritten Note",hand,76,"#fff3df","typewriter",false),
        option("comic-sticker","Comic Sticker",rounded,96,"#ffdf4f","bounce",true,true,false,true),
        option("clean","Clean",sans,84,"#ffffff","none"),
        option("playful","Playful",rounded,84,"#ffe395","none"),
        option("box","Background box",sans,57,"#ffffff","none",true,false)};
}
bool setTextFields(TextOverlay &t,const QVariantMap &fields,QString *error) {
    const auto fail=[&](const QString &message){if(error)*error=message;return false;};
    if(fields.contains("preset")) {
        const auto id=fields["preset"].toString();QVariantMap preset;
        for(const auto &value:textPresetOptions())if(value.toMap()["id"].toString()==id){preset=value.toMap();break;}
        if(preset.isEmpty())return fail("Unknown text preset.");
        preset.remove("id");preset.remove("name");preset.insert("style",id);
        if(!setTextFields(t,preset,error))return false;
    }
    if(fields.contains("style")){
        t.style=fields["style"].toString();
        if(!validTextStyle(t.style))return fail("Unknown text appearance.");
        if(t.style=="playful"){t.fontFamily=QFontDatabase::hasFamily("Arial Rounded MT Bold")?"Arial Rounded MT Bold":"Arial";t.color="#ffe395";t.bold=true;t.shadow=true;}
        else if(t.style=="clean"){t.fontFamily="Arial";t.color="#ffffff";t.shadow=true;}
        else if(t.style=="box"){t.color="#ffffff";t.shadow=false;}
    }
    for(auto it=fields.cbegin();it!=fields.cend();++it){const auto &key=it.key();const auto &v=it.value();
        if(key=="text")t.text=v.toString();else if(key=="fontFamily")t.fontFamily=v.toString();else if(key=="color")t.color=v.toString();else if(key=="style"||key=="preset"){}
        else if(key=="animation") {if(!validTextAnimation(v.toString()))return fail("Unknown text animation.");t.animation=v.toString();}
        else if(key=="bold"||key=="shadow"||key=="fade"||key=="italic"||key=="uppercase"){
            if(v.metaType().id()!=QMetaType::Bool)return fail("Text switches need a true or false value.");
            if(key=="bold")t.bold=v.toBool();else if(key=="shadow")t.shadow=v.toBool();else if(key=="fade")t.fade=v.toBool();else if(key=="italic")t.italic=v.toBool();else t.uppercase=v.toBool();
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
        {"fontSize",t.fontSize},{"bold",t.bold},{"shadow",t.shadow},{"fade",t.fade},{"animation",t.animation},{"italic",t.italic},{"uppercase",t.uppercase},{"startFrame",project.textStartFrame(index,caption)},
        {"durationFrames",t.durationFrames},{"durationSeconds",t.durationFrames/60.0},{"effectiveFrames",frames},{"x",t.x},{"y",t.y},
        {"attached",!t.hostId.isEmpty()},{"hostId",t.hostId},{"hostSourceFrame",t.hostSourceFrame},{"timingNeedsReview",t.timingNeedsReview},
        {"clamped",frames<t.durationFrames}};
}
