#pragma once
#include "project.h"
#include <QVariantMap>
#include <QVariantList>
QVariantList textPresetOptions();
bool validTextStyle(const QString &style);
bool validTextAnimation(const QString &animation);
bool setTextFields(TextOverlay &text,const QVariantMap &fields,QString *error=nullptr);
QVariantMap timedTextInfo(const Project &project,int index,bool caption=false);
