// BleLock (plan 0011 B2, ADR-0083): held, stale, sanitised names; and the BLE profile (B3).

#include "hio/core/error.h"
#include "hio/device/ble_lock.h"
#include "hio/device/probe_lock.h"
#include "hio/device/profile.h"
#include "lock_holder.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QList>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::device::BleLock;
using hio::device::sanitisedBleId;

class BleLockTest : public ::testing::Test {
protected:
    [[nodiscard]] fs::path dir() const { return fs::path(m_dir.path().toStdString()) / "locks"; }

private:
    QTemporaryDir m_dir;
};

TEST(SanitisedBleId, KeepsLettersDigitsAndDashes) {
    EXPECT_EQ(sanitisedBleId("F1:22:33:44:55:66"), "F1-22-33-44-55-66");
    EXPECT_EQ(sanitisedBleId("{6A0B3F1C-1D2E-4F50-8A9B-0C1D2E3F4A5B}"),
              "-6A0B3F1C-1D2E-4F50-8A9B-0C1D2E3F4A5B-");
    EXPECT_EQ(sanitisedBleId("a/b\\c d.e"), "a-b-c-d-e");
    EXPECT_EQ(sanitisedBleId(sanitisedBleId("F1:22")), sanitisedBleId("F1:22")); // idempotent
}

TEST_F(BleLockTest, AcquireNamesTheFileAndReleases) {
    {
        auto lock = BleLock::acquire("F1:22:33:44:55:66", dir());
        ASSERT_TRUE(lock) << lock.error().message;
        EXPECT_EQ(lock->deviceId(), "F1:22:33:44:55:66");
        EXPECT_TRUE(fs::exists(dir() / "ble-F1-22-33-44-55-66.lock"));
        const auto owner = BleLock::owner("F1:22:33:44:55:66", dir());
        ASSERT_TRUE(owner.has_value());
        EXPECT_EQ(owner.value_or(hio::device::ProbeLockOwner{}).pid,
                  QCoreApplication::applicationPid());
        // The retention passes the sanitised id from a file name: the same lock.
        EXPECT_TRUE(BleLock::owner("F1-22-33-44-55-66", dir()).has_value());
    }
    EXPECT_FALSE(BleLock::owner("F1:22:33:44:55:66", dir()).has_value());
}

TEST_F(BleLockTest, SecondAcquireInTheSameProcessIsAlreadyUsed) {
    auto first = BleLock::acquire("F1:22:33:44:55:66", dir());
    ASSERT_TRUE(first);
    const auto second = BleLock::acquire("F1:22:33:44:55:66", dir());
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, ErrorCode::BleInUseByOtherProcess);
    EXPECT_EQ(second.error().message,
              "The device F1:22:33:44:55:66 is already used by this application.");
}

TEST_F(BleLockTest, HeldByAnotherProcessNamesTheOwner) {
    fs::create_directories(dir());
    hio::device::test::LockHolder holder(
        QString::fromStdString((dir() / "ble-F1-22-33-44-55-66.lock").string()));
    ASSERT_TRUE(holder.waitUntilLocked());
    const auto lock = BleLock::acquire("F1:22:33:44:55:66", dir());
    ASSERT_FALSE(lock);
    EXPECT_EQ(lock.error().code, ErrorCode::BleInUseByOtherProcess);
    EXPECT_NE(lock.error().message.find("is used by another program"), std::string::npos);
    EXPECT_NE(lock.error().message.find(std::to_string(holder.pid())), std::string::npos);
    const auto owner = BleLock::owner("F1:22:33:44:55:66", dir());
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(owner.value_or(hio::device::ProbeLockOwner{}).pid, holder.pid());
    holder.release();
    EXPECT_FALSE(BleLock::owner("F1:22:33:44:55:66", dir()).has_value());
    EXPECT_TRUE(BleLock::acquire("F1:22:33:44:55:66", dir()));
}

TEST_F(BleLockTest, StaleLockOfThisPidNotHeldIsTakenOver) {
    // A lock file with our PID that this process does not hold (a dead process whose PID we got,
    // review R15): taken over, as for probes.
    QByteArray content;
    {
        auto own = BleLock::acquire("AA:BB", dir());
        ASSERT_TRUE(own);
        QFile file(QString::fromStdString((dir() / "ble-AA-BB.lock").string()));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        content = file.readAll();
    }
    {
        QFile file(QString::fromStdString((dir() / "ble-AA-BB.lock").string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }
    EXPECT_FALSE(BleLock::owner("AA:BB", dir()).has_value());
    EXPECT_TRUE(BleLock::acquire("AA:BB", dir()));
}

TEST_F(BleLockTest, ProbeAndBleLocksAreIndependent) {
    auto probe = hio::device::ProbeLock::acquire(1, dir());
    ASSERT_TRUE(probe);
    auto ble = BleLock::acquire("1", dir());
    EXPECT_TRUE(ble);
}

// ---- BLE profile (B3)
// -----------------------------------------------------------------------------

TEST(BleProfile, ChesterHasTheNusConsole) {
    const auto* chester = hio::device::ProfileRegistry::find("chester");
    ASSERT_NE(chester, nullptr);
    ASSERT_TRUE(chester->ble.has_value());
    const hio::device::BleProfile ble = chester->ble.value_or(hio::device::BleProfile{});
    EXPECT_EQ(ble.namePattern, "CHESTER");
    EXPECT_EQ(ble.serviceUuid, "6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    EXPECT_EQ(ble.rxUuid, "6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    EXPECT_EQ(ble.txUuid, "6e400003-b5a3-f393-e0a9-e50e24dcca9e");
    EXPECT_EQ(ble.shellPrompt, "bt_nus:~$ ");
    EXPECT_EQ(ble.consoleTargetId, "app");
    ASSERT_NE(hio::device::findTarget(*chester, ble.consoleTargetId), nullptr);
    EXPECT_EQ(hio::device::passkeyHint(*chester).toStdString(),
              "The passkey is 123456 unless it was changed when the device was configured.");
    ASSERT_EQ(ble.pairingGuide.size(), 2U);
    EXPECT_EQ(ble.pairingGuide[0].illustrationSlot, "ble-enable");
    EXPECT_EQ(ble.pairingGuide[1].illustrationSlot, "ble-pair-os");
}

TEST(BleProfile, FindByAdvertisement) {
    using hio::device::ProfileRegistry;
    const auto* chester = ProfileRegistry::find("chester");
    const std::vector<std::string> none;
    EXPECT_EQ(ProfileRegistry::findByAdvertisement("CHESTER-ABC123", none), chester);
    EXPECT_EQ(ProfileRegistry::findByAdvertisement("chester-abc", none), nullptr); // case
    // ADR-0142: the NUS UUID alone is no CHESTER (Ruuvi tags advertise it too).
    const std::vector<std::string> nus{"6E400001-B5A3-F393-E0A9-E50E24DCCA9E"};
    EXPECT_EQ(ProfileRegistry::findByAdvertisement("Ruuvi B0C2", nus), nullptr);
    EXPECT_EQ(ProfileRegistry::findByAdvertisement("CHESTER-ABC", nus), chester);
    const std::vector<std::string> other{"0000180f-0000-1000-8000-00805f9b34fb"};
    EXPECT_EQ(ProfileRegistry::findByAdvertisement("Phone", other), nullptr);
}

} // namespace
