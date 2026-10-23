#include "hio/core/app_identity.h"

#include <QCoreApplication>
#include <QStandardPaths>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleMock(&argc, argv);
    // Tests never touch the user's real settings or data directories.
    QStandardPaths::setTestModeEnabled(true);
    const QCoreApplication app(argc, argv);
    hio::initApplicationIdentity();
    return RUN_ALL_TESTS();
}
