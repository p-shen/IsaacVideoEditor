#pragma once

#include <QObject>
#include <QUrl>
#include <QFile>
#include <QPointer>
#include <functional>
#include <memory>
#include "update_package.h"

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTemporaryDir;
class QCryptographicHash;

class UpdateChecker : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(QUrl releaseUrl READ releaseUrl NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool canDownload READ canDownload NOTIFY stateChanged)
    Q_PROPERTY(bool readyToInstall READ readyToInstall NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
public:
    enum State { Idle, Checking, UpToDate, Available, NoRelease, Error, Downloading, Preparing, Ready, Installing };
    Q_ENUM(State)

    explicit UpdateChecker(QObject *parent = nullptr);
    explicit UpdateChecker(QNetworkAccessManager &network, QObject *parent = nullptr);
    ~UpdateChecker() override;
    State state() const { return state_; }
    bool checking() const { return state_ == Checking; }
    bool updateAvailable() const { return state_ == Available || state_ >= Downloading; }
    bool busy() const { return checking() || state_ == Downloading || state_ == Preparing || state_ == Installing; }
    bool canDownload() const { return state_ == Available && !package_.url.isEmpty(); }
    bool readyToInstall() const { return state_ == Ready; }
    double progress() const { return progress_; }
    QString message() const { return message_; }
    QString currentVersion() const;
    QString latestVersion() const { return latestVersion_; }
    QUrl releaseUrl() const { return releaseUrl_; }
    Q_INVOKABLE void check();
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void install();
signals:
    void stateChanged();
    void progressChanged();
    void restartRequested();
private:
    void finish(State state, const QString &message);
    void downloadFailed(const QString &message);
    void readDownload();
    void preparePackage();
    void payloadReady(const QString &path);
    void runPreparation(const QString &program, const QStringList &arguments,
                        std::function<void(const QByteArray &)> completed);
    QNetworkAccessManager *network_;
    State state_ = Idle;
    QString message_, latestVersion_;
    QUrl releaseUrl_;
    UpdatePackage package_;
    QPointer<QNetworkReply> reply_;
    QPointer<QProcess> process_;
    std::unique_ptr<QTemporaryDir> prepared_, helperTemporary_;
    std::unique_ptr<QCryptographicHash> hash_;
    QFile downloadFile_;
    qint64 received_ = 0;
    double progress_ = 0;
    bool canceled_ = false;
    QString transferError_;
};
