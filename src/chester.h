#ifndef CHESTER_H
#define CHESTER_H

#include <QObject>
#include <QThread>
#include <QDebug>
#include <QTimer>
#include <JLinkARMDLL.h>
#include "historyfile.h"
#include "deviceInterface.h"

class Chester : public DeviceInterface
{
    Q_OBJECT
    Q_INTERFACES(DeviceInterface)

    static void jlinkLogHandler(const char *msg);
    static void jlinkErrHandler(const char *msg);

    QThread *attachThread = nullptr;
    QThread *messageReaderThread = nullptr;
    QThread *logReaderThread = nullptr;
public:
    explicit Chester(QObject *parent = nullptr, HistoryFile *commandHistoryFile = nullptr);
    QVariant getCommandHistory() override;
public slots:
    void sendCommand(const QString &command) override;
    bool isConnected();
signals:
    void attachRequested();
    void detachRequested();
    void attachSucceeded();
    void attachFailed();
    void detachSucceeded();
    void detachFailed();
    void messageReadingFailed();
    void logReadingFailed();
    void deviceLogReceived(const QString &msg);
private slots:
    void checkMessageForCommandFailure(const QString &message);
    void attach();
    void detach();
private:
    HistoryFile *_logFile = nullptr;
    HistoryFile *_commandHistoryFile = nullptr;
    QString _currentCommand;
    QString _lastCommand;
};

#endif //CHESTER_H
