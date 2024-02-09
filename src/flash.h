#ifndef FLASH_H
#define FLASH_H

#include <QThread>
#include <QDebug>
#include <nrfjprogdll.h>
#include <QRegularExpression>

#include "filedownloader.h"
#include "deviceInterface.h"

class Flash : public DeviceInterface {
    Q_OBJECT
    Q_INTERFACES(DeviceInterface)
    QThread *flashThread = nullptr;

public:
    explicit Flash(QObject *parent = nullptr);
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    Q_PROPERTY(bool running MEMBER _isRunning)
    QVariant getCommandHistory() override;

signals:
    void readyChanged();
    void finished();
    void errorOccured();

public slots:
    void sendCommand(const QString &command) override;
    void setHexPath(const QString &path);
    void defaultFlash();
    bool isReady() {
        return _isReady;
    }

private slots:
    bool tryDownload(const QString &str);
    bool loadDll();
    void freeDll();
    bool checkErr(nrfjprogdll_err_t err, const QString& context);
    void flash(QString filepath);
    QString makeMessage(QString tag, QString msg);

    void setReady(bool value) {
        _isReady = value;
        emit readyChanged();
    }

private:
    bool _isReady = false;
    bool _isRunning = false;
    bool _isFileDownloaded = false;
    QString _hexPath;
    FileDownloader *_downloader = nullptr;
    QString _helpMessage =
"Type the hex value and press Enter to start downloading the CHESTER Catalog Application.\n" \
"Browse: select program file on your computer.\n" \
"Run: start the flashing process.\n" \
"Catalog: open catalog application list in browser.\n";
};

#endif // FLASH_H
