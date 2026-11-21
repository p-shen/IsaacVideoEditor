#include "text_style.h"
#include <QFontDatabase>
#include <QColor>
#include <QTextBoundaryFinder>
#include <cmath>
namespace {
bool boundary(const QString &text,int position) {
    if(position<0||position>text.size())return false;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme,text);finder.setPosition(position);return finder.isAtBoundary();
}
QVector<TextSpan> textCells(const TextOverlay &text) {
    QVector<TextSpan> cells(text.text.size());
    for(const auto &span:text.spans)for(int i=span.start;i<span.start+span.length;++i){cells[i].color=span.color;cells[i].bold=span.bold;}
    return cells;
}
bool sameStyle(const TextSpan &a,const TextSpan &b){return a.color==b.color&&a.bold==b.bold;}
QVector<TextSpan> compactSpans(const QVector<TextSpan> &cells) {
    QVector<TextSpan> result;
    for(int i=0;i<cells.size();){int end=i+1;while(end<cells.size()&&sameStyle(cells[i],cells[end]))++end;
        if(!cells[i].color.isEmpty()||cells[i].bold.has_value())result.append({i,end-i,cells[i].color,cells[i].bold});i=end;}
    return result;
}
}
bool validTextSpans(const TextOverlay &text) {
    if(text.spans.size()>text.text.size())return false;
    int end=0;
    for(const auto &span:text.spans){
        if(span.start<end||span.length<1||span.start>text.text.size()-span.length||
            (span.color.isEmpty()&&!span.bold.has_value())||(!span.color.isEmpty()&&!QColor(span.color).isValid())||
            !boundary(text.text,span.start)||!boundary(text.text,span.start+span.length))return false;
        end=span.start+span.length;
    }
    return true;
}
bool setTextRangeFields(TextOverlay &text,int start,int end,const QVariantMap &fields,QString *error) {
    const auto fail=[&](const QString &message){if(error)*error=message;return false;};
    if(start>=end||!boundary(text.text,start)||!boundary(text.text,end)||!validTextSpans(text))return fail("Select complete caption characters to format.");
    if(fields.isEmpty())return fail("Choose a selected-text color or bold setting.");
    for(auto i=fields.begin();i!=fields.end();++i){
        if(i.key()=="color"){if(i.value().metaType().id()!=QMetaType::QString||!QColor(i.value().toString()).isValid())return fail("Invalid selected-text color.");}
        else if(i.key()=="bold"){if(i.value().metaType().id()!=QMetaType::Bool)return fail("Bold needs a true or false value.");}
        else return fail("Selected text supports color and bold.");
    }
    auto cells=textCells(text);
    for(int i=start;i<end;++i){if(fields.contains("color"))cells[i].color=QColor(fields["color"].toString()).name();if(fields.contains("bold"))cells[i].bold=fields["bold"].toBool();}
    text.spans=compactSpans(cells);return true;
}
QVariantMap textRangeFields(const TextOverlay &text,int start,int end) {
    if(start>=end||!boundary(text.text,start)||!boundary(text.text,end)||!validTextSpans(text))return {};
    const auto cells=textCells(text);const bool bold=cells[start].bold.value_or(text.bold);
    const auto color=cells[start].color.isEmpty()?text.color:cells[start].color;bool sameBold=true,sameColor=true;
    for(int i=start+1;i<end;++i){sameBold&=cells[i].bold.value_or(text.bold)==bold;sameColor&=QColor(cells[i].color.isEmpty()?text.color:cells[i].color)==QColor(color);}
    return {{"bold",sameBold&&bold},{"color",sameColor?QColor(color).name():QString{}},{"mixedBold",!sameBold},{"mixedColor",!sameColor}};
}
void replaceTextContent(TextOverlay &text,const QString &content) {
    if(text.text==content)return;
    if(text.spans.isEmpty()){text.text=content;return;}
    int first=0;while(first<text.text.size()&&first<content.size()&&text.text[first]==content[first])++first;
    while(first>0&&(!boundary(text.text,first)||!boundary(content,first)))--first;
    int oldEnd=text.text.size(),newEnd=content.size();
    while(oldEnd>first&&newEnd>first&&text.text[oldEnd-1]==content[newEnd-1]){--oldEnd;--newEnd;}
    while(oldEnd<text.text.size()&&(!boundary(text.text,oldEnd)||!boundary(content,newEnd))){++oldEnd;++newEnd;}
    const auto before=textCells(text);QVector<TextSpan> after(content.size());
    for(int i=0;i<first;++i)after[i]=before[i];
    for(int i=oldEnd;i<before.size();++i)after[newEnd+i-oldEnd]=before[i];
    bool inherit=oldEnd>first;
    if(inherit)for(int i=first+1;i<oldEnd;++i)inherit&=sameStyle(before[first],before[i]);
    else inherit=first>0&&first<before.size()&&sameStyle(before[first-1],before[first]);
    if(inherit)for(int i=first;i<newEnd;++i)after[i]=before[first];
    text.text=content;text.spans=compactSpans(after);
}
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
        if(key=="text")replaceTextContent(t,v.toString());else if(key=="fontFamily")t.fontFamily=v.toString();else if(key=="color")t.color=v.toString();else if(key=="style"||key=="preset"){}
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
QVariantMap timedTextInfo(const Project &project,int index,bool caption,const Project::TextTiming *timing) {
    const auto &list=caption?project.captions:project.textOverlays;if(index<0||index>=list.size())return {};
    const auto &t=list[index];
    const int start=timing?(caption?timing->captionStarts:timing->titleStarts)[index]:project.textStartFrame(index,caption);
    const int frames=std::clamp((timing?timing->duration:project.durationFrames())-start,0,t.durationFrames);
    return {{"index",index},{"text",t.text},{"fontFamily",t.fontFamily},{"style",t.style},{"color",t.color},
        {"fontSize",t.fontSize},{"bold",t.bold},{"shadow",t.shadow},{"fade",t.fade},{"animation",t.animation},{"italic",t.italic},{"uppercase",t.uppercase},{"startFrame",start},
        {"durationFrames",t.durationFrames},{"durationSeconds",t.durationFrames/60.0},{"effectiveFrames",frames},{"x",t.x},{"y",t.y},
        {"attached",!t.hostId.isEmpty()},{"hostId",t.hostId},{"hostSourceFrame",t.hostSourceFrame},{"timingNeedsReview",t.timingNeedsReview},
        {"clamped",frames<t.durationFrames}};
}
