#pragma once
#include <QVector>
#include <QString>
#include <QJsonObject>
struct TransformKey {
    int frame=0; double value=0; bool ease=false;
    bool operator==(const TransformKey &o) const {return frame==o.frame&&value==o.value&&ease==o.ease;}
};
struct TransformCurve {
    double value=0;
    QVector<TransformKey> keys;
    double valueAt(int sourceFrame) const;
    void setKey(int sourceFrame,double v,bool ease=false);
    void edit(int sourceFrame,double v);
    bool operator==(const TransformCurve &o) const {return value==o.value&&keys==o.keys;}
};
struct VisualTransform {
    TransformCurve x{.5},y{.5},scale{1},rotation{0},opacity{1};
    QString framing="fit";
    double cropLeft=0,cropTop=0,cropRight=0,cropBottom=0;
    bool operator==(const VisualTransform &o) const;
    bool operator!=(const VisualTransform &o) const {return !(*this==o);}
    QString validate() const;
};
QJsonObject transformJson(const VisualTransform &t);
bool readTransform(const QJsonObject &json,VisualTransform &t);
