#ifndef FILEDOWNLOADER_H
#define FILEDOWNLOADER_H

#include <QDir>
#include <QMetaEnum>
#include <QSaveFile>
#include <QByteArray>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QNetworkRequest>
#include <QNetworkAccessManager>

class FileDownloader : public QObject
{
    Q_OBJECT
public:
    explicit FileDownloader(QUrl imageUrl, QObject *parent = 0);
    QByteArray downloadedData() const;
    QString save(const QString& fileName);
    bool remove(const QString& fileName);

signals:
    void downloaded();
    void errorOccured(QString err);

private slots:
    void fileDownloaded(QNetworkReply* pReply);
    bool isErrorOccured(QNetworkReply* pReply);

private:
    QNetworkAccessManager _webCtrl;
    QByteArray _downloadedData;
};

#endif // FILEDOWNLOADER_H
