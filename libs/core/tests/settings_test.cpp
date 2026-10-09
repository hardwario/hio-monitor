#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"
#include "settings_internal.h"

#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QVariant>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::SettingKey;
using hio::Settings;

constexpr SettingKey<bool> flagKey{.path = "test/flag", .defaultValue = true};
constexpr SettingKey<int> countKey{.path = "test/count", .defaultValue = 7};
const SettingKey<std::string> nameKey{.path = "test/name", .defaultValue = "default"};
const SettingKey<fs::path> pathKey{.path = "test/path", .defaultValue = fs::path("none")};

fs::path settingsFile(const QTemporaryDir& dir) {
    return fs::path(dir.path().toStdU16String()) / "Monitor.ini";
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeRaw(const fs::path& file, const QString& key, const QVariant& value) {
    QSettings raw(QString::fromStdU16String(file.u16string()), QSettings::IniFormat);
    raw.setValue(key, value);
    raw.sync();
}

TEST(Settings, NewFileGetsCurrentSchemaAndDefaults) {
    const QTemporaryDir dir;
    const fs::path file = settingsFile(dir);
    auto settings = Settings::openFile(file);
    ASSERT_TRUE(settings.has_value());
    EXPECT_FALSE(settings->isReadOnly());
    EXPECT_EQ(settings->schemaVersion(), hio::currentSettingsSchema);
    EXPECT_TRUE(settings->get(flagKey));
    EXPECT_EQ(settings->get(countKey), 7);
    EXPECT_EQ(settings->get(nameKey), "default");
    EXPECT_EQ(settings->get(pathKey), fs::path("none"));
    EXPECT_NE(readFile(file).find("schemaVersion=1"), std::string::npos);
}

TEST(Settings, ValuesPersistAcrossReopen) {
    const QTemporaryDir dir;
    const fs::path file = settingsFile(dir);
    const fs::path unicodePath = fs::path(u8"/tmp/Čeština/žluťoučký kůň.hex");
    {
        auto settings = Settings::openFile(file);
        ASSERT_TRUE(settings.has_value());
        ASSERT_TRUE(settings->set(flagKey, false));
        ASSERT_TRUE(settings->set(countKey, -42));
        ASSERT_TRUE(settings->set(nameKey, std::string("Čeština")));
        ASSERT_TRUE(settings->set(pathKey, unicodePath));
        EXPECT_EQ(settings->get(countKey), -42); // visible before sync
        ASSERT_TRUE(settings->sync());
    }
    auto settings = Settings::openFile(file);
    ASSERT_TRUE(settings.has_value());
    EXPECT_FALSE(settings->get(flagKey));
    EXPECT_EQ(settings->get(countKey), -42);
    EXPECT_EQ(settings->get(nameKey), "Čeština");
    EXPECT_EQ(settings->get(pathKey), unicodePath);
}

TEST(Settings, InvalidTypesFallBackToDefaults) {
    const QTemporaryDir dir;
    const fs::path file = settingsFile(dir);
    writeRaw(file, QStringLiteral("meta/schemaVersion"), 1);
    writeRaw(file, QStringLiteral("test/flag"), QStringLiteral("maybe"));
    writeRaw(file, QStringLiteral("test/count"), QStringLiteral("seven"));
    writeRaw(file, QStringLiteral("test/name"),
             QStringList{QStringLiteral("a"), QStringLiteral("b")});
    auto settings = Settings::openFile(file);
    ASSERT_TRUE(settings.has_value());
    EXPECT_TRUE(settings->get(flagKey));
    EXPECT_EQ(settings->get(countKey), 7);
    EXPECT_EQ(settings->get(nameKey), "default");
}

TEST(Settings, NewerSchemaIsReadOnlyAndNeverWritten) {
    const QTemporaryDir dir;
    const fs::path file = settingsFile(dir);
    writeRaw(file, QStringLiteral("meta/schemaVersion"), 99);
    writeRaw(file, QStringLiteral("test/count"), 5);
    const std::string before = readFile(file);
    {
        auto settings = Settings::openFile(file);
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->isReadOnly());
        EXPECT_EQ(settings->schemaVersion(), 99);
        EXPECT_EQ(settings->get(countKey), 5);
        const hio::Status status = settings->set(countKey, 6);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, hio::ErrorCode::SettingsNewerSchema);
        EXPECT_TRUE(settings->sync());
    }
    EXPECT_EQ(readFile(file), before);
}

// ---- migration mechanism (test-only migration list via settings_internal.h) -----------------

// Records the migrations run by the test migration functions (plain function pointers, so the
// record has to be reachable without captures). Only used from the test thread.
std::vector<int>& appliedMigrations() {
    static std::vector<int> applied;
    return applied;
}

hio::Status migrateTo2(QSettings& raw) {
    appliedMigrations().push_back(2);
    raw.setValue(QStringLiteral("test/count"), raw.value(QStringLiteral("old/count")));
    raw.remove(QStringLiteral("old/count"));
    return {};
}

hio::Status migrateTo3(QSettings& raw) {
    appliedMigrations().push_back(3);
    raw.setValue(QStringLiteral("test/name"), QStringLiteral("migrated"));
    return {};
}

hio::Status failingMigration(QSettings& /*raw*/) {
    return hio::fail(hio::ErrorCode::IoWriteFailed);
}

constexpr std::array<hio::detail::SettingsMigration, 2> testMigrations{{
    {.toVersion = 2, .apply = migrateTo2},
    {.toVersion = 3, .apply = migrateTo3},
}};

TEST(SettingsMigration, RunsPendingMigrationsInOrder) {
    const QTemporaryDir dir;
    const fs::path file = settingsFile(dir);
    writeRaw(file, QStringLiteral("meta/schemaVersion"), 1);
    writeRaw(file, QStringLiteral("old/count"), 11);
    appliedMigrations().clear();

    auto settings = hio::detail::openSettingsFile(file, 3, testMigrations);
    ASSERT_TRUE(settings.has_value());
    EXPECT_EQ(appliedMigrations(), (std::vector<int>{2, 3}));
    EXPECT_EQ(settings->schemaVersion(), 3);
    EXPECT_EQ(settings->get(countKey), 11);
    EXPECT_EQ(settings->get(nameKey), "migrated");
    EXPECT_NE(readFile(file).find("schemaVersion=3"), std::string::npos);
}

TEST(SettingsMigration, SkipsMigrationsAlreadyApplied) {
    const QTemporaryDir dir;
    const fs::path file = settingsFile(dir);
    writeRaw(file, QStringLiteral("meta/schemaVersion"), 2);
    appliedMigrations().clear();
    auto settings = hio::detail::openSettingsFile(file, 3, testMigrations);
    ASSERT_TRUE(settings.has_value());
    EXPECT_EQ(appliedMigrations(), (std::vector<int>{3}));
}

TEST(SettingsMigration, NewFileRunsNoMigrations) {
    const QTemporaryDir dir;
    appliedMigrations().clear();
    auto settings = hio::detail::openSettingsFile(settingsFile(dir), 3, testMigrations);
    ASSERT_TRUE(settings.has_value());
    EXPECT_TRUE(appliedMigrations().empty());
    EXPECT_EQ(settings->schemaVersion(), 3);
}

TEST(SettingsMigration, FailureIsReported) {
    const QTemporaryDir dir;
    const fs::path file = settingsFile(dir);
    writeRaw(file, QStringLiteral("meta/schemaVersion"), 1);
    const std::array<hio::detail::SettingsMigration, 1> migrations{{
        {.toVersion = 2, .apply = failingMigration},
    }};
    auto settings = hio::detail::openSettingsFile(file, 2, migrations);
    ASSERT_FALSE(settings.has_value());
    EXPECT_EQ(settings.error().code, hio::ErrorCode::IoWriteFailed);
}

TEST(SettingsMigration, FailureLeavesTheFileUnchanged) {
    const QTemporaryDir dir;
    const fs::path file = settingsFile(dir);
    writeRaw(file, QStringLiteral("meta/schemaVersion"), 1);
    writeRaw(file, QStringLiteral("old/count"), 11);
    // migrateTo2 succeeds and changes keys, then the step to 3 fails.
    const std::array<hio::detail::SettingsMigration, 2> migrations{{
        {.toVersion = 2, .apply = migrateTo2},
        {.toVersion = 3, .apply = failingMigration},
    }};
    {
        auto settings = hio::detail::openSettingsFile(file, 3, migrations);
        ASSERT_FALSE(settings.has_value());
    }
    const QSettings raw(QString::fromStdU16String(file.u16string()), QSettings::IniFormat);
    EXPECT_EQ(raw.value(QStringLiteral("meta/schemaVersion")).toInt(), 1);
    EXPECT_EQ(raw.value(QStringLiteral("old/count")).toInt(), 11);
    EXPECT_FALSE(raw.contains(QStringLiteral("test/count")));
}

TEST(SettingsMigration, MissingStepIsRejected) {
    const QTemporaryDir dir;
    const fs::path file = settingsFile(dir);
    writeRaw(file, QStringLiteral("meta/schemaVersion"), 1);
    const std::array<hio::detail::SettingsMigration, 1> migrations{{
        {.toVersion = 3, .apply = migrateTo3},
    }};
    appliedMigrations().clear();
    auto settings = hio::detail::openSettingsFile(file, 3, migrations);
    ASSERT_FALSE(settings.has_value());
    EXPECT_EQ(settings.error().code, hio::ErrorCode::InternalUnexpected);
    EXPECT_TRUE(appliedMigrations().empty());
    const QSettings raw(QString::fromStdU16String(file.u16string()), QSettings::IniFormat);
    EXPECT_EQ(raw.value(QStringLiteral("meta/schemaVersion")).toInt(), 1);
}

} // namespace
