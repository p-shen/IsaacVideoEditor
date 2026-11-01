#include "text_renderer.h"
#include <QCache>
#include <QColor>
#include <QFont>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBoundaryFinder>
#include <QTextCharFormat>
#include <QTextLayout>
#include <algorithm>
#include <memory>
#include <cmath>

namespace {
constexpr auto stateKey="isaac.text.state";
struct RenderState { QMutex mutex; QVector<TextOverlay> texts; };
struct Word { int start,length; };
struct Layout {
    QTextLayout text;
    QVector<Word> words;
    QVector<int> graphemeEnds;
    QRectF rect;
    qreal padding=0,scale=1;
    Layout(const TextOverlay &t,const QSize &canvas) {
        if(canvas.isEmpty() || t.text.isEmpty())return;
        scale=canvas.height()/1080.0;
        const QSizeF logicalCanvas(canvas.width()/scale,1080);
        QFont font(t.fontFamily);font.setPixelSize(t.fontSize);font.setBold(t.bold);font.setItalic(t.italic);
        QString content=t.uppercase?t.text.toUpper():t.text;content.replace('\n',QChar::LineSeparator);
        text.setText(content);text.setFont(font);text.setCacheEnabled(true);
        static const QRegularExpression wordPattern("\\S+",QRegularExpression::UseUnicodePropertiesOption);
        auto matches=wordPattern.globalMatch(content);
        while(matches.hasNext()){const auto match=matches.next();words.append({int(match.capturedStart()),int(match.capturedLength())});}
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme,content);
        for(int end=finder.toNextBoundary();end>=0;end=finder.toNextBoundary())graphemeEnds.append(end);
        QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        option.setAlignment(t.style=="box"?Qt::AlignLeft:Qt::AlignHCenter);text.setTextOption(option);
        text.beginLayout();qreal y=0,w=0;
        while(true){auto line=text.createLine();if(!line.isValid())break;line.setLineWidth(logicalCanvas.width()*.86);line.setPosition({0,y});y+=line.height();w=std::max(w,line.naturalTextWidth());}
        text.endLayout();
        text.beginLayout();y=0;
        while(true){auto line=text.createLine();if(!line.isValid())break;line.setLineWidth(w+1);line.setPosition({0,y});y+=line.height();}
        text.endLayout();
        padding=t.style=="neon-glow"?28:18;
        const QSizeF size(std::min(logicalCanvas.width(),w+1+2*padding),std::min(logicalCanvas.height(),y+2*padding));
        const qreal x=std::clamp(t.x*logicalCanvas.width()-size.width()/2,0.0,logicalCanvas.width()-size.width());
        const qreal top=std::clamp(t.y*logicalCanvas.height()-size.height()/2,0.0,logicalCanvas.height()-size.height());
        rect={QPointF(x,top),size};
    }
};
struct LayoutKey {
    QString text,fontFamily,style;
    QSize canvas;
    int fontSize;
    bool bold,italic,uppercase;
    double x,y;
    bool operator==(const LayoutKey &o) const {
        return text==o.text && fontFamily==o.fontFamily && style==o.style && canvas==o.canvas && fontSize==o.fontSize && bold==o.bold && italic==o.italic && uppercase==o.uppercase && x==o.x && y==o.y;
    }
};
size_t qHash(const LayoutKey &key,size_t seed=0) {
    return qHashMulti(seed,key.text,key.fontFamily,key.style,key.canvas,key.fontSize,key.bold,key.italic,key.uppercase,key.x,key.y);
}
struct LayoutCache {
    // QTextLayout owns glyph/line storage; conservatively charge 256 bytes per character,
    // plus 64 KiB per entry so at most 128 recent layouts survive each thread's 8 MiB budget.
    QCache<LayoutKey,Layout> layouts{8*1024*1024};
    Layout &get(const TextOverlay &text,const QSize &canvas,std::unique_ptr<Layout> &transient) {
        const LayoutKey key{text.text,text.fontFamily,text.style,canvas,text.fontSize,text.bold,text.italic,text.uppercase,text.x,text.y};
        if(auto *layout=layouts.object(key))return *layout;
        transient=std::make_unique<Layout>(text,canvas);
        const qint64 cost=64*1024+256*qint64(text.text.size()+text.fontFamily.size());
        if(cost>layouts.maxCost())return *transient;
        auto *layout=transient.release();layouts.insert(key,layout,int(cost));return *layout;
    }
};
LayoutCache &layoutCache(){thread_local LayoutCache cache;return cache;}
QList<QRectF> wordRects(const Layout &layout,const Word &word) {
    QList<QRectF> result;
    for(int i=0;i<layout.text.lineCount();++i){const auto line=layout.text.lineAt(i);
        const int first=std::max(word.start,line.textStart()),last=std::min(word.start+word.length,line.textStart()+line.textLength());
        if(first>=last)continue;
        const qreal left=line.cursorToX(first),right=line.cursorToX(last);
        result.append({std::min(left,right),line.y(),std::abs(right-left),line.height()});
    }
    return result;
}
void drawText(QPainter &p,const Layout &layout,const QPointF &origin,const QColor &color,const QColor &outline,qreal width,int visibleEnd,const Word *emphasis=nullptr,const QColor &accent={}) {
    QTextCharFormat format;format.setForeground(color);
    if(width>0)format.setTextOutline(QPen(outline,width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    // Qt's selection drawing uses the painter pen outside the selected range.
    p.setPen(Qt::transparent);
    layout.text.draw(&p,origin,{{0,visibleEnd,format}});
    if(emphasis&&emphasis->start<visibleEnd){
        auto highlighted=format;highlighted.setForeground(accent);
        layout.text.draw(&p,origin,{{emphasis->start,std::min(emphasis->length,visibleEnd-emphasis->start),highlighted}});
    }
}
int getImage(mlt_frame frame,uint8_t **pixels,mlt_image_format *format,int *width,int *height,int) {
    const int position=mlt_frame_pop_service_int(frame);
    auto *state=static_cast<RenderState*>(mlt_frame_pop_service(frame));
    *format=mlt_image_rgba;
    const int error=mlt_frame_get_image(frame,pixels,format,width,height,1);
    if(error || !*pixels || *format!=mlt_image_rgba)return error?error:1;
    QVector<TextOverlay> texts;{QMutexLocker lock(&state->mutex);texts=state->texts;}
    QImage image(*pixels,*width,*height,*width*4,QImage::Format_RGBA8888);
    TextRendering::paint(image,texts,position);
    return 0;
}
mlt_frame process(mlt_filter filter,mlt_frame frame) {
    auto *state=static_cast<RenderState*>(mlt_properties_get_data(MLT_FILTER_PROPERTIES(filter),stateKey,nullptr));
    const int position=mlt_filter_get_position(filter,frame);
    bool active=false;
    {QMutexLocker lock(&state->mutex);for(const auto &t:state->texts)active |= !t.text.isEmpty() && position>=t.startFrame && position<t.startFrame+t.durationFrames;}
    if(active){mlt_frame_push_service(frame,state);mlt_frame_push_service_int(frame,position);mlt_frame_push_get_image(frame,getImage);}
    return frame;
}
}
namespace TextRendering {
QRectF bounds(const TextOverlay &text,const QSize &canvas){
    auto &cache=layoutCache();std::unique_ptr<Layout> transient;
    const auto &layout=cache.get(text,canvas,transient);return {layout.rect.topLeft()*layout.scale,layout.rect.size()*layout.scale};
}
void paint(QImage &image,const QVector<TextOverlay> &texts,int frame) {
    QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::TextAntialiasing);
    for(const auto &t:texts){
        if(frame<t.startFrame || frame>=t.startFrame+t.durationFrames || t.text.isEmpty())continue;
        auto &cache=layoutCache();std::unique_ptr<Layout> transient;
        auto *layoutPtr=&cache.get(t,image.size(),transient);if(layoutPtr->rect.isEmpty())continue;
        const int elapsed=frame-t.startFrame;
        double entry=std::min(1.0,elapsed/std::max(1.0,std::min(18.0,(t.durationFrames-1)/2.0)));
        std::unique_ptr<Layout> wordTransient;
        const bool wordPop=t.animation=="word-pop"&&t.durationFrames>=layoutPtr->words.size();
        // A cue shorter than its word count shows the full text rather than dropping words.
        if(wordPop&&!layoutPtr->words.isEmpty()){
            const int wordCount=layoutPtr->words.size();
            const int index=std::min(wordCount-1,int(qint64(elapsed)*wordCount/t.durationFrames));
            const auto word=layoutPtr->words[index];auto cue=t;cue.text=layoutPtr->text.text().mid(word.start,word.length);
            layoutPtr=&cache.get(cue,image.size(),wordTransient);
            const double wordFrames=t.durationFrames/double(wordCount);
            entry=std::min(1.0,(elapsed-index*wordFrames)/std::max(1.0,std::min(10.0,wordFrames/2)));
        }
        auto &layout=*layoutPtr;
        p.save();p.scale(layout.scale,layout.scale);p.setClipRect(layout.rect);
        if((t.fade||t.animation=="fade")&&t.durationFrames>2){const double ramp=std::min(27.0,(t.durationFrames-1)/2.0);p.setOpacity(std::clamp(std::min(frame-t.startFrame,t.startFrame+t.durationFrames-1-frame)/ramp,0.0,1.0));}
        const double ease=1-std::pow(1-entry,3);
        double zoom=1;
        if(t.animation=="pop"||t.animation=="bounce"||wordPop)zoom=.82+.18*ease;
        if(t.animation=="pulse")zoom=.975+.025*std::cos(elapsed*2*3.141592653589793/90);
        if(zoom!=1){p.translate(layout.rect.center());p.scale(zoom,zoom);p.translate(-layout.rect.center());}
        if(t.animation=="slide")p.translate(0,layout.padding*.75*(1-ease));
        if(t.animation=="bounce")p.translate(0,-layout.padding*.5*std::sin(entry*2*3.141592653589793)*(1-entry));
        if(t.style=="box"){p.setPen(Qt::NoPen);p.setBrush(QColor(10,24,23,220));p.drawRoundedRect(layout.rect,8,8);}
        const QPointF origin=layout.rect.topLeft()+QPointF(layout.padding,layout.padding);
        if(t.style=="rounded-label"){
            p.setPen(Qt::NoPen);p.setBrush(QColor("#ffffff"));
            for(int i=0;i<layout.text.lineCount();++i){auto line=layout.text.lineAt(i);p.drawRoundedRect(line.naturalTextRect().translated(origin).adjusted(-12,-4,12,4),10,10);}
        }
        int visibleEnd=layout.text.text().size();
        if(t.animation=="typewriter"&&!layout.graphemeEnds.isEmpty()){
            const int count=std::clamp(int((elapsed+1)*layout.graphemeEnds.size()/std::max(1.0,std::min(120.0,t.durationFrames*.65))),1,int(layout.graphemeEnds.size()));
            visibleEnd=layout.graphemeEnds[count-1];
        }
        const Word *emphasis=nullptr;QColor accent("#ffe34f");
        if(t.style=="bold-highlight"&&!layout.words.isEmpty())emphasis=&layout.words.last();
        if(t.animation=="karaoke"&&!layout.words.isEmpty()){
            emphasis=&layout.words[std::min(int(layout.words.size())-1,int(qint64(elapsed)*layout.words.size()/t.durationFrames))];accent=QColor("#17200c");
            p.setPen(Qt::NoPen);p.setBrush(QColor("#c4ff57"));
            for(auto rect:wordRects(layout,*emphasis))p.drawRoundedRect(rect.translated(origin).adjusted(-6,0,6,0),8,8);
        }
        const bool legacy=t.style=="clean"||t.style=="playful"||t.style=="box";
        if(legacy&&t.animation!="typewriter"&&t.animation!="karaoke"){
            if(t.shadow){p.setPen(QColor(0,0,0,210));layout.text.draw(&p,origin+QPointF(2,3));}
            p.setPen(QColor(t.color));layout.text.draw(&p,origin);
        }else{
            const bool outlined=t.style=="classic-outline"||t.style=="bold-highlight"||t.style=="word-pop"||t.style=="comic-sticker";
            const QColor outline=t.style=="comic-sticker"?QColor("#ffffff"):QColor("#101014");
            const qreal stroke=outlined?std::max(2.0,t.fontSize*.065):0;
            if(t.shadow)drawText(p,layout,origin+QPointF(t.style=="comic-sticker"?6:2,t.style=="comic-sticker"?7:3),QColor(0,0,0,210),QColor(0,0,0,210),stroke,visibleEnd);
            if(t.style=="neon-glow")for(int width:{18,12,6}){QColor glow(t.color);glow.setAlpha(35);drawText(p,layout,origin,glow,glow,width,visibleEnd);}
            drawText(p,layout,origin,QColor(t.color),outline,stroke,visibleEnd,emphasis,accent);
        }
        p.restore();
    }
}
bool attach(Mlt::Tractor &timeline,const QVector<TextOverlay> &texts) {
    auto filter=mlt_filter_new();if(!filter)return false;
    auto *state=new RenderState;state->texts=texts;
    mlt_properties_set_data(MLT_FILTER_PROPERTIES(filter),stateKey,state,0,[](void *p){delete static_cast<RenderState*>(p);},nullptr);
    filter->process=process;
    const bool attached=mlt_service_attach(timeline.get_service(),filter)==0;
    if(attached)mlt_properties_set_data(timeline.get_properties(),stateKey,state,0,nullptr,nullptr);
    mlt_filter_close(filter);
    return attached;
}
void update(Mlt::Tractor &timeline,const QVector<TextOverlay> &texts) {
    auto *state=static_cast<RenderState*>(mlt_properties_get_data(timeline.get_properties(),stateKey,nullptr));
    if(state){QMutexLocker lock(&state->mutex);state->texts=texts;}
}
}
