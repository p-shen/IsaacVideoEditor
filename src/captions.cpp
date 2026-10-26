#include "captions.h"
#include <QRegularExpression>
#include <QStringConverter>
#include <algorithm>

namespace {
constexpr int frameLimit=86400*Project::fps;
QString plainCaption(QString text) {
    static const QRegularExpression breaks("<br\\s*/?>",QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags("</?(?:b|i|u|s|font)(?:\\s[^>]*)?>",QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression entities("&(amp|lt|gt|quot|apos|nbsp|#x[0-9a-f]+|#[0-9]+);",QRegularExpression::CaseInsensitiveOption);
    text.replace(breaks,"\n");text.remove(tags);
    QString result;int start=0;auto matches=entities.globalMatch(text);
    while(matches.hasNext()){
        const auto match=matches.next();const auto token=match.captured(1).toLower();QString replacement=match.captured();
        if(token=="amp")replacement="&";else if(token=="lt")replacement="<";else if(token=="gt")replacement=">";else if(token=="quot")replacement="\"";else if(token=="apos")replacement="'";else if(token=="nbsp")replacement=QChar(0xa0);
        else {bool valid=false;const uint value=token.mid(token.startsWith("#x")?2:1).toUInt(&valid,token.startsWith("#x")?16:10);
            if(valid&&value>0&&value<=0x10ffff&&!(value>=0xd800&&value<=0xdfff)){const char32_t point=value;replacement=QString::fromUcs4(&point,1);}}
        result+=text.mid(start,match.capturedStart()-start)+replacement;start=match.capturedEnd();
    }
    return result+text.mid(start);
}
QString srtText(QString text){text.replace("&","&amp;");text.replace("<","&lt;");text.replace(">","&gt;");return text;}
QString stamp(int frame) {
    const qint64 ms=(qint64(frame)*1000+30)/60;
    return QString("%1:%2:%3,%4").arg(ms/3600000,2,10,QChar('0')).arg(ms/60000%60,2,10,QChar('0')).arg(ms/1000%60,2,10,QChar('0')).arg(ms%1000,3,10,QChar('0'));
}
}
std::optional<QVector<TextOverlay>> Subtitles::parseSrt(const QByteArray &bytes,QString *error) {
    const auto fail=[&](const QString &message)->std::optional<QVector<TextOverlay>>{if(error)*error=message;return std::nullopt;};
    if(bytes.size()>128*1024*1024)return fail("SRT files must be smaller than 128 MB.");
    QStringDecoder decoder(QStringDecoder::Utf8);QString input=decoder(bytes);
    if(decoder.hasError())return fail("The SRT file must contain valid UTF-8 text.");
    if(input.startsWith(QChar::ByteOrderMark))input.remove(0,1);
    input.replace("\r\n","\n");input.replace('\r','\n');input=input.trimmed();
    if(input.isEmpty())return fail("The SRT file contains no captions.");
    const QRegularExpression number("^\\d+$");
    const QRegularExpression timing("^(\\d{2,3}):([0-5]\\d):([0-5]\\d)[,.](\\d{3})[ \\t]+-->[ \\t]+(\\d{2,3}):([0-5]\\d):([0-5]\\d)[,.](\\d{3})[ \\t]*$");
    const QRegularExpression timestampCandidate("^[ \t]*[+-]?\\d{1,3}:\\d{1,2}");
    QVector<TextOverlay> captions;
    for(const auto &block:input.split(QRegularExpression("\\n[ \\t]*\\n"),Qt::SkipEmptyParts)){
        auto lines=block.split('\n');int line=number.match(lines[0].trimmed()).hasMatch()?1:0;
        const auto match=line<lines.size()?timing.match(lines[line].trimmed()):QRegularExpressionMatch{};
        if(!match.hasMatch()){
            if(line==0&&!captions.isEmpty()&&!lines[0].contains("-->")&&!timestampCandidate.match(lines[0]).hasMatch()){
                captions.last().text+="\n\n"+plainCaption(block);
                if(captions.last().text.size()>2000)return fail("A caption exceeds 2000 characters.");
                continue;
            }
            return fail(QString("Caption %1 has an invalid SRT time interval.").arg(captions.size()+1));
        }
        const auto millis=[&](int n){return (match.captured(n).toLongLong()*3600+match.captured(n+1).toInt()*60+match.captured(n+2).toInt())*1000+match.captured(n+3).toInt();};
        const qint64 start=millis(1),end=millis(5);
        if(start<0||end<=start||end>86400000)return fail("SRT intervals must have a positive duration within 24 hours.");
        TextOverlay cue;cue.startFrame=int((start*60+500)/1000);
        const int last=std::max(cue.startFrame+1,int((end*60+500)/1000));cue.durationFrames=last-cue.startFrame;
        cue.text=plainCaption(lines.mid(line+1).join('\n'));cue.fontSize=57;cue.y=.85;
        if(cue.text.size()>2000||last>frameLimit)return fail("A caption needs at most 2000 characters and a valid frame interval.");
        captions.append(cue);if(captions.size()>10000)return fail("A project supports up to 10000 captions.");
    }
    if(captions.isEmpty())return fail("The SRT file contains no captions.");
    return captions;
}
QByteArray Subtitles::writeSrt(const Project &project) {
    QVector<int> order;for(int i=0;i<project.captions.size();++i)order.append(i);
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){return project.textStartFrame(a,true)<project.textStartFrame(b,true);});
    QString output;int number=1;
    for(int index:order){const auto &cue=project.captions[index];const int start=project.textStartFrame(index,true);
        output+=QString("%1\n%2 --> %3\n%4\n\n").arg(number++).arg(stamp(start),stamp(start+cue.durationFrames),srtText(cue.text));
    }
    return output.toUtf8();
}
