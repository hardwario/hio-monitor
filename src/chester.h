#ifndef CHESTER_H
#define CHESTER_H

#include <QDebug>
#include <QTimer>
#include <QThread>
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

signals:
    void attachFailed();
    void detachFailed();
    void attachRequested();
    void detachRequested();
    void attachSucceeded();
    void detachSucceeded();
    void logReadingFailed();
    void messageReadingFailed();
    void deviceLogReceived(const QString &msg);

private slots:
    void attach();
    void detach();
    void checkMessageForCommandFailure(const QString &message);

private:
    HistoryFile *_logFile = nullptr;
    HistoryFile *_shellFile = nullptr;
    HistoryFile *_commandHistoryFile = nullptr;
    QString _currentCommand;
    QString _lastCommand;
};

#endif //CHESTER_H
