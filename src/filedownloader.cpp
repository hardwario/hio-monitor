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
    if (!isErrorOccured(pReply)) {
        emit downloaded();
    }
}

bool FileDownloader::isErrorOccured(QNetworkReply* pReply) {
    auto err = pReply->error();
    bool isErr = err != QNetworkReply::NetworkError::NoError;
    if (isErr) {
        auto str = QString(QMetaEnum::fromType<QNetworkReply::NetworkError>().valueToKey(err));
        emit errorOccured(str);
    }
    return isErr;
}

QByteArray FileDownloader::downloadedData() const {
    return _downloadedData;
}

void FileDownloader::save(const QString& fileName) {
    QSaveFile file(fileName);
    file.open(QIODevice::WriteOnly);
    file.write(downloadedData());
    // Calling commit() is mandatory, otherwise nothing will be written.
    file.commit();
    qDebug() << "Downloaded hex file saved to the " << QDir::currentPath() + "/" + fileName;
}

void FileDownloader::remove(const QString& fileName) {
    QFile file(fileName);
    file.remove();
    qDebug() << "File " << fileName << "sucesfully deleted";
}
