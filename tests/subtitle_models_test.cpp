#include "subtitle_models.h"
#include <QCryptographicHash>
#include <QFile>
#include <QNetworkReply>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class FixtureReply final : public QNetworkReply {
public:
    FixtureReply(const QNetworkRequest &request,QByteArray bytes,int delay,QObject *parent):QNetworkReply(parent),bytes_(bytes){
        setRequest(request);setUrl(request.url());setAttribute(QNetworkRequest::HttpStatusCodeAttribute,200);open(ReadOnly);
        QTimer::singleShot(delay,this,[this]{if(isFinished())return;available_=true;emit readyRead();setFinished(true);emit finished();});
    }
    void abort() override {if(isFinished())return;setError(OperationCanceledError,"Canceled");setFinished(true);emit finished();}
    qint64 bytesAvailable() const override {return (available_?bytes_.size()-position_:0)+QNetworkReply::bytesAvailable();}
protected:
    qint64 readData(char *data,qint64 size) override {const auto count=std::min(size,available_?qint64(bytes_.size())-position_:0);if(count<=0)return -1;memcpy(data,bytes_.constData()+position_,count);position_+=count;return count;}
private:
    QByteArray bytes_;qint64 position_=0;bool available_=false;
};
class FixtureNetwork final : public QNetworkAccessManager {
public:
    QByteArray body="MODEL!";int delay=0;
protected:
    QNetworkReply *createRequest(Operation,const QNetworkRequest &request,QIODevice*) override {return new FixtureReply(request,body,delay,this);}
};
class SubtitleModelsTest final : public QObject {
    Q_OBJECT
    QList<SubtitleModelSpec> catalog() const {
        const auto hash=QCryptographicHash::hash("MODEL!",QCryptographicHash::Sha256).toHex();
        return {{"large-v3","Large",6,hash,QUrl("https://huggingface.co/fixture/large")},{"large-v3-turbo","Turbo",6,hash,QUrl("https://huggingface.co/fixture/turbo")}};
    }
    static QByteArray read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
private slots:
    void initTestCase(){QCoreApplication::setOrganizationName("IsaacSubtitleTests");QCoreApplication::setApplicationName("Models");}
    void init(){QSettings().remove("subtitles/model");}
    void catalogAndSelection(){
        const auto models=subtitleModelCatalog();QCOMPARE(models.size(),2);for(const auto &m:models){QCOMPARE(m.sha256.size(),64);QVERIFY(m.bytes>1000000000);QCOMPARE(m.url.scheme(),"https");QVERIFY(m.url.path().contains("5359861c739e955e79d9a303bcbc70fb988958b1"));}
        QTemporaryDir dir;FixtureNetwork network;SubtitleModels m(nullptr,dir.path(),&network,catalog());
        m.setSelectedModel("large-v3");QCOMPARE(m.selectedModel(),"large-v3");m.setSelectedModel("../../untrusted");QCOMPARE(m.selectedModel(),"large-v3");
        SubtitleModels other(nullptr,dir.path(),&network,catalog());QCOMPARE(other.selectedModel(),"large-v3");QVERIFY(!m.ready());
    }
    void verifiedDownloadAndCachedCheck(){
        QTemporaryDir dir;FixtureNetwork network;SubtitleModels m(nullptr,dir.path(),&network,catalog());m.download();
        QTRY_VERIFY(!m.busy());QVERIFY2(m.ready(),qPrintable(m.status()));QCOMPARE(read(m.modelPath()),QByteArray("MODEL!"));
        SubtitleModels cached(nullptr,dir.path(),&network,catalog());QTRY_VERIFY(!cached.busy());QTRY_VERIFY(cached.ready());
    }
    void badDigestAndCancellationPreservePreviousFile(){
        QTemporaryDir dir;FixtureNetwork network;SubtitleModels m(nullptr,dir.path(),&network,catalog());
        QFile previous(m.modelPath());QVERIFY(previous.open(QIODevice::WriteOnly));previous.write("OLDold");previous.close();
        network.body="BROKEN";m.download();QTRY_VERIFY(!m.busy());QVERIFY(!m.ready());QVERIFY(m.status().contains("verification",Qt::CaseInsensitive));QCOMPARE(read(m.modelPath()),QByteArray("OLDold"));
        network.body="MODEL!";network.delay=1000;m.download();QVERIFY(m.busy());m.cancel();QTRY_VERIFY(!m.busy());QVERIFY(!m.ready());QCOMPARE(read(m.modelPath()),QByteArray("OLDold"));
    }
    void corruptCacheDoesNotBecomeReady(){
        QTemporaryDir dir;FixtureNetwork network;SubtitleModels m(nullptr,dir.path(),&network,catalog());
        QFile f(m.modelPath());QVERIFY(f.open(QIODevice::WriteOnly));f.write("BROKEN");f.close();m.refresh();QTRY_VERIFY(!m.busy());QVERIFY(!m.ready());
    }
    void deletingOrReplacingVerifiedModelInvalidatesReadinessAndCanRecover(){
        QTemporaryDir dir;FixtureNetwork network;SubtitleModels m(nullptr,dir.path(),&network,catalog());m.download();QTRY_VERIFY(m.ready());
        QVERIFY(QFile::remove(m.modelPath()));QVERIFY(!m.ready());m.download();QTRY_VERIFY(m.ready());
        QFile f(m.modelPath());QVERIFY(f.open(QIODevice::WriteOnly));f.write("BROKEN");QVERIFY(f.setFileTime(QDateTime::currentDateTimeUtc().addSecs(5),QFileDevice::FileModificationTime));f.close();
        QVERIFY(!m.ready());m.refresh();QTRY_VERIFY(!m.busy());QVERIFY(!m.ready());m.download();QTRY_VERIFY(m.ready());QCOMPARE(read(m.modelPath()),QByteArray("MODEL!"));
    }
};
QTEST_GUILESS_MAIN(SubtitleModelsTest)
#include "subtitle_models_test.moc"
