#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

struct UpdatePackage {
    QString name;
    QUrl url;
    QByteArray sha256;
    qint64 size = 0;
};

QString updatePlatform();
UpdatePackage releaseUpdatePackage(const QJsonObject &release, const QString &platform, QString *error);
bool safeUpdateArchiveEntry(const QString &entry, const QString &root);
