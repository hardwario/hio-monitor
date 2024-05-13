#include "chester.h"

Chester::Chester(QObject *parent, HistoryFile *commandHistoryFile)
    : DeviceInterface(parent)
{
    _name = "chester";
    connect(this, &Chester::attachRequested,
            this, &Chester::attach);
    connect(this, &Chester::detachRequested,
            this, &Chester::detach);

    _logFile = new HistoryFile(this, "hardwario-monitor-device.log");
    _shellFile = new HistoryFile(this, "hardwario-monitor-shell.log");

    _commandHistoryFile = commandHistoryFile;
    connect(_commandHistoryFile, &HistoryFile::historyChanged,
            this, &Chester::historyChanged);
}

QVariant Chester::getCommandHistory()
{
    return QVariant::fromValue(_commandHistoryFile->readAll());
}

void Chester::checkMessageForCommandFailure(const QString &message)
{
    if (message.contains("command not found") ||
        message.contains("wrong") ||
        message.contains("invalid"))
    {
        qDebug() << "Command failed";
        emit sendCommandFailed(_currentCommand);
        return;
    }

    // sometimes message received in chunks,
    // so I don't want to print the same command multiple times
    if (_currentCommand != _lastCommand)
    {
        emit sendCommandSucceeded(_currentCommand);
    }

    _lastCommand = _currentCommand;
}

void Chester::sendCommand(const QString &command)
{
    if (!JLINKARM_IsConnected())
    {
        qWarning() << "Device is not connected";
        emit sendCommandFailed(command);
        return;
    }

    _currentCommand = command;
    _lastCommand = _currentCommand;
    _commandHistoryFile->writeMoveOnMatch(_currentCommand);

    QThread *thread = QThread::create([this, command]
                                      {
        qDebug() << "Sending command:" << command;

        QByteArray ba = command.toUtf8();

        ba.append('\r');
        ba.append('\n');

        forever {
            // TODO Split write operation to chunks based on buffer size
            int bytesWritten = JLINK_RTTERMINAL_Write(0, ba.constData(), ba.size());

            qDebug() << "bytesWritten:" << bytesWritten;

            if (bytesWritten < 0) {
                qWarning() << "Command sending failed:" << command;
                emit sendCommandFailed(command);
                break;
            }

            if (bytesWritten == ba.size()) {
                qInfo() << "Command successfully sent:" << command;
                emit sendCommandSucceeded(command);
                break;
            }

            ba = ba.mid(bytesWritten);
            QThread::msleep(50);
        } });

    thread->start();
}

void Chester::jlinkLogHandler(const char *msg)
{
    qInfo() << "J-Link Log:" << msg;
}

void Chester::jlinkErrHandler(const char *msg)
{
    qWarning() << "J-Link Err:" << msg;
}

void Chester::errAttachFailed(const char *msg)
{
    qWarning() << "Attach failed:" << msg;
    emit attachFailed();
    JLINKARM_Close();
}

void Chester::attach()
{
    attachThread = QThread::create([this]
                                   {
        QTimer timer;
        timer.setSingleShot(true);

        const char *err = JLINKARM_OpenEx(Chester::jlinkLogHandler, Chester::jlinkLogHandler);

        if (err != NULL) {
            return errAttachFailed("Call `JLINKARM_OpenEx` failed");
        }

        if (JLINKARM_ExecCommand("Device = nRF52840_xxAA", NULL, 0) < 0) {
            return errAttachFailed("Call `JLINKARM_ExecCommand` failed");
        }

        JLINKARM_TIF_Select(JLINKARM_TIF_SWD);
        JLINKARM_SetSpeed(4000);

        if (JLINKARM_Connect() < 0) {
            return errAttachFailed("Call `JLINKARM_Connect` failed");
        }

        if (JLINK_RTTERMINAL_Control(JLINKARM_RTTERMINAL_CMD_START, NULL) < 0) {
            return errAttachFailed("Call `JLINK_RTTERMINAL_Control` failed (JLINKARM_RTTERMINAL_CMD_START)");
        }

        timer.start(2000);

        forever {
            if (timer.remainingTime() == 0) {
                return errAttachFailed("No RTT UP buffers get ready in time");
            }

            U32 dir = JLINKARM_RTTERMINAL_BUFFER_DIR_UP;
            int buffersFound = JLINK_RTTERMINAL_Control(JLINKARM_RTTERMINAL_CMD_GETNUMBUF, &dir);

            if (buffersFound == -2) {
                QThread::msleep(50);
                continue;
            }

            if (buffersFound < 0) {
                return errAttachFailed("Call `JLINK_RTTERMINAL_Control` failed (JLINKARM_RTTERMINAL_CMD_GETNUMBUF)");
            }

            if (buffersFound == 0) {
                return errAttachFailed("No RTT UP buffers found");
            }

            qInfo() << "RTT UP buffers found:" << buffersFound;
            break;
        }

        timer.start(2000);

        forever {
            if (timer.remainingTime() == 0) {
                return errAttachFailed("No RTT DOWN buffers get ready in time");
            }

            U32 dir = JLINKARM_RTTERMINAL_BUFFER_DIR_DOWN;
            int buffersFound = JLINK_RTTERMINAL_Control(JLINKARM_RTTERMINAL_CMD_GETNUMBUF, &dir);

            if (buffersFound == -2) {
                QThread::msleep(50);
                continue;
            }

            if (buffersFound < 0) {
                return errAttachFailed("Call `JLINK_RTTERMINAL_Control` failed (JLINKARM_RTTERMINAL_CMD_GETNUMBUF)");
            }

            if (buffersFound == 0) {
                return errAttachFailed("No RTT DOWN buffers found");
            }

            qInfo() << "RTT DOWN buffers found:" << buffersFound;
            break;
        }

        emit attachSucceeded();

        readerThread = QThread::create([this]{
            QByteArray shellBA;
            QByteArray logBA;
            char buffer[2048];
            bool isFirstMessage = true;
            while (!QThread::currentThread()->isInterruptionRequested()) {

                if (!JLINKARM_IsConnected()) {
                    emit messageReadingFailed();
                    return;
                }

                // TODO Split read operation to chunks based on buffer size
                int bytesRead = JLINK_RTTERMINAL_Read(0, buffer, sizeof(buffer));

                if (bytesRead < 0) {
                    qWarning() << "Call `JLINK_RTTERMINAL_Read` failed";
                    emit messageReadingFailed();
                    return;
                } else if (bytesRead > 0) {
                    shellBA.append(buffer, bytesRead);

                    forever {
                        int newLineIndex = shellBA.indexOf('\n');
                        if (newLineIndex == -1) {
                            isFirstMessage = true;
                            break;
                        }

                        QByteArray line = shellBA.left(newLineIndex);

                        shellBA.remove(0, newLineIndex + 1);

                        line.replace('\r', "");
                        line.replace('\n', "");

                        if (line.length() > 0) {
                            qDebug() << "Read device message:" << QString(line);
                            _shellFile->write(QString(line));
                            if (isFirstMessage) {
                                checkMessageForCommandFailure(QString(line));
                                isFirstMessage = false;
                            }
                            emit deviceMessageReceived(QString(line));
                        }
                    }
                }

                bytesRead = JLINK_RTTERMINAL_Read(1, buffer, sizeof(buffer));
                if (bytesRead < 0) {
                    qWarning() << "Call `JLINK_RTTERMINAL_Read` failed";
                    emit logReadingFailed();
                    return;

                } else if (bytesRead > 0) {
                    logBA.append(buffer, bytesRead);

                    forever {
                        int newLineIndex = logBA.indexOf('\n');

                        if (newLineIndex == -1) {
                            break;
                        }

                        QByteArray line = logBA.left(newLineIndex);

                        logBA.remove(0, newLineIndex + 1);

                        line.replace('\r', "");
                        line.replace('\n', "");

                        if (line.length() > 0) {
                            qDebug() << "Read device log:" << QString(line);
                            _logFile->write(QString(line));
                            emit deviceLogReceived(QString(line));
                        }
                    }
                }

                QThread::msleep(50);
            }
        });

        readerThread->start(); });

    attachThread->start();
}

void Chester::detach()
{
    auto thread = QThread::create([this]
                                  {

        if (readerThread) {
            qDebug() << "Requesting thread interruption for `readerThread`";
            readerThread->requestInterruption();
        }

        if (readerThread) {
            readerThread->wait();
            qDebug() << "Thread `readerThread` finished";
        }

        if (JLINK_RTTERMINAL_Control(JLINKARM_RTTERMINAL_CMD_STOP, NULL) < 0) {
            qWarning() << "Call `JLINK_RTTERMINAL_Control` failed (JLINKARM_RTTERMINAL_CMD_STOP)";
        }

        JLINKARM_Close();

        emit detachSucceeded(); });

    thread->start();
}
