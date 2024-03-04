#include "filedownloader.h"

#include <QFileDevice>

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

QString FileDownloader::save(const QString& fileName) {
    auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/" + fileName;
    QSaveFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(downloadedData());
    // Calling commit() is mandatory, otherwise nothing will be written.
    file.commit();
    return path;
}

bool FileDownloader::remove(const QString& fileName) {
    QFile file(fileName);

    file.setPermissions(file.permissions() |
                    QFileDevice::WriteOwner |
                    QFileDevice::WriteUser |
                    QFileDevice::WriteGroup |
                    QFileDevice::WriteOther);

    return file.remove();
}
