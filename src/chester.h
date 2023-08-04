#ifndef CHESTER_H
#define CHESTER_H

#include <QObject>
#include <QThread>
#include <QDebug>
#include <QTimer>
#include <QRandomGenerator>
#include <JLinkARMDLL.h>
#include "filehandler.h"

class Chester : public QObject
{
    Q_OBJECT

    static void jlinkLogHandler(const char *msg);
    static void jlinkErrHandler(const char *msg);

    QThread *attachThread = nullptr;
    QThread *messageReaderThread = nullptr;
    QThread *logReaderThread = nullptr;
public:
    explicit Chester(QObject *parent = nullptr, FileHandler *commandHistoryFile = nullptr);
    Q_INVOKABLE QVariant getCommandHistory();
public slots:
    void sendCommand(const QString &command);
    
signals:
    void attachRequested();
    void detachRequested();
    void attachSucceeded();
    void attachFailed();
    void detachSucceeded();
    void detachFailed();
    void messageReadingFailed();
    void logReadingFailed();
    void sendCommandSucceeded(const QString &command);
    void sendCommandFailed(const QString &command);
    void deviceMessageReceived(const QString &msg);
    void deviceLogReceived(const QString &msg);

private slots:
    void checkMessageForCommandFailure(const QString &message);
    void attach();
    void detach();
private:
    bool isConnected();
    FileHandler *_logFile = nullptr;
    FileHandler *_commandHistoryFile = nullptr;
    QString _currentCommand;
};

#endif //CHESTER_H
