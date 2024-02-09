#include "updatechecker.h"

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
    connect(&manager, &QNetworkAccessManager::finished, this, &UpdateChecker::onFinished);

    // speed up first network request significantly, otherwise it takes about 2-3 seconds
    manager.connectToHostEncrypted("api.github.com", 443);
}

void UpdateChecker::checkForUpdate(const QString &currentVersion) {
    this->currentVersion = currentVersion;
    QNetworkRequest req(QUrl("https://api.github.com/repos/hardwario/hio-monitor/releases/latest"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    manager.get(req);
}

void UpdateChecker::onFinished(QNetworkReply *reply) {
    if (!reply || reply->error() != QNetworkReply::NoError) {
        if (reply) reply->deleteLater();
        setUpdateAvailable(false);
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();
    QString latestVersion = obj["tag_name"].toString();

    bool isAvailable = latestVersion != currentVersion;
    setUpdateAvailable(isAvailable);

    reply->deleteLater();
}
