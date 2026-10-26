#pragma once
#include "project.h"
#include <QVariantMap>
bool setTextFields(TextOverlay &text,const QVariantMap &fields,QString *error=nullptr);
QVariantMap timedTextInfo(const Project &project,int index,bool caption=false);
