#pragma once

// The helper process of single_instance_test.cpp (see main.cpp): with
// HIO_TEST_SINGLE_INSTANCE=<lock file>|<server name> this binary runs a primary instance, prints
// "primary", and prints "activated" and exits after the first activation request (or after 30 s).
// With HIO_TEST_SINGLE_INSTANCE_HUNG=1 it takes the lock and listens but never runs its event
// loop (a hung instance) until it is killed (or 30 s pass).

#include "single_instance.h"

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QtEnvironmentVariables>

#include <chrono>
#include <iostream>
#include <thread>

namespace hio::gui::test {

inline int runSingleInstanceHelper(int argc, char** argv, const QString& spec) {
    const QCoreApplication app(argc, argv);
    const QStringList parts = spec.split(QLatin1Char('|'));
    if (parts.size() != 2) {
        return 2;
    }
    SingleInstance instance(parts[0], parts[1]);
    if (instance.start() != SingleInstance::Result::Primary) {
        std::cout << "not-primary\n" << std::flush;
        return 1;
    }
    std::cout << "primary\n" << std::flush;
    if (qEnvironmentVariableIntValue("HIO_TEST_SINGLE_INSTANCE_HUNG") == 1) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        return 0;
    }
    QObject::connect(&instance, &SingleInstance::activationRequested, &app, [] {
        std::cout << "activated\n" << std::flush;
        // Let the answer go out first.
        QTimer::singleShot(std::chrono::milliseconds(200), [] { QCoreApplication::quit(); });
    });
    QTimer::singleShot(std::chrono::seconds(30), [] { QCoreApplication::quit(); });
    return QCoreApplication::exec();
}

} // namespace hio::gui::test
