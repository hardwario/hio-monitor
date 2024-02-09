#include "chester.h"

Chester::Chester(QObject *parent, HistoryFile *commandHistoryFile)
    : DeviceInterface(parent)
{
    _name = "chester";
    connect(this, &Chester::attachRequested,
            this, &Chester::attach);
    connect(this, &Chester::detachRequested,
            this, &Chester::detach);

    _logFile = new HistoryFile(this, "hardwario-monitor-console.log");
    _commandHistoryFile = commandHistoryFile;
    connect(_commandHistoryFile, &HistoryFile::historyChanged,
            this, &Chester::historyChanged);
}

QVariant Chester::getCommandHistory() {
    return QVariant::fromValue(_commandHistoryFile->readAll());
}

bool Chester::isConnected() {
    return JLINKARM_IsConnected() == 1;
}

void Chester::checkMessageForCommandFailure(const QString &message) {
    if (message.contains("command not found") ||
        message.contains("wrong")) {
        qDebug() << "Command failed";
        emit sendCommandFailed(_currentCommand);
        return;
    }

    // sometimes message received in chunks,
    // so I don't want to print the same command multiple times
    if (_currentCommand != _lastCommand) {
        emit sendCommandSucceeded(_currentCommand);
    }

    _lastCommand = _currentCommand;
}

void Chester::sendCommand(const QString &command) {
    if (!this->isConnected()) {
        qWarning() << "Device is not connected";
        emit sendCommandFailed(command);
        return;
    }

    _currentCommand = command;
    _lastCommand = _currentCommand;
    _commandHistoryFile->writeMoveOnMatch(_currentCommand);

    QThread *thread = QThread::create([this, command]{
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
        }
    });

    thread->start();
}

void Chester::jlinkLogHandler(const char *msg) {
    qInfo() << "J-Link Log:" << msg;
}

void Chester::jlinkErrHandler(const char *msg) {
    qWarning() << "J-Link Err:" << msg;
}

void Chester::attach() {
    attachThread = QThread::create([this] {
        QTimer timer;
        timer.setSingleShot(true);
        const char *err = JLINKARM_OpenEx(Chester::jlinkLogHandler, Chester::jlinkLogHandler);

        if (err != NULL) {
            qWarning() << "Call `JLINKARM_OpenEx` failed";
            emit attachFailed();
            return;
        }

        if (JLINKARM_ExecCommand("Device = nRF52840_xxAA", NULL, 0) < 0) {
            qWarning() << "Call `JLINKARM_ExecCommand` failed";
            emit attachFailed();
            return;
        }

        JLINKARM_TIF_Select(JLINKARM_TIF_SWD);
        JLINKARM_SetSpeed(4000);

        if (JLINKARM_Connect() < 0) {
            qWarning() << "Call `JLINKARM_Connect` failed";
            emit attachFailed();
            return;
        }

        if (JLINK_RTTERMINAL_Control(JLINKARM_RTTERMINAL_CMD_START, NULL) < 0) {
            qWarning() << "Call `JLINK_RTTERMINAL_Control` failed (JLINKARM_RTTERMINAL_CMD_START)";
            emit attachFailed();
            return;
        }

        timer.start(2000);

        forever {
            if (timer.remainingTime() == 0) {
                emit attachFailed();
                return;
            }

            U32 dir = JLINKARM_RTTERMINAL_BUFFER_DIR_UP;
            int buffersFound = JLINK_RTTERMINAL_Control(JLINKARM_RTTERMINAL_CMD_GETNUMBUF, &dir);

            if (buffersFound == -2) {
                QThread::msleep(50);
                continue;
            }

            if (buffersFound < 0) {
                qWarning() << "Call `JLINK_RTTERMINAL_Control` failed (JLINKARM_RTTERMINAL_CMD_GETNUMBUF)";
                emit attachFailed();
                return;
            }

            if (buffersFound == 0) {
                qWarning() << "No RTT UP buffers found";
                emit attachFailed();
                return;
            }

            qInfo() << "RTT UP buffers found:" << buffersFound;
            break;
        }

        timer.start(2000);

        forever {
            if (timer.remainingTime() == 0) {
                emit attachFailed();
                return;
            }

            U32 dir = JLINKARM_RTTERMINAL_BUFFER_DIR_DOWN;
            int buffersFound = JLINK_RTTERMINAL_Control(JLINKARM_RTTERMINAL_CMD_GETNUMBUF, &dir);

            if (buffersFound == -2) {
                QThread::msleep(50);
                continue;
            }

            if (buffersFound < 0) {
                qWarning() << "Call `JLINK_RTTERMINAL_Control` failed (JLINKARM_RTTERMINAL_CMD_GETNUMBUF)";
                emit attachFailed();
                return;
            }

            if (buffersFound == 0) {
                qWarning() << "No RTT DOWN buffers found";
                emit attachFailed();
                return;
            }

            qInfo() << "RTT DOWN buffers found:" << buffersFound;
            break;
        }

        emit attachSucceeded();

        messageReaderThread = QThread::create([this]{
            QByteArray ba;
            char buffer[2048];
            bool isFirstMessage = true;
            while (!QThread::currentThread()->isInterruptionRequested()) {

                if (!this->isConnected()) {
                    emit messageReadingFailed();
                    return;
                }

                // TODO Split read operation to chunks based on buffer size
                int bytesRead = JLINK_RTTERMINAL_Read(0, buffer, sizeof(buffer));

                if (bytesRead < 0) {
                    qWarning() << "Call `JLINK_RTTERMINAL_Read` failed";
                    emit messageReadingFailed();
                    return;
                } else if (bytesRead == 0) {
                    QThread::msleep(50);
                    continue;
                }

                ba.append(buffer, bytesRead);

                forever {
                    int newLineIndex = ba.indexOf('\n');
                    if (newLineIndex == -1) {
                        isFirstMessage = true;
                        break;
                    }

                    QByteArray line = ba.left(newLineIndex);

                    ba.remove(0, newLineIndex + 1);

                    line.replace('\r', "");
                    line.replace('\n', "");

                    if (line.length() > 0) {
                        qDebug() << "Read device message:" << QString(line);
                        if (isFirstMessage) {
                            checkMessageForCommandFailure(QString(line));
                            isFirstMessage = false;
                        }
                        emit deviceMessageReceived(QString(line));
                    }
                }
            }
        });

        messageReaderThread->start();

        logReaderThread = QThread::create([this]{
            QByteArray ba;
            char buffer[2048];

            while (!QThread::currentThread()->isInterruptionRequested()) {

                if (!this->isConnected()) {
                    emit logReadingFailed();
                    return;
                }

                // TODO Split read operation to chunks based on buffer size
                int bytesRead = JLINK_RTTERMINAL_Read(1, buffer, sizeof(buffer));

                if (bytesRead < 0) {
                    qWarning() << "Call `JLINK_RTTERMINAL_Read` failed";
                    emit logReadingFailed();
                    return;

                } else if (bytesRead == 0) {
                    QThread::msleep(50);
                    continue;
                }

                ba.append(buffer, bytesRead);

                forever {
                    int newLineIndex = ba.indexOf('\n');

                    if (newLineIndex == -1) {
                        break;
                    }

                    QByteArray line = ba.left(newLineIndex);

                    ba.remove(0, newLineIndex + 1);

                    line.replace('\r', "");
                    line.replace('\n', "");

                    if (line.length() > 0) {
                        qDebug() << "Read device log:" << QString(line);
                        _logFile->write(QString(line));
                        emit deviceLogReceived(QString(line));
                    }
                }
            }
        });

        logReaderThread->start();
    });

    attachThread->start();
}

void Chester::detach() {
    auto thread = QThread::create([this]{
        if (messageReaderThread) {
            qDebug() << "Requesting thread interruption for `messageReaderThread`";
            messageReaderThread->requestInterruption();
        }

        if (logReaderThread) {
            qDebug() << "Requesting thread interruption for `logReaderThread`";
            logReaderThread->requestInterruption();
        }

        if (messageReaderThread) {
            messageReaderThread->wait();
            qDebug() << "Thread `messageReaderThread` finished";
        }

        if (logReaderThread) {
            logReaderThread->wait();
            qDebug() << "Thread `logReaderThread` finished";
        }

        JLINKARM_Close();

        emit detachSucceeded();
    });

    thread->start();
}
