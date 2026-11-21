#pragma once
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>
#include <atomic>
#include <memory>

struct SubtitleModelSpec {QString id,label;qint64 bytes;QByteArray sha256;QUrl url;};
QList<SubtitleModelSpec> subtitleModelCatalog();

class SubtitleModels final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList choices READ choices CONSTANT)
    Q_PROPERTY(QString selectedModel READ selectedModel WRITE setSelectedModel NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit SubtitleModels(QObject *parent=nullptr,const QString &directory={},QNetworkAccessManager *network=nullptr,
                            const QList<SubtitleModelSpec> &catalog=subtitleModelCatalog());
    ~SubtitleModels() override;
    QVariantList choices() const;
    QString selectedModel() const {return selected_;}
    void setSelectedModel(const QString &id);
    bool busy() const {return busy_;}
    bool ready() const;
    double progress() const {return progress_;}
    QString status() const {return status_;}
    QString modelPath() const;
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void refresh();
signals:
    void changed();
private:
    SubtitleModelSpec selectedSpec() const;
    void readDownload();
    void finishDownload();
    void finish(bool ready,const QString &message);
    QString directory_,selected_,status_,transferError_;
    QList<SubtitleModelSpec> catalog_;
    QNetworkAccessManager *network_;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QSaveFile> file_;
    std::unique_ptr<QCryptographicHash> hash_;
    QThreadPool verification_;
    std::shared_ptr<std::atomic_bool> canceled_=std::make_shared<std::atomic_bool>(false);
    quint64 verificationRevision_=0;
    qint64 received_=0;
    qint64 verifiedModified_=-1;
    double progress_=0;
    bool busy_=false,ready_=false;
};
