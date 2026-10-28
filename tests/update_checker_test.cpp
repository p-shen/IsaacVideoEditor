#include "update_checker.h"
#include "update_package.h"
#include "update_fixtures.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>
#include <cstring>

using namespace UpdateTest;

class UpdateCheckerTest : public QObject {
    Q_OBJECT
private slots:
    void init() { QCoreApplication::setApplicationVersion("1.2.0"); }
    void selectsOnlyVerifiedPackagesForTheRequestedPlatform() {
        auto object=QJsonDocument::fromJson(release("v1.3.0")).object();
        QJsonArray assets;
        for(const auto &platform:{QString("macos-arm64"),QString("windows-x86_64"),QString("linux-x86_64")}) {
            const auto name="IsaacVideoEditor-v1.3.0-"+platform+(platform.startsWith("linux")?".AppImage":".zip");
            assets.append(QJsonObject{{"name",name},{"state","uploaded"},{"size",12345},
                {"digest","sha256:"+QString(64,'a')},{"browser_download_url","https://untrusted.example/update"}});
        }
        object["assets"]=assets;
        QString error;
        auto package=releaseUpdatePackage(object,"macos-arm64",&error);
        QCOMPARE(package.name,QString("IsaacVideoEditor-v1.3.0-macos-arm64.zip"));
        QCOMPARE(package.url,QUrl("https://github.com/p-shen/IsaacVideoEditor/releases/download/v1.3.0/IsaacVideoEditor-v1.3.0-macos-arm64.zip"));
        QCOMPARE(package.sha256,QByteArray(64,'a')); QCOMPARE(package.size,12345);
        QVERIFY(releaseUpdatePackage(object,"macos-x86_64",&error).url.isEmpty());
        QVERIFY(releaseUpdatePackage(object,{},&error).url.isEmpty());
        auto asset=assets[0].toObject(); asset["digest"]=QJsonValue(); assets[0]=asset; object["assets"]=assets;
        QVERIFY(releaseUpdatePackage(object,"macos-arm64",&error).url.isEmpty()); QVERIFY(!error.isEmpty());
    }
    void rejectsUnsafeArchivePaths_data() {
        QTest::addColumn<QString>("entry"); QTest::addColumn<bool>("safe");
        QTest::newRow("normal")<<"IsaacVideoEditor.app/Contents/MacOS/IsaacVideoEditor"<<true;
        QTest::newRow("directory")<<"IsaacVideoEditor.app/"<<true;
        QTest::newRow("resource metadata")<<"__MACOSX/IsaacVideoEditor.app/._Contents"<<true;
        QTest::newRow("escape")<<"IsaacVideoEditor.app/../../important"<<false;
        QTest::newRow("absolute")<<"/IsaacVideoEditor.app/Contents"<<false;
        QTest::newRow("drive")<<"IsaacVideoEditor.app/C:/important"<<false;
        QTest::newRow("backslash")<<"IsaacVideoEditor.app/..\\important"<<false;
        QTest::newRow("unexpected root")<<"other.app/Contents"<<false;
    }
    void rejectsUnsafeArchivePaths() {
        QFETCH(QString,entry); QFETCH(bool,safe);
        QCOMPARE(safeUpdateArchiveEntry(entry,"IsaacVideoEditor.app"),safe);
    }
    void rejectsDamagedDownloads_data() {
        QTest::addColumn<QByteArray>("checksum"); QTest::addColumn<QByteArray>("body"); QTest::addColumn<int>("status");
        const QByteArray good="expected package bytes";
        const auto hash=QCryptographicHash::hash(good,QCryptographicHash::Sha256).toHex();
        QTest::newRow("checksum mismatch")<<QByteArray(64,'a')<<good<<200;
        QTest::newRow("short download")<<hash<<QByteArray("short")<<200;
        QTest::newRow("excess bytes")<<hash<<QByteArray(100,'x')<<200;
        QTest::newRow("HTTP failure")<<hash<<QByteArray()<<503;
    }
    void rejectsDamagedDownloads() {
        QFETCH(QByteArray,checksum); QFETCH(QByteArray,body); QFETCH(int,status);
        ReleaseNetwork network; network.body=downloadableRelease("expected package bytes",checksum);
        UpdateChecker checker(network); QSignalSpy restart(&checker,&UpdateChecker::restartRequested);
        checker.check(); QTRY_VERIFY(checker.canDownload());
        network.body=body; network.status=status;
        checker.download(); QVERIFY(checker.busy());
        QTRY_COMPARE(checker.state(),UpdateChecker::Error);
        QVERIFY(!checker.readyToInstall()); QCOMPARE(restart.count(),0);
    }
    void canceledDownloadCanBeRetried() {
        ReleaseNetwork network; network.body=downloadableRelease("archive");
        UpdateChecker checker(network); checker.check(); QTRY_VERIFY(checker.canDownload());
        network.body="archive";
        checker.download(); QCOMPARE(checker.state(),UpdateChecker::Downloading);
        checker.download(); QCOMPARE(network.requests,2);
        checker.check(); QCOMPARE(network.requests,2);
        checker.cancelDownload();
        QTRY_VERIFY(checker.canDownload()); QVERIFY(!checker.busy());
        checker.download(); QCOMPARE(network.requests,3);
        QTRY_COMPARE(checker.state(),UpdateChecker::Error);
        QVERIFY(!checker.readyToInstall());
    }
    void verifiedPackageIsPreparedBeforeInstallation() {
        const auto archive=preparedArchiveFixture();
        if(archive.isEmpty()) QSKIP("A native signed-archive fixture is unavailable on this platform");
        ReleaseNetwork network; network.body=downloadableRelease(archive);
        UpdateChecker checker(network); QSignalSpy restart(&checker,&UpdateChecker::restartRequested);
        checker.check(); QTRY_VERIFY(checker.canDownload());
        network.body=archive; checker.download();
        QTRY_VERIFY_WITH_TIMEOUT(!checker.busy(),20000);
        QVERIFY2(checker.readyToInstall(),qPrintable(checker.message()));
        QCOMPARE(checker.progress(),1.0); QCOMPARE(restart.count(),0);
        // This test executable is outside an installed bundle/AppImage/portable folder.
        checker.install(); QCOMPARE(checker.state(),UpdateChecker::Error); QCOMPARE(restart.count(),0);
    }
    void comparesVersions_data() {
        QTest::addColumn<QString>("installed");
        QTest::addColumn<QString>("tag");
        QTest::addColumn<bool>("available");
        QTest::newRow("new patch") << "1.2.0" << "v1.2.1" << true;
        QTest::newRow("numeric ordering") << "1.2.0" << "v1.10.0" << true;
        QTest::newRow("same version") << "v1.2.0" << "1.2.0" << false;
        QTest::newRow("equivalent components") << "1.2" << "v1.2.0" << false;
        QTest::newRow("older release") << "2.0.0" << "v1.2.0" << false;
        QTest::newRow("installed prerelease") << "v1.2.0-rc.1" << "v1.2.0" << true;
        QTest::newRow("build metadata") << "1.2.0+local" << "v1.2.0+release" << false;
    }
    void comparesVersions() {
        QFETCH(QString, installed); QFETCH(QString, tag); QFETCH(bool, available);
        QCoreApplication::setApplicationVersion(installed);
        ReleaseNetwork network; network.body = release(tag);
        UpdateChecker checker(network);
        checker.check();
        QVERIFY(checker.checking());
        QTRY_VERIFY(!checker.checking());
        QCOMPARE(checker.updateAvailable(), available);
        QCOMPARE(checker.state(), available ? UpdateChecker::Available : UpdateChecker::UpToDate);
        QVERIFY(!checker.message().isEmpty());
        if (available) {
            QCOMPARE(checker.latestVersion(), tag);
            QCOMPARE(checker.releaseUrl().host(), QString("github.com"));
            QVERIFY(checker.releaseUrl().path().startsWith("/p-shen/IsaacVideoEditor/releases/tag/"));
        } else QVERIFY(checker.releaseUrl().isEmpty());
    }
    void invalidResponses_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("invalid JSON") << QByteArray("<html>Unavailable</html>");
        QTest::newRow("missing tag") << QByteArray("{}");
        QTest::newRow("array") << QByteArray("[]");
        QTest::newRow("unsupported tag") << release("latest");
        QTest::newRow("trailing junk") << release("v1.3.0oops");
        QTest::newRow("numeric overflow") << release("v999999999999999999.0.0");
        QTest::newRow("draft") << release("v2.0.0", true);
        QTest::newRow("prerelease") << release("v2.0.0-beta.1", false, true);
        QTest::newRow("mislabeled prerelease") << release("v2.0.0-beta.1");
    }
    void invalidResponses() {
        QFETCH(QByteArray, body);
        ReleaseNetwork network; network.body = body;
        UpdateChecker checker(network); checker.check();
        QTRY_VERIFY(!checker.checking());
        QCOMPARE(checker.state(), UpdateChecker::Error);
        QVERIFY(!checker.updateAvailable()); QVERIFY(checker.releaseUrl().isEmpty());
        QVERIFY(!checker.message().isEmpty());
    }
    void connectionFailures_data() {
        QTest::addColumn<int>("status"); QTest::addColumn<int>("error"); QTest::addColumn<bool>("noRelease");
        QTest::newRow("no published releases") << 404 << int(QNetworkReply::ContentNotFoundError) << true;
        QTest::newRow("rate limit") << 403 << int(QNetworkReply::ContentAccessDenied) << false;
        QTest::newRow("too many requests") << 429 << int(QNetworkReply::UnknownContentError) << false;
        QTest::newRow("server error") << 500 << int(QNetworkReply::InternalServerError) << false;
        QTest::newRow("offline") << 0 << int(QNetworkReply::HostNotFoundError) << false;
        QTest::newRow("timeout") << 0 << int(QNetworkReply::TimeoutError) << false;
        QTest::newRow("TLS failure") << 0 << int(QNetworkReply::SslHandshakeFailedError) << false;
    }
    void connectionFailures() {
        QFETCH(int, status); QFETCH(int, error); QFETCH(bool, noRelease);
        ReleaseNetwork network; network.status = status; network.error = QNetworkReply::NetworkError(error);
        UpdateChecker checker(network); checker.check();
        QTRY_VERIFY(!checker.checking());
        QCOMPARE(checker.state(), noRelease ? UpdateChecker::NoRelease : UpdateChecker::Error);
        QVERIFY(!checker.updateAvailable()); QVERIFY(checker.releaseUrl().isEmpty());
        QVERIFY(!checker.message().isEmpty());
    }
    void duplicateChecksAndRetries() {
        ReleaseNetwork network; network.body = release("v1.3.0");
        UpdateChecker checker(network); QSignalSpy changed(&checker, &UpdateChecker::stateChanged);
        checker.check(); checker.check();
        QCOMPARE(network.requests, 1);
        QCOMPARE(network.lastRequest.url(), QUrl("https://api.github.com/repos/p-shen/IsaacVideoEditor/releases/latest"));
        QVERIFY(!network.lastRequest.rawHeader("User-Agent").isEmpty());
        QTRY_COMPARE(checker.state(), UpdateChecker::Available);
        network.status = 503; network.error = QNetworkReply::ServiceUnavailableError;
        checker.check();
        QVERIFY(checker.releaseUrl().isEmpty()); QVERIFY(checker.latestVersion().isEmpty());
        QTRY_COMPARE(checker.state(), UpdateChecker::Error);
        network.status = 200; network.error = QNetworkReply::NoError; network.body = release("v1.2.0");
        checker.check();
        QTRY_COMPARE(checker.state(), UpdateChecker::UpToDate);
        QCOMPARE(network.requests, 3); QCOMPARE(changed.size(), 6);
    }
    void releaseLinkCannotBeReplacedByResponse() {
        ReleaseNetwork network;
        auto object = QJsonDocument::fromJson(release("v1.3.0")).object();
        object["html_url"] = "file:///tmp/untrusted";
        network.body = QJsonDocument(object).toJson();
        UpdateChecker checker(network); checker.check();
        QTRY_COMPARE(checker.state(), UpdateChecker::Available);
        QCOMPARE(checker.releaseUrl(), QUrl("https://github.com/p-shen/IsaacVideoEditor/releases/tag/v1.3.0"));
    }
};

QTEST_GUILESS_MAIN(UpdateCheckerTest)
#include "update_checker_test.moc"
