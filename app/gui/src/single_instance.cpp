#include "single_instance.h"

#include "gui_log.h"

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QLoggingCategory>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QtEnvironmentVariables>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>

namespace hio::gui {

namespace {

const QByteArray activateMessage = QByteArrayLiteral("activate\n");
const QByteArray okMessage = QByteArrayLiteral("ok\n");

} // namespace

SingleInstance::SingleInstance(QObject* parent)
    : SingleInstance(defaultLockFile(), defaultServerName(), parent) {}

SingleInstance::SingleInstance(QString lockFile, QString serverName, QObject* parent)
    : QObject(parent), m_lockFile(std::move(lockFile)), m_serverName(std::move(serverName)) {
    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
}

SingleInstance::~SingleInstance() {
    m_server.close();
    if (m_lock) {
        m_lock->unlock();
    }
}

bool SingleInstance::serving() const { return m_server.isListening(); }

QString SingleInstance::defaultLockFile() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
           QStringLiteral("/gui.lock");
}

QString SingleInstance::defaultServerName() {
#ifdef Q_OS_WIN
    // A named pipe; the user name keeps the pipes of several users apart.
    QString user = qEnvironmentVariable("USERNAME");
    QString safe;
    for (const QChar character : user) {
        safe.append(character.isLetterOrNumber() || character == QLatin1Char('_') ||
                            character == QLatin1Char('-')
                        ? character
                        : QLatin1Char('_'));
    }
    return QStringLiteral("hio-monitor-") + (safe.isEmpty() ? QStringLiteral("user") : safe);
#else
    // Next to the lock file (review S21): every session of the user that sees the lock also finds
    // the server (ssh, `su -l` and desktop sessions have different runtime directories). It is the
    // user's own application data directory, never /tmp (S11); the socket itself is owner-only
    // (QLocalServer::UserAccessOption). A path too long for a Unix socket falls back to the
    // runtime directory.
    QString besideLock = QFileInfo(defaultLockFile()).absolutePath() + QStringLiteral("/gui.sock");
    if (besideLock.toLocal8Bit().size() <= maxSocketPathBytes) {
        return besideLock;
    }
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) +
           QStringLiteral("/hio-monitor.sock");
#endif
}

SingleInstance::Result SingleInstance::start() {
    QDir().mkpath(QFileInfo(m_lockFile).absolutePath());
    auto lock = std::make_unique<QLockFile>(m_lockFile);
    lock->setStaleLockTime(std::chrono::milliseconds(0)); // stale only when the owner is gone
    if (lock->tryLock(std::chrono::milliseconds(0))) {
        if (!listen()) {
            // Without the server a later launch could not reach this instance and would report
            // it as not responding, so the lock is given up: single-instance is off for this run
            // (review S11).
            qCCritical(lcGui) << "Single instance: cannot listen on" << m_serverName << "-"
                              << m_server.errorString() << "- single-instance mode is off";
            lock->unlock();
            return Result::Primary;
        }
        m_lock = std::move(lock);
        return Result::Primary;
    }
    if (askToActivate()) {
        qCInfo(lcGui) << "Another instance is running; it was asked to come to the front";
        return Result::Activated;
    }
    qCWarning(lcGui) << "Another instance holds" << m_lockFile << "but does not respond";
    return Result::NotResponding;
}

void SingleInstance::startWithoutLock() {
    QLocalSocket probe;
    probe.connectToServer(m_serverName);
    if (probe.waitForConnected(100)) {
        probe.abort();
        return; // the other instance keeps its server
    }
    if (!listen()) {
        qCWarning(lcGui) << "Single instance: cannot listen on" << m_serverName;
    }
}

bool SingleInstance::listen() {
    // A socket file left behind by a crashed instance (Unix) would block listen().
    if (QFileInfo(m_serverName).isAbsolute()) {
        QDir().mkpath(QFileInfo(m_serverName).absolutePath());
    }
    QLocalServer::removeServer(m_serverName);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    return m_server.listen(m_serverName);
}

bool SingleInstance::askToActivate() const {
    QElapsedTimer elapsed;
    elapsed.start();
    const auto remaining = [&elapsed] {
        return static_cast<int>(std::max<qint64>(0, timeout.count() - elapsed.elapsed()));
    };
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (!socket.waitForConnected(remaining())) {
        return false;
    }
    socket.write(activateMessage);
    if (!socket.waitForBytesWritten(remaining())) {
        return false;
    }
    QByteArray answer;
    while (!answer.contains('\n') && answer.size() < maxMessageBytes) {
        if (remaining() == 0 || !socket.waitForReadyRead(remaining())) {
            return false;
        }
        answer += socket.read(maxMessageBytes - answer.size());
    }
    return answer.startsWith(okMessage);
}

void SingleInstance::onNewConnection() {
    while (QLocalSocket* socket = m_server.nextPendingConnection()) {
        // Qt parent ownership: the server owns the socket; deleteLater() on disconnect.
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(timeout, socket, [socket] { socket->abort(); });
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            const QByteArray received = socket->peek(maxMessageBytes);
            if (!received.contains('\n') && received.size() < maxMessageBytes) {
                return; // wait for the rest (bounded by the timeout)
            }
            socket->read(maxMessageBytes);
            if (received.startsWith(activateMessage)) {
                activationRequested(); // signal
                socket->write(okMessage);
                socket->flush();
            }
            socket->disconnectFromServer();
        });
    }
}

} // namespace hio::gui
