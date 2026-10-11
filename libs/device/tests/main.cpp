#include "hio/core/app_identity.h"

#include <QCoreApplication>
#include <QLockFile>
#include <QStandardPaths>
#include <QString>
#include <QtEnvironmentVariables>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <string>

namespace {

// Helper mode for the probe-lock tests (review R41): with HIO_TEST_HOLD_LOCK_FILE=<path> this
// binary takes a real QLockFile at <path> (as ProbeLock::acquire() does), prints "locked" and
// holds the lock until its stdin ends, so a test sees a lock file that Qt wrote in another process
// (with the application and host names Qt records on this OS).
int holdLockFile(const QString& path) {
    QLockFile lock(path);
    lock.setStaleLockTime(std::chrono::milliseconds(0));
    if (!lock.tryLock(std::chrono::milliseconds(0))) {
        std::cout << "failed " << static_cast<int>(lock.error()) << "\n" << std::flush;
        return 1;
    }
    std::cout << "locked\n" << std::flush;
    for (std::string line; std::getline(std::cin, line);) {
    }
    lock.unlock();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (const QString lockFile = qEnvironmentVariable("HIO_TEST_HOLD_LOCK_FILE");
        !lockFile.isEmpty()) {
        const QCoreApplication app(argc, argv);
        return holdLockFile(lockFile);
    }
    ::testing::InitGoogleMock(&argc, argv);
    // Tests never touch the user's real settings or data directories.
    QStandardPaths::setTestModeEnabled(true);
    const QCoreApplication app(argc, argv);
    hio::initApplicationIdentity();
    return RUN_ALL_TESTS();
}
