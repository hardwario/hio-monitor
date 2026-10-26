#pragma once

// A real QLockFile held by another process (review R41, R45): runs this test binary in its
// helper mode (HIO_TEST_HOLD_LOCK_FILE, see main.cpp), which locks the file as ProbeLock::acquire()
// does and keeps it until its stdin is closed (by the destructor).

#include <QByteArray>
#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QtTypes>

#include <chrono>

namespace hio::device::test {

class LockHolder {
public:
    explicit LockHolder(const QString& lockFile) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("HIO_TEST_HOLD_LOCK_FILE"), lockFile);
        m_process.setProcessEnvironment(env);
        m_process.start(QCoreApplication::applicationFilePath(), {});
    }
    ~LockHolder() { release(); }
    LockHolder(const LockHolder&) = delete;
    LockHolder& operator=(const LockHolder&) = delete;
    LockHolder(LockHolder&&) = delete;
    LockHolder& operator=(LockHolder&&) = delete;

    // Waits (at most 30 s) until the helper holds the lock; false when it could not lock.
    [[nodiscard]] bool waitUntilLocked() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!m_out.contains("locked\n")) { // the helper's "\r\n" is normalized below
            if (m_out.contains("failed") || std::chrono::steady_clock::now() > deadline ||
                m_process.state() == QProcess::NotRunning) {
                return false;
            }
            m_process.waitForReadyRead(100);
            m_out += m_process.readAllStandardOutput();
            m_out.replace("\r\n", "\n"); // Windows text-mode stdout
        }
        return true;
    }

    [[nodiscard]] qint64 pid() const { return m_process.processId(); }

    // Ends the helper, which unlocks the file.
    void release() {
        if (m_process.state() == QProcess::NotRunning) {
            return;
        }
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(10000)) {
            m_process.kill();
            m_process.waitForFinished(5000);
        }
    }

private:
    QProcess m_process;
    QByteArray m_out;
};

} // namespace hio::device::test
