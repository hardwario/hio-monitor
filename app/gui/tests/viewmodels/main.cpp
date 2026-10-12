#include "hio/core/app_identity.h"
#include "single_instance_helper.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QLocale>
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

// Helper mode for lock_holder.h (libs/device/tests): with HIO_TEST_HOLD_LOCK_FILE=<path> this
// binary takes a real QLockFile at <path> (as ProbeLock::acquire() does), prints "locked" and holds
// the lock until its stdin ends.
int holdLockFile(int argc, char** argv, const QString& path) {
    const QCoreApplication app(argc, argv);
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
        return holdLockFile(argc, argv, lockFile);
    }
    if (const QString spec = qEnvironmentVariable("HIO_TEST_SINGLE_INSTANCE"); !spec.isEmpty()) {
        return hio::gui::test::runSingleInstanceHelper(argc, argv, spec);
    }
    ::testing::InitGoogleMock(&argc, argv);
    // Death tests (debug assertions of the registries) re-run this binary instead of forking a
    // process with Qt threads.
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    // Tests never touch the user's real settings or data directories.
    QStandardPaths::setTestModeEnabled(true);
    // Also for the test discovery at build time (no ctest environment): no window system, no
    // input-method or D-Bus threads.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    if (qEnvironmentVariableIsEmpty("QT_IM_MODULES")) {
        qputenv("QT_IM_MODULE", "none");
        qputenv("QT_IM_MODULES", "none");
    }
    if (::testing::GTEST_FLAG(list_tests)) {
        return RUN_ALL_TESTS(); // test discovery: no application, no Qt threads
    }
    const QGuiApplication app(argc, argv);
    // Numbers in texts ("3.30 V") do not depend on the machine's language.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    hio::initApplicationIdentity();
    return RUN_ALL_TESTS();
}
