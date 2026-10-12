#pragma once

// SingleInstance - one GUI instance per user (plan/features/0004-m3-gui-shell.md, A5; ADR-0058).
//
// start():
//   - takes QLockFile <AppLocalData>/gui.lock (stale-lock detection by process, 0 wait) and
//     listens on the QLocalServer -> Primary. The server is the socket file gui.sock next to the
//     lock file (AppLocalData, never /tmp; review S11, S21; the runtime directory when that path
//     is too long for a Unix socket), on Windows the named pipe "hio-monitor-<user>". If it cannot
//     listen, the lock is released again and single-instance mode is off for this run (Primary,
//     serving() false), so that later launches are not reported as "does not respond";
//   - when the lock is held: connects to the server, sends "activate\n" and waits for "ok\n"
//     (2 s in total) -> Activated (the first instance raises its window; this one exits with 0);
//   - no answer within 2 s (a hung instance) -> NotResponding (main() asks: Quit / Start anyway).
// The primary instance handles one message per connection: "activate\n" emits activationRequested
// and is answered with "ok\n"; anything else is ignored. Every read is bounded to maxMessageBytes
// and every connection is dropped after the timeout.
//
// The CLI is never single-instance. Ownership: a stack object in main(). GUI thread only.

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QString>
#include <QtTypes>

#include <chrono>
#include <cstdint>
#include <memory>

namespace hio::gui {

class SingleInstance : public QObject {
    Q_OBJECT

public:
    enum class Result : std::uint8_t {
        Primary,       // this is the only instance
        Activated,     // another instance was asked to come to the front
        NotResponding, // another instance holds the lock but does not answer
    };

    static constexpr qsizetype maxMessageBytes = 64;
    static constexpr std::chrono::milliseconds timeout{2000};
    // sockaddr_un::sun_path is 104 bytes on macOS, 108 on Linux (incl. the terminating zero).
    static constexpr qsizetype maxSocketPathBytes = 100;

    // Defaults: <AppLocalData>/gui.lock and defaultServerName().
    explicit SingleInstance(QObject* parent = nullptr);
    SingleInstance(QString lockFile, QString serverName, QObject* parent = nullptr);
    ~SingleInstance() override;
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    SingleInstance(SingleInstance&&) = delete;
    SingleInstance& operator=(SingleInstance&&) = delete;

    [[nodiscard]] Result start();
    // The activation server runs.
    [[nodiscard]] bool serving() const;
    // "Start anyway": serve activation requests only if the server name is free; the lock is not
    // taken.
    void startWithoutLock();

    [[nodiscard]] static QString defaultLockFile();
    [[nodiscard]] static QString defaultServerName();

signals:
    void activationRequested();

private:
    [[nodiscard]] bool listen();
    [[nodiscard]] bool askToActivate() const;
    void onNewConnection();

    QString m_lockFile;
    QString m_serverName;
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer m_server;
};

} // namespace hio::gui
