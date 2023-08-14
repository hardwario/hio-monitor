#include "flash.h"
#include "filedownloader.h"

Flash::Flash(QObject *parent) : DeviceInterface(parent) {
    _commandHistoryFile = new FileHandler("hardwario-monitor-flashhex-history.txt");
};

QVariant Flash::getCommandHistory() {
    return QVariant::fromValue(_commandHistoryFile->readAll());
}

void Flash::sendCommand(const QString &command) {
    qDebug() << "Flash sendCommand: " << command;
    if(isPath(command)) {
        qDebug() << "it's a path";
    } else {
        tryDownload(command);
    }
}

bool Flash::isPath(const QString &str) {
    return str.contains("/") || str.contains("\\") || str.contains(".hex");
}

bool Flash::tryDownload(const QString &str) {
    QUrl url(QString("https://firmware.hardwario.com/chester/%1/hex").arg(str));
    auto downloader = new FileDownloader(url, this);
    connect(downloader, &FileDownloader::downloaded,
        [this, downloader] {
            qDebug() << "flash program file downloaded!";
            _programFile = downloader->downloadedData();
        });
    connect(downloader, &FileDownloader::errorOccured,
        [this](QString error) {
            qDebug() << "flash program file error while downloading: " << error;
            emit errorOccured(error);
            return false;
        });
    return true;
}

