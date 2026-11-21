#include "caption_model.h"
#include "text_style.h"
#include <algorithm>

QVariant CaptionModel::data(const QModelIndex &index,int role) const {
    return index.isValid()&&index.row()>=0&&index.row()<records_.size()&&(role==Qt::UserRole||role==Qt::DisplayRole)?records_[index.row()]:QVariant{};
}
void CaptionModel::synchronize(const Project &project) {
    const auto timing=project.textTiming();
    const bool reset=records_.size()!=project.captions.size();
    bool intervalsChanged=reset||timing.captionStarts!=timing_.captionStarts;
    if(!intervalsChanged)for(int i=0;i<project.captions.size();++i)intervalsChanged|=captions_[i].durationFrames!=project.captions[i].durationFrames;
    auto overlaps=overlaps_;
    if(intervalsChanged){
        QVector<int> order;order.reserve(project.captions.size());for(int i=0;i<project.captions.size();++i)order.append(i);
        std::stable_sort(order.begin(),order.end(),[&](int a,int b){return timing.captionStarts[a]<timing.captionStarts[b];});
        overlaps.fill(false,order.size());int farthest=-1,end=-1;
        for(int i:order){const int start=timing.captionStarts[i],last=start+project.captions[i].durationFrames;
            if(start<end){overlaps[i]=true;if(farthest>=0)overlaps[farthest]=true;}
            if(last>end){farthest=i;end=last;}
        }
    }
    if(reset)beginResetModel();
    QVariantList records;records.reserve(project.captions.size());QVector<int> changed;
    bool review=false,overlap=false,clamp=false;
    for(int i=0;i<project.captions.size();++i){const auto &cue=project.captions[i];
        const bool clamped=timing.duration-timing.captionStarts[i]<cue.durationFrames;
        const bool reuse=!reset&&captions_[i]==cue&&timing_.captionStarts[i]==timing.captionStarts[i]&&
            records_[i].toMap()["effectiveFrames"].toInt()==std::clamp(timing.duration-timing.captionStarts[i],0,cue.durationFrames)&&records_[i].toMap()["overlap"].toBool()==overlaps[i];
        QVariant item;
        if(reuse)item=records_[i];
        else {auto record=timedTextInfo(project,i,true,&timing);record.insert("overlap",overlaps[i]);item=record;if(!reset&&item!=records_[i])changed.append(i);}
        records.append(item);review|=cue.timingNeedsReview;overlap|=overlaps[i];clamp|=clamped;
    }
    QStringList warnings;
    if(review)warnings.append("Timing needs review — Project-time captions were retained.");
    if(overlap)warnings.append("Caption intervals overlap; all text is retained.");
    if(clamp)warnings.append("Past project end: video is clamped; SRT retains timing.");
    const bool summaries=reset||warnings_!=warnings||review_!=review;
    captions_=project.captions;timing_=timing;overlaps_=std::move(overlaps);records_=std::move(records);warnings_=std::move(warnings);review_=review;
    if(reset)endResetModel();else for(int row:changed)emit dataChanged(index(row),index(row),{Qt::UserRole});
    if(summaries)emit summariesChanged();
}
void CaptionModel::updateCaption(const Project &project,int row) {
    if(row<0||row>=project.captions.size())return;
    if(captions_.size()!=project.captions.size()){synchronize(project);return;}
    const auto &before=captions_[row],&after=project.captions[row];
    if(before.startFrame!=after.startFrame||before.durationFrames!=after.durationFrames||before.hostId!=after.hostId||before.hostSourceFrame!=after.hostSourceFrame||before.timingNeedsReview!=after.timingNeedsReview){synchronize(project);return;}
    auto record=timedTextInfo(project,row,true,&timing_);record.insert("overlap",records_[row].toMap()["overlap"]);
    captions_[row]=after;
    if(records_[row]!=record){records_[row]=record;emit dataChanged(index(row),index(row),{Qt::UserRole});}
}
