#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QtNetwork>
#include <QQmlEngine>
#include <QQmlEngine>
#include <QJsonObject>
#include <QJsonDocument>

class UpdateChecker : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool updateAvailable READ isUpdateAvailable NOTIFY updateAvailableChanged)
public:
    explicit UpdateChecker(QObject *parent = nullptr);

    Q_INVOKABLE void checkForUpdate(const QString &currentVersion);

    bool isUpdateAvailable() const {
        return updateAvailable;
    }

signals:
    void updateAvailableChanged(bool available);
    void readyToCheck();

private slots:
    void onFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager manager;
    QString currentVersion;
    bool updateAvailable = false;

    void setUpdateAvailable(bool available) {
        if (updateAvailable != available) {
            updateAvailable = available;
            emit updateAvailableChanged(updateAvailable);
        }
    }
};

#endif // UPDATECHECKER_H
