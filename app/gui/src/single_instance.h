#pragma once

// SingleInstance - one GUI instance per user (plan/features/0004-m3-gui-shell.md, A5; ADR-0058).
//
// start():
//   - takes QLockFile <AppLocalData>/gui.lock (stale-lock detection by process, 0 wait) and
//     listens on the QLocalServer "hio-monitor-<user>" -> Primary;
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

    // Defaults: <AppLocalData>/gui.lock and "hio-monitor-<user>".
    explicit SingleInstance(QObject* parent = nullptr);
    SingleInstance(QString lockFile, QString serverName, QObject* parent = nullptr);
    ~SingleInstance() override;
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    SingleInstance(SingleInstance&&) = delete;
    SingleInstance& operator=(SingleInstance&&) = delete;

    [[nodiscard]] Result start();
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
