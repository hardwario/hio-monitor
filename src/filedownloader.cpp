#include "filedownloader.h"

FileDownloader::FileDownloader(QUrl imageUrl, QObject *parent) :
    QObject(parent)
{
    connect(&_webCtrl, &QNetworkAccessManager::finished,
        this, &FileDownloader::fileDownloaded);
    QNetworkRequest request(imageUrl);
    _webCtrl.get(request);
}

void FileDownloader::fileDownloaded(QNetworkReply* pReply) {
    _downloadedData = pReply->readAll();
    pReply->deleteLater();
    emit downloaded();
}

void FileDownloader::checkError(QNetworkReply* pReply) {
    auto err = pReply->error();
    if (err != QNetworkReply::NetworkError::NoError) {
        QString str = QString(QMetaEnum::fromType<QNetworkReply::NetworkError>().valueToKey(err));
        emit errorOccured(str);
    }
}

QByteArray FileDownloader::downloadedData() const {
    return _downloadedData;
}
