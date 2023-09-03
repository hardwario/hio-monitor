#ifndef FILEDOWNLOADER_H
#define FILEDOWNLOADER_H

#include <QObject>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMetaEnum>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>

class FileDownloader : public QObject
{
    Q_OBJECT
public:
    explicit FileDownloader(QUrl imageUrl, QObject *parent = 0);
    QByteArray downloadedData() const;
    QString save(const QString& fileName);
    void remove(const QString& fileName);
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
