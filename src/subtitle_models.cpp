#include "subtitle_models.h"
#include <QDir>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

QList<SubtitleModelSpec> subtitleModelCatalog(){
    const QString root="https://huggingface.co/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/";
    return {{"large-v3","Whisper large-v3 · accuracy",3095033483LL,"64d182b440b98d5203c4f9bd541544d84c605196c4f7b845dfa11fb23594d1e2",QUrl(root+"ggml-large-v3.bin")},
            {"large-v3-turbo","Whisper Turbo · speed",1624555275LL,"1fc70f774d38eb169993ac391eea357ef47c88757ef72ee5943879b7e8e2bc69",QUrl(root+"ggml-large-v3-turbo.bin")}};
}
SubtitleModels::SubtitleModels(QObject *parent,const QString &directory,QNetworkAccessManager *network,const QList<SubtitleModelSpec> &catalog)
    :QObject(parent),directory_(directory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/subtitle-models":directory),catalog_(catalog),network_(network?network:new QNetworkAccessManager(this)){
    verification_.setMaxThreadCount(1);
#ifdef Q_OS_MACOS
    const QString initial="large-v3-turbo";
#else
    const QString initial="large-v3";
#endif
    selected_=QSettings().value("subtitles/model",initial).toString();
    bool known=false;for(const auto &m:catalog_)known|=m.id==selected_;
    if(!known&&!catalog_.isEmpty())selected_=catalog_.first().id;
    status_="Download this model once to transcribe offline.";
    QTimer::singleShot(0,this,&SubtitleModels::refresh);
}
SubtitleModels::~SubtitleModels(){
    *canceled_=true;++verificationRevision_;
    if(reply_){disconnect(reply_,nullptr,this,nullptr);reply_->abort();reply_->deleteLater();}
    verification_.waitForDone();
}
QVariantList SubtitleModels::choices() const {
    QVariantList list;for(const auto &m:catalog_)list.append(QVariantMap{{"id",m.id},{"label",m.label},{"bytes",m.bytes}});return list;
}
SubtitleModelSpec SubtitleModels::selectedSpec() const {for(const auto &m:catalog_)if(m.id==selected_)return m;return {};}
QString SubtitleModels::modelPath() const {return QDir(directory_).filePath("ggml-"+selected_+".bin");}
bool SubtitleModels::ready() const {
    if(!ready_)return false;
    const QFileInfo file(modelPath());return file.isFile()&&!file.isSymLink()&&file.size()==selectedSpec().bytes&&file.lastModified().toMSecsSinceEpoch()==verifiedModified_;
}
void SubtitleModels::setSelectedModel(const QString &id){
    if(busy_||selected_==id)return;
    for(const auto &m:catalog_)if(m.id==id){selected_=id;QSettings().setValue("subtitles/model",id);ready_=false;progress_=0;refresh();emit changed();return;}
}
void SubtitleModels::finish(bool ready,const QString &message){busy_=false;ready_=ready;verifiedModified_=ready?QFileInfo(modelPath()).lastModified().toMSecsSinceEpoch():-1;status_=message;progress_=ready?1:0;emit changed();}
void SubtitleModels::refresh(){
    if(busy_||catalog_.isEmpty())return;
    ready_=false;const auto spec=selectedSpec();const auto path=modelPath();const QFileInfo info(path);
    if(!info.isFile()||info.isSymLink()||info.size()!=spec.bytes){finish(false,"Download this model once to transcribe offline.");return;}
    busy_=true;progress_=-1;status_="Checking the downloaded model…";canceled_=std::make_shared<std::atomic_bool>(false);const auto revision=++verificationRevision_;const auto stopped=canceled_;
    emit changed();
    const auto modified=info.lastModified().toMSecsSinceEpoch();
    verification_.start(QRunnable::create([this,path,spec,revision,stopped,modified]{
        QFile file(path);QCryptographicHash hash(QCryptographicHash::Sha256);bool ok=file.open(QIODevice::ReadOnly);
        while(ok&&!file.atEnd()&&!stopped->load()){const auto bytes=file.read(1024*1024);if(bytes.isEmpty()&&file.error()!=QFileDevice::NoError){ok=false;break;}hash.addData(bytes);}
        const bool valid=ok&&!stopped->load()&&hash.result().toHex()==spec.sha256;
        QMetaObject::invokeMethod(this,[this,valid,revision,modified,spec,path]{
            if(revision!=verificationRevision_)return;const QFileInfo now(path);const bool unchanged=now.isFile()&&!now.isSymLink()&&now.size()==spec.bytes&&now.lastModified().toMSecsSinceEpoch()==modified;
            finish(valid&&unchanged,valid&&unchanged?"Model ready. Transcription stays on this computer.":"Model verification failed. Download it again.");
        },Qt::QueuedConnection);
    }));
}
void SubtitleModels::download(){
    if(busy_||ready()||catalog_.isEmpty())return;
    const auto spec=selectedSpec();
    if(!QDir().mkpath(directory_)||QFileInfo(modelPath()).isSymLink()){finish(false,"Cannot save the subtitle model in the application data folder.");return;}
    file_=std::make_unique<QSaveFile>(modelPath());file_->setDirectWriteFallback(false);
    if(!file_->open(QIODevice::WriteOnly)){finish(false,file_->errorString());file_.reset();return;}
    hash_=std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);received_=0;progress_=0;busy_=true;ready_=false;canceled_=std::make_shared<std::atomic_bool>(false);transferError_.clear();status_="Downloading the model…";emit changed();
    QNetworkRequest request(spec.url);request.setRawHeader("User-Agent","IsaacVideoEditor");request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::UserVerifiedRedirectPolicy);
    reply_=network_->get(request);auto *reply=reply_.data();reply->setReadBufferSize(262144);
    connect(reply,&QNetworkReply::redirected,this,[this,reply](const QUrl &url){
        const auto host=url.host().toLower();
        if(url.scheme()=="https"&&url.userInfo().isEmpty()&&(host=="huggingface.co"||host.endsWith(".huggingface.co")||host.endsWith(".hf.co")))reply->redirectAllowed();
        else {transferError_="The model download was redirected to an unsupported location.";reply->abort();}
    });
    connect(reply,&QIODevice::readyRead,this,&SubtitleModels::readDownload);
    connect(reply,&QNetworkReply::finished,this,&SubtitleModels::finishDownload);
}
void SubtitleModels::readDownload(){
    if(!reply_||!file_||!transferError_.isEmpty()||canceled_->load())return;
    const auto expected=selectedSpec().bytes;
    while(reply_->bytesAvailable()>0){const auto bytes=reply_->read(65536);if(bytes.isEmpty())break;
        if(received_+bytes.size()>expected||file_->write(bytes)!=bytes.size()){
            transferError_="Could not save the model, or its download exceeded the expected size.";reply_->abort();return;
        }
        hash_->addData(bytes);received_+=bytes.size();progress_=double(received_)/expected;
    }
    emit changed();
}
void SubtitleModels::finishDownload(){
    readDownload();auto *reply=reply_.data();if(!reply)return;
    const auto spec=selectedSpec();QString error=transferError_;
    if(canceled_->load())error="Model download canceled. Existing files were preserved.";
    else if(error.isEmpty()&&(reply->error()!=QNetworkReply::NoError||reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200))error="Model download interrupted. Check your connection and try again.";
    else if(error.isEmpty()&&(received_!=spec.bytes||hash_->result().toHex()!=spec.sha256))error="Model verification failed. Download it again.";
    if(error.isEmpty()&&!file_->commit())error="Could not publish the verified model: "+file_->errorString();
    if(!error.isEmpty())file_->cancelWriting();
    file_.reset();hash_.reset();reply_.clear();reply->deleteLater();
    finish(error.isEmpty(),error.isEmpty()?"Model ready. Transcription stays on this computer.":error);
}
void SubtitleModels::cancel(){
    if(!busy_)return;
    *canceled_=true;
    if(reply_){reply_->abort();return;}
    ++verificationRevision_;finish(false,"Model check canceled.");
}
