#ifndef FLASH_H
#define FLASH_H

#include <QObject>
#include <QDebug>
#include "deviceInterface.h"
#include "filehandler.h"

class Flash : public DeviceInterface {
    Q_OBJECT
    Q_INTERFACES(DeviceInterface)
public:
    explicit Flash(QObject *parent = nullptr);
    Q_INVOKABLE QVariant getCommandHistory() override;
public slots:
    void sendCommand(const QString &command) override;
signals:
    void errorOccured(const QString &err);
private slots:
    bool isPath(const QString &str);
    bool tryDownload(const QString &str);
//    void run(const QString &command);
private:
    QByteArray _programFile;
    FileHandler *_commandHistoryFile = nullptr;
};

#endif // FLASH_H
