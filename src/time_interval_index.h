#pragma once
#include <QVector>
#include <algorithm>

// Half-open intervals. Results retain input order, including arbitrary overlaps.
// Rebuilds are caller-owned; concurrent const queries need no mutable cursor.
class TimeIntervalIndex {
public:
    struct Interval {qint64 start=0,end=0;};
    TimeIntervalIndex()=default;
    explicit TimeIntervalIndex(const QVector<Interval> &intervals){reset(intervals);}
    void reset(const QVector<Interval> &intervals) {
        nodes_.clear();nodes_.reserve(intervals.size());
        for(int i=0;i<intervals.size();++i){const auto &interval=intervals[i];
            if(interval.end>interval.start)nodes_.append({interval.start,interval.end,interval.end,i});
        }
        std::sort(nodes_.begin(),nodes_.end(),[](const Node &a,const Node &b){return a.start<b.start;});
        if(!nodes_.isEmpty())build(0,nodes_.size());
    }
    QVector<int> query(qint64 start,qint64 end) const {
        QVector<int> result;
        if(end>start)collect(0,nodes_.size(),start,end,result);
        std::sort(result.begin(),result.end());return result;
    }
    QVector<int> at(qint64 frame) const {
        QVector<int> result;collectPoint(0,nodes_.size(),frame,result);
        std::sort(result.begin(),result.end());return result;
    }
private:
    struct Node {qint64 start,end,maxEnd;int index;};
    QVector<Node> nodes_;
    qint64 build(int first,int last) {
        const int middle=first+(last-first)/2;auto &node=nodes_[middle];
        if(first<middle)node.maxEnd=std::max(node.maxEnd,build(first,middle));
        if(middle+1<last)node.maxEnd=std::max(node.maxEnd,build(middle+1,last));
        return node.maxEnd;
    }
    void collect(int first,int last,qint64 start,qint64 end,QVector<int> &result) const {
        if(first>=last)return;
        const int middle=first+(last-first)/2;const auto &node=nodes_[middle];
        if(node.maxEnd<=start||nodes_[first].start>=end)return;
        collect(first,middle,start,end,result);
        if(node.start<end&&node.end>start)result.append(node.index);
        if(node.start<end)collect(middle+1,last,start,end,result);
    }
    void collectPoint(int first,int last,qint64 frame,QVector<int> &result) const {
        if(first>=last)return;
        const int middle=first+(last-first)/2;const auto &node=nodes_[middle];
        if(node.maxEnd<=frame||nodes_[first].start>frame)return;
        collectPoint(first,middle,frame,result);
        if(node.start<=frame&&node.end>frame)result.append(node.index);
        if(node.start<=frame)collectPoint(middle+1,last,frame,result);
    }
};
