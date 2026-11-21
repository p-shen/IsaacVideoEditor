#pragma once
#include "project.h"
#include <QVariantMap>
#include <QVariantList>
QVariantList textPresetOptions();
bool validTextStyle(const QString &style);
bool validTextAnimation(const QString &animation);
bool setTextFields(TextOverlay &text,const QVariantMap &fields,QString *error=nullptr);
QVariantMap timedTextInfo(const Project &project,int index,bool caption=false,const Project::TextTiming *timing=nullptr);
bool validTextSpans(const TextOverlay &text);
bool setTextRangeFields(TextOverlay &text,int start,int end,const QVariantMap &fields,QString *error=nullptr);
QVariantMap textRangeFields(const TextOverlay &text,int start,int end);
void replaceTextContent(TextOverlay &text,const QString &content);
