#ifndef FILEDOWNLOADER_H
#define FILEDOWNLOADER_H

#include <QObject>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMetaEnum>

class FileDownloader : public QObject
{
    Q_OBJECT
public:
    explicit FileDownloader(QUrl imageUrl, QObject *parent = 0);
    QByteArray downloadedData() const;
signals:
    void downloaded();
    void errorOccured(QString err);
private slots:
    void fileDownloaded(QNetworkReply* pReply);
    void checkError(QNetworkReply* pReply);
private:
    QNetworkAccessManager _webCtrl;
    QByteArray _downloadedData;
};

#endif // FILEDOWNLOADER_H
