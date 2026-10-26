// SettingsStore: key table, validation, valueChanged, sync, read-only mode (plan 0004, A5).

#include "gui_test_support.h"
#include "hio/core/error.h"
#include "hio/core/settings.h"
#include "settings_store.h"

#include <QMetaType>
#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QStringLiteral>
#include <QVariant>

#include <gtest/gtest.h>

#include <array>
#include <optional>

namespace {

using hio::ErrorCode;
using hio::gui::SettingKey;
using hio::gui::SettingsStore;
namespace validators = hio::gui::validators;

class SettingsStoreTest : public hio::gui::test::GuiTest {
protected:
    static void registerTestKeys(SettingsStore& store) {
        const std::array keys{
            SettingKey{.path = QStringLiteral("ui/theme"),
                       .type = QMetaType::QString,
                       .defaultValue = QStringLiteral("system"),
                       .validator =
                           validators::oneOf({QStringLiteral("system"), QStringLiteral("light"),
                                              QStringLiteral("dark")})},
            SettingKey{.path = QStringLiteral("ui/restoreTabs"),
                       .type = QMetaType::Bool,
                       .defaultValue = true,
                       .validator = {}},
            SettingKey{.path = QStringLiteral("jlink/speedKhz"),
                       .type = QMetaType::Int,
                       .defaultValue = 0,
                       .validator = validators::intRange(5, 50000, true)},
            SettingKey{.path = QStringLiteral("probes/*/speedKhz"),
                       .type = QMetaType::Int,
                       .defaultValue = 0,
                       .validator = validators::intRange(5, 50000, true)},
        };
        store.registerKeys(keys);
    }
};

TEST_F(SettingsStoreTest, DefaultsAndUnregisteredKeys) {
    SettingsStore store(openSettings());
    registerTestKeys(store);
    EXPECT_EQ(store.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("system"));
    EXPECT_TRUE(store.value(QStringLiteral("ui/restoreTabs")).toBool());
    EXPECT_FALSE(store.value(QStringLiteral("ui/unknown")).isValid());
    const auto status = store.setValue(QStringLiteral("ui/unknown"), 1);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::UsageInvalidArgument);
    EXPECT_FALSE(store.readOnly());
}

TEST_F(SettingsStoreTest, WritesAreValidatedSyncedAndSignalled) {
    SettingsStore store(openSettings());
    registerTestKeys(store);
    QSignalSpy changed(&store, &SettingsStore::valueChanged);

    ASSERT_TRUE(store.setValue(QStringLiteral("ui/theme"), QStringLiteral("dark")));
    ASSERT_EQ(changed.count(), 1);
    EXPECT_EQ(changed.at(0).at(0).toString(), QStringLiteral("ui/theme"));
    EXPECT_EQ(store.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("dark"));

    // Visible to another reader at once (sync, ADR-0058).
    const QSettings raw(QString::fromStdU16String(settingsFile().u16string()),
                        QSettings::IniFormat);
    EXPECT_EQ(raw.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("dark"));

    // Wrong type, a value the validator rejects: nothing is written, no signal.
    for (const QVariant& bad : {QVariant(QStringLiteral("blue")), QVariant(3)}) {
        const auto status = store.setValue(QStringLiteral("ui/theme"), bad);
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().code, ErrorCode::UsageInvalidArgument);
    }
    EXPECT_FALSE(store.setValue(QStringLiteral("jlink/speedKhz"), 4));
    EXPECT_FALSE(store.setValue(QStringLiteral("jlink/speedKhz"), 50001));
    EXPECT_FALSE(store.setValue(QStringLiteral("jlink/speedKhz"), 2.5));
    EXPECT_FALSE(store.setValue(QStringLiteral("ui/restoreTabs"), QStringLiteral("true")));
    EXPECT_EQ(changed.count(), 1);
    EXPECT_EQ(store.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("dark"));

    // 0 = the profile's value; QML numbers arrive as double.
    EXPECT_TRUE(store.setValue(QStringLiteral("jlink/speedKhz"), 0));
    EXPECT_TRUE(store.setValue(QStringLiteral("jlink/speedKhz"), 4000.0));
    EXPECT_EQ(store.value(QStringLiteral("jlink/speedKhz")).toInt(), 4000);
    EXPECT_TRUE(store.setValue(QStringLiteral("ui/restoreTabs"), false));
    EXPECT_FALSE(store.value(QStringLiteral("ui/restoreTabs")).toBool());
}

TEST_F(SettingsStoreTest, PatternKeys) {
    SettingsStore store(openSettings());
    registerTestKeys(store);
    EXPECT_TRUE(store.isRegistered(QStringLiteral("probes/683012345/speedKhz")));
    EXPECT_FALSE(store.isRegistered(QStringLiteral("probes//speedKhz")));
    EXPECT_FALSE(store.isRegistered(QStringLiteral("probes/1/2/speedKhz")));
    ASSERT_TRUE(store.setValue(QStringLiteral("probes/683012345/speedKhz"), 1000));
    EXPECT_EQ(store.value(QStringLiteral("probes/683012345/speedKhz")).toInt(), 1000);
    EXPECT_EQ(store.value(QStringLiteral("probes/1/speedKhz")).toInt(), 0);
}

TEST_F(SettingsStoreTest, StoredValuesAreReadBackAndInvalidOnesIgnored) {
    {
        QSettings raw(QString::fromStdU16String(settingsFile().u16string()), QSettings::IniFormat);
        raw.setValue(QStringLiteral("meta/schemaVersion"), hio::currentSettingsSchema);
        raw.setValue(QStringLiteral("ui/theme"), QStringLiteral("purple"));
        raw.setValue(QStringLiteral("jlink/speedKhz"), QStringLiteral("12000"));
        raw.setValue(QStringLiteral("ui/restoreTabs"), QStringLiteral("false"));
    }
    SettingsStore store(openSettings());
    registerTestKeys(store);
    EXPECT_EQ(store.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("system"));
    EXPECT_EQ(store.value(QStringLiteral("jlink/speedKhz")).toInt(), 12000);
    EXPECT_FALSE(store.value(QStringLiteral("ui/restoreTabs")).toBool());
}

TEST_F(SettingsStoreTest, NewerSchemaIsReadOnly) {
    {
        QSettings raw(QString::fromStdU16String(settingsFile().u16string()), QSettings::IniFormat);
        raw.setValue(QStringLiteral("meta/schemaVersion"), hio::currentSettingsSchema + 1);
        raw.setValue(QStringLiteral("ui/theme"), QStringLiteral("light"));
    }
    SettingsStore store(openSettings());
    registerTestKeys(store);
    EXPECT_TRUE(store.readOnly());
    EXPECT_EQ(store.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("light"));
    const auto status = store.setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::SettingsNewerSchema);
}

TEST_F(SettingsStoreTest, WithoutAFileValuesLiveInMemory) {
    SettingsStore store(std::nullopt);
    registerTestKeys(store);
    EXPECT_EQ(store.settings(), nullptr);
    ASSERT_TRUE(store.setValue(QStringLiteral("ui/theme"), QStringLiteral("light")));
    EXPECT_EQ(store.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("light"));
    EXPECT_FALSE(store.setValue(QStringLiteral("ui/theme"), QStringLiteral("x")));
}

TEST_F(SettingsStoreTest, DuplicateKeysAreAProgrammingError) {
    SettingsStore store(std::nullopt);
    registerTestKeys(store);
    const std::array duplicate{SettingKey{.path = QStringLiteral("ui/theme"),
                                          .type = QMetaType::Int,
                                          .defaultValue = 1,
                                          .validator = {}}};
    EXPECT_DEBUG_DEATH(store.registerKeys(duplicate), "duplicate setting key");
    EXPECT_EQ(store.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("system"));
}

} // namespace
