#pragma once
#include "update_package.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <cstring>

namespace UpdateTest {
class ReleaseReply : public QNetworkReply {
public:
    ReleaseReply(const QNetworkRequest &request, int status, QByteArray body,
                 NetworkError error, int delay, QObject *parent)
        : QNetworkReply(parent), body_(std::move(body)) {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(delay, this, [this, error] {
            if (isFinished()) return;
            if (error != NoError) setError(error, "Simulated connection failure");
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }
    void abort() override {
        if (isFinished()) return;
        setError(OperationCanceledError, "Canceled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override { return body_.size() - offset_ + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 size) override {
        const auto count = qMin(size, body_.size() - offset_);
        if (!count) return -1;
        std::memcpy(data, body_.constData() + offset_, size_t(count));
        offset_ += count;
        return count;
    }
private:
    QByteArray body_;
    qint64 offset_ = 0;
};

class ReleaseNetwork : public QNetworkAccessManager {
public:
    int status = 200;
    QByteArray body;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    int requests = 0;
    int delay = 0;
    QNetworkRequest lastRequest;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        ++requests;
        lastRequest = request;
        return new ReleaseReply(request, status, body, error, delay, this);
    }
};

inline QByteArray release(const QString &tag, bool draft = false, bool prerelease = false) {
    return QJsonDocument(QJsonObject{{"tag_name", tag}, {"draft", draft}, {"prerelease", prerelease},
        {"html_url", "https://github.com/p-shen/IsaacVideoEditor/releases/tag/" + tag},
        {"name", tag}, {"body", "Release notes"}, {"assets", QJsonArray{}}}).toJson();
}

inline QByteArray downloadableRelease(const QByteArray &bytes, QByteArray checksum = {}) {
    auto object=QJsonDocument::fromJson(release("v1.3.0")).object();
    const auto platform=updatePlatform();
    const auto name="IsaacVideoEditor-v1.3.0-"+platform+(platform.startsWith("linux")?".AppImage":".zip");
    if(checksum.isEmpty()) checksum=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
    object["assets"]=QJsonArray{QJsonObject{{"name",name},{"state","uploaded"},{"size",qint64(bytes.size())},
        {"digest","sha256:"+QString::fromLatin1(checksum)}}};
    return QJsonDocument(object).toJson();
}

inline QByteArray preparedArchiveFixture() {
#if defined(Q_OS_MACOS)
    QTemporaryDir folder;
    const auto app=folder.filePath("IsaacVideoEditor.app");
    QDir().mkpath(app+"/Contents/MacOS");
    if(!QFile::copy("/usr/bin/true",app+"/Contents/MacOS/IsaacVideoEditor") ||
       !QFile::copy("/usr/bin/true",app+"/Contents/MacOS/IsaacVideoEditorUpdater")) return {};
    QFile info(app+"/Contents/Info.plist");
    if(!info.open(QIODevice::WriteOnly)) return {};
    info.write("<?xml version=\"1.0\"?><!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"><plist version=\"1.0\"><dict><key>CFBundleIdentifier</key><string>local.isaacvideo.editor</string><key>CFBundleExecutable</key><string>IsaacVideoEditor</string><key>CFBundlePackageType</key><string>APPL</string></dict></plist>");
    info.close();
    QProcess sign; sign.start("/usr/bin/codesign",{"--force","--deep","--sign","-",app});
    if(!sign.waitForFinished(15000) || sign.exitCode()) return {};
    QProcess zip; zip.start("/usr/bin/ditto",{"-c","-k","--sequesterRsrc","--keepParent",app,folder.filePath("update.zip")});
    if(!zip.waitForFinished(15000) || zip.exitCode()) return {};
    QFile archive(folder.filePath("update.zip"));
    return archive.open(QIODevice::ReadOnly)?archive.readAll():QByteArray();
#elif defined(Q_OS_LINUX)
    return QByteArray("\177ELF\002\001\001\000AI\002",11)+"fixture";
#else
    return {};
#endif
}
}
