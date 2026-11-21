#pragma once
#include "project.h"
#include <QAbstractListModel>
#include <QVariantList>

class CaptionModel final : public QAbstractListModel {
    Q_OBJECT
public:
    explicit CaptionModel(QObject *parent=nullptr):QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent={}) const override {return parent.isValid()?0:records_.size();}
    QVariant data(const QModelIndex &,int role=Qt::DisplayRole) const override;
    QHash<int,QByteArray> roleNames() const override {return {{Qt::UserRole,"modelData"}};}
    void synchronize(const Project &);
    void updateCaption(const Project &,int index);
    QVariantList records() const {return records_;}
    QStringList warnings() const {return warnings_;}
    bool timingNeedsReview() const {return review_;}
signals:
    void summariesChanged();
private:
    QVector<TextOverlay> captions_;
    Project::TextTiming timing_;
    QVector<bool> overlaps_;
    QVariantList records_;
    QStringList warnings_;
    bool review_=false;
};
