#include "update_checker.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QVersionNumber>

namespace {
struct Version {
    QVersionNumber number;
    bool prerelease = false;
};

Version parseVersion(const QString &tag) {
    static const QRegularExpression pattern(
        "\\A[vV]?([0-9]+(?:\\.[0-9]+){1,3})(?:-([0-9A-Za-z.-]+))?(?:\\+[0-9A-Za-z.-]+)?\\z");
    if (tag.size() > 100) return {};
    const auto match = pattern.match(tag);
    if (!match.hasMatch()) return {};
    qsizetype end = 0;
    const auto numeric = match.captured(1);
    const auto number = QVersionNumber::fromString(numeric, &end);
    if (end != numeric.size()) return {};
    return {number.normalized(), !match.captured(2).isEmpty()};
}
}

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent), network_(new QNetworkAccessManager(this)) {}

UpdateChecker::UpdateChecker(QNetworkAccessManager &network, QObject *parent)
    : QObject(parent), network_(&network) {}

UpdateChecker::~UpdateChecker() {
    if (reply_) { reply_->disconnect(this); reply_->abort(); }
    if (process_) {
        process_->disconnect(this);
        if (process_->state() != QProcess::NotRunning) { process_->kill(); process_->waitForFinished(1000); }
    }
}

QString UpdateChecker::currentVersion() const {
    return QCoreApplication::applicationVersion();
}

void UpdateChecker::finish(State state, const QString &message) {
    state_ = state;
    message_ = message;
    emit stateChanged();
}

void UpdateChecker::check() {
    if (busy()) return;
    prepared_.reset();
    package_ = {};
    latestVersion_.clear();
    releaseUrl_.clear();
    finish(Checking, tr("Checking GitHub for the latest release…"));

    QNetworkRequest request(QUrl("https://api.github.com/repos/p-shen/IsaacVideoEditor/releases/latest"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "IsaacVideoEditor");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setTransferTimeout(15000);
    auto *reply = network_->get(request);
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    deadline->start(15000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, deadline] {
        deadline->stop();
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            finish(NoRelease, tr("No published stable release was found on GitHub."));
            return;
        }
        if (status == 403 || status == 429) {
            finish(Error, tr("GitHub is limiting update checks. Please try again later."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            finish(Error, tr("Could not check for updates. Check your internet connection and try again."));
            return;
        }

        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &error);
        const auto object = document.object();
        const auto tag = object.value("tag_name").toString();
        const auto latest = parseVersion(tag);
        const auto installed = parseVersion(currentVersion());
        if (error.error != QJsonParseError::NoError || !document.isObject() ||
            !object.value("draft").isBool() || object.value("draft").toBool() ||
            !object.value("prerelease").isBool() || object.value("prerelease").toBool() ||
            latest.number.isNull() || latest.prerelease) {
            finish(Error, tr("GitHub returned an unrecognized release. Please try again later."));
            return;
        }
        if (installed.number.isNull()) {
            finish(Error, tr("This build has no recognizable version, so it cannot check for updates."));
            return;
        }
        const int comparison = QVersionNumber::compare(latest.number, installed.number);
        if (comparison > 0 || (comparison == 0 && installed.prerelease)) {
            latestVersion_ = tag;
            // Build the link from the known repository rather than trusting a response URL.
            releaseUrl_ = QUrl::fromEncoded("https://github.com/p-shen/IsaacVideoEditor/releases/tag/" +
                                           QUrl::toPercentEncoding(tag));
            QString packageError;
            package_ = releaseUpdatePackage(object, updatePlatform(), &packageError);
            finish(Available, tr("IsaacVideoEditor %1 is available. ").arg(tag) +
                (package_.url.isEmpty() ? packageError : tr("Download the update, then restart to install it.")));
        } else {
            finish(UpToDate, tr("You’re up to date. No newer stable release is available."));
        }
    });
}

void UpdateChecker::downloadFailed(const QString &message) {
    downloadFile_.close();
    prepared_.reset();
    hash_.reset();
    finish(Error, message);
}

void UpdateChecker::readDownload() {
    if (!reply_ || !transferError_.isEmpty() || canceled_) return;
    while (reply_->bytesAvailable() > 0) {
        const auto bytes = reply_->read(65536);
        if (bytes.isEmpty()) break;
        if (received_ + bytes.size() > package_.size || downloadFile_.write(bytes) != bytes.size()) {
            transferError_ = tr("The update could not be saved, or its size does not match the release. Your installed app has not changed.");
            reply_->abort();
            return;
        }
        hash_->addData(bytes);
        received_ += bytes.size();
        progress_ = double(received_) / package_.size;
        emit progressChanged();
    }
}

void UpdateChecker::download() {
    if (!canDownload()) return;
    prepared_ = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/isaac-update-XXXXXX");
    if (!prepared_->isValid()) { downloadFailed(tr("Could not create a temporary folder for the update.")); return; }
    downloadFile_.setFileName(prepared_->filePath("archive"));
    if (!downloadFile_.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        downloadFailed(tr("Could not save the update. Check available disk space and try again.")); return;
    }
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    received_ = 0; progress_ = 0; canceled_ = false; transferError_.clear();
    emit progressChanged();
    finish(Downloading, tr("Downloading the update… You can keep this window open or continue editing."));
    QNetworkRequest request(package_.url);
    request.setRawHeader("User-Agent", "IsaacVideoEditor");
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    reply_ = network_->get(request);
    auto *reply = reply_.data();
    reply->setReadBufferSize(262144);
    connect(reply, &QNetworkReply::redirected, this, [this, reply](const QUrl &url) {
        const auto host = url.host().toLower();
        if (url.scheme() == "https" && url.userInfo().isEmpty() &&
            (host == "github.com" || host == "release-assets.githubusercontent.com" || host == "objects.githubusercontent.com")) {
            reply->redirectAllowed();
        } else {
            transferError_ = tr("The download was redirected to an untrusted location. Your installed app has not changed.");
            reply->abort();
        }
    });
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    deadline->start(600000);
    connect(reply, &QIODevice::readyRead, this, &UpdateChecker::readDownload);
    connect(reply, &QNetworkReply::finished, this, [this, reply, deadline] {
        deadline->stop();
        readDownload();
        reply_.clear();
        reply->deleteLater();
        downloadFile_.close();
        if (canceled_) {
            prepared_.reset(); hash_.reset();
            finish(Available, tr("Download canceled. You can download the update again when you’re ready."));
            return;
        }
        if (!transferError_.isEmpty()) { downloadFailed(transferError_); return; }
        if (reply->error() != QNetworkReply::NoError ||
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            downloadFailed(tr("The download was interrupted. Check your connection and try again. Your installed app has not changed.")); return;
        }
        if (received_ != package_.size || hash_->result().toHex() != package_.sha256) {
            downloadFailed(tr("The update failed its size or SHA-256 integrity check. Please download it again. Your installed app has not changed.")); return;
        }
        hash_.reset();
        finish(Preparing, tr("Download verified. Preparing the update…"));
        preparePackage();
    });
}

void UpdateChecker::cancelDownload() {
    if (state_ != Downloading || !reply_) return;
    canceled_ = true;
    reply_->abort();
}

void UpdateChecker::runPreparation(const QString &program, const QStringList &arguments,
                                  std::function<void(const QByteArray &)> completed) {
    auto *process = new QProcess(this);
    process_ = process;
    auto output = std::make_shared<QByteArray>();
    connect(process, &QProcess::readyReadStandardOutput, this, [process, output] {
        output->append(process->readAllStandardOutput());
        if (output->size() > 8388608) process->kill();
    });
    connect(process, &QProcess::readyReadStandardError, process, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || process_ != process) return;
        process_.clear(); process->deleteLater();
        downloadFailed(tr("The operating system could not prepare the update. Your installed app has not changed."));
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, output, completed](int code, QProcess::ExitStatus status) {
        if (process_ != process) return;
        output->append(process->readAllStandardOutput());
        process_.clear(); process->deleteLater();
        if (code || status != QProcess::NormalExit || output->size() > 8388608) {
            downloadFailed(tr("The update package could not be unpacked or verified. Your installed app has not changed.")); return;
        }
        completed(*output);
    });
    auto *deadline = new QTimer(process);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, process, &QProcess::kill);
    deadline->start(120000);
    process->start(program, arguments);
}

void UpdateChecker::preparePackage() {
    const auto archive = prepared_->filePath("archive");
#if defined(Q_OS_LINUX)
    QFile image(archive);
    if (!image.open(QIODevice::ReadOnly)) { downloadFailed(tr("Could not read the downloaded update.")); return; }
    const auto header = image.read(11); image.close();
    if (header.size() != 11 || !header.startsWith(QByteArray("\177ELF", 4)) || header.mid(8,3) != QByteArray("AI\002",3)) {
        downloadFailed(tr("The downloaded file is not an AppImage. Your installed app has not changed.")); return;
    }
    QFile::setPermissions(archive, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                  QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    payloadReady(archive);
#else
# if defined(Q_OS_MACOS)
    const QString tar = "/usr/bin/tar", root = "IsaacVideoEditor.app";
# elif defined(Q_OS_WIN)
    const QString tar = QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/tar.exe");
    const QString root = "IsaacVideoEditor";
# else
    downloadFailed(tr("In-app installation is not supported on this operating system.")); return;
# endif
    runPreparation(tar, {"-tf", archive}, [this, tar, root, archive](const QByteArray &listing) {
        const auto entries = QString::fromUtf8(listing).split('\n', Qt::SkipEmptyParts);
        if (entries.isEmpty()) { downloadFailed(tr("The update archive is empty.")); return; }
        for (const auto &entry : entries) {
            if (!safeUpdateArchiveEntry(entry, root)) {
                downloadFailed(tr("The update archive contains an unsafe file path. Your installed app has not changed.")); return;
            }
        }
        const auto unpacked = prepared_->filePath("unpacked");
        if (!QDir().mkdir(unpacked)) { downloadFailed(tr("Could not create the update's extraction folder.")); return; }
        runPreparation(tar, {"--no-same-owner", "-xf", archive, "-C", unpacked},
                       [this, unpacked, root](const QByteArray &) {
            const auto payload = QDir(unpacked).filePath(root);
            const auto canonical = QFileInfo(payload).canonicalFilePath();
            if (QFileInfo(payload).isSymLink() || canonical.isEmpty()) {
                downloadFailed(tr("The archive is missing the application.")); return;
            }
            QDirIterator files(payload, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (files.hasNext()) {
                const auto info = QFileInfo(files.next());
                const auto resolved = info.canonicalFilePath();
                if (resolved.isEmpty() || (resolved != canonical && !resolved.startsWith(canonical + '/'))) {
                    downloadFailed(tr("The update contains a link outside its application folder. Your installed app has not changed.")); return;
                }
            }
# if defined(Q_OS_MACOS)
            const QDir app(payload);
            if (!QFileInfo(app.filePath("Contents/MacOS/IsaacVideoEditor")).isExecutable() ||
                !QFileInfo(app.filePath("Contents/MacOS/IsaacVideoEditorUpdater")).isExecutable()) {
                downloadFailed(tr("The update is missing the editor or its installer.")); return;
            }
            runPreparation("/usr/libexec/PlistBuddy", {"-c", "Print :CFBundleIdentifier", app.filePath("Contents/Info.plist")},
                           [this, payload](const QByteArray &identifier) {
                if (identifier.trimmed() != "local.isaacvideo.editor") {
                    downloadFailed(tr("The update belongs to a different application.")); return;
                }
                runPreparation("/usr/bin/codesign", {"--verify", "--deep", "--strict", payload},
                               [this, payload](const QByteArray &) { payloadReady(payload); });
            });
# else
            const QDir app(payload);
            if (!QFileInfo(app.filePath("IsaacVideoEditor.exe")).isFile() ||
                !QFileInfo(app.filePath("IsaacVideoEditorUpdater.exe")).isFile() ||
                !QFileInfo(app.filePath(".isaac-portable")).isFile() ||
                !QFileInfo(app.filePath("isaac-package-files.txt")).isFile()) {
                downloadFailed(tr("The update is missing files required for a portable installation.")); return;
            }
            payloadReady(payload);
# endif
        });
    });
#endif
}

void UpdateChecker::payloadReady(const QString &path) {
    if (!QDir().rename(path, prepared_->filePath("payload"))) {
        downloadFailed(tr("Could not prepare the verified application for installation.")); return;
    }
    QFile marker(prepared_->filePath("ready"));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::NewOnly) || marker.write("IsaacVideoEditor update 1\n") != 26) {
        downloadFailed(tr("Could not finish preparing the update.")); return;
    }
    marker.close();
    QFile::remove(prepared_->filePath("archive"));
    finish(Ready, tr("The update is ready. Restart and Install will ask you to save your work, then replace and reopen the app."));
}

void UpdateChecker::install() {
    if (!readyToInstall() || !prepared_) return;
    const auto applicationDirectory = QCoreApplication::applicationDirPath();
    QString target, relative, helper;
#if defined(Q_OS_MACOS)
    target = QFileInfo(QDir(applicationDirectory).filePath("../..")).canonicalFilePath();
    relative = "Contents/MacOS/IsaacVideoEditor";
    helper = QDir(applicationDirectory).filePath("IsaacVideoEditorUpdater");
    if (!target.endsWith(".app")) target.clear();
#elif defined(Q_OS_WIN)
    target = QFileInfo(applicationDirectory).canonicalFilePath();
    relative = "IsaacVideoEditor.exe";
    helper = QDir(applicationDirectory).filePath("IsaacVideoEditorUpdater.exe");
    if (!QFileInfo(QDir(target).filePath(".isaac-portable")).isFile() ||
        !QFileInfo(QDir(target).filePath("isaac-package-files.txt")).isFile()) target.clear();
#elif defined(Q_OS_LINUX)
    target = QFileInfo(qEnvironmentVariable("APPIMAGE")).canonicalFilePath();
    relative = ".";
    helper = QDir(applicationDirectory).filePath("IsaacVideoEditorUpdater");
    if (!target.endsWith(".AppImage")) target.clear();
#endif
    if (target.isEmpty() || !QFileInfo(helper).isExecutable()) {
        finish(Error, tr("This build cannot replace itself. Use the packaged application in a writable folder to enable in-app installation.")); return;
    }
    helperTemporary_ = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/isaac-installer-XXXXXX");
    const auto copiedHelper = helperTemporary_->filePath(QFileInfo(helper).fileName());
    if (!helperTemporary_->isValid() || !QFile::copy(helper, copiedHelper)) {
        finish(Error, tr("Could not start the update installer. Your installed app has not changed.")); return;
    }
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto &name : {"LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH", "QML_IMPORT_PATH"}) environment.remove(name);
    QProcess launcher;
    launcher.setProcessEnvironment(environment);
    launcher.setProgram(copiedHelper);
    launcher.setArguments({"--install", QString::number(QCoreApplication::applicationPid()),
                           QFileInfo(prepared_->path()).canonicalFilePath(), target, relative});
    launcher.setWorkingDirectory(helperTemporary_->path());
    if (!launcher.startDetached()) {
        finish(Error, tr("Could not start the update installer. Your installed app has not changed.")); return;
    }
    helperTemporary_->setAutoRemove(false);
    finish(Installing, tr("Preparing the restart… Your current app will stay open until the installer is ready."));
    auto *poll = new QTimer(this);
    poll->setInterval(100);
    auto elapsed = std::make_shared<int>(0);
    connect(poll, &QTimer::timeout, this, [this, poll, elapsed] {
        QFile error(prepared_->filePath("error.txt"));
        if (error.open(QIODevice::ReadOnly)) {
            poll->stop(); poll->deleteLater();
            finish(Error, tr("The update could not be installed. Your current app has not changed. ") + QString::fromUtf8(error.read(4096)));
        } else if (QFileInfo::exists(prepared_->filePath("armed"))) {
            poll->stop(); poll->deleteLater();
            prepared_->setAutoRemove(false);
            emit restartRequested();
        } else if ((*elapsed += 100) >= 120000) {
            poll->stop(); poll->deleteLater();
            finish(Error, tr("The installer did not become ready. Your current app is still in place. Please try again."));
        }
    });
    poll->start();
}
