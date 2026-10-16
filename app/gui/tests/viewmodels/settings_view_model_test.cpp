// SettingsViewModel and DependencyStatus (plan/features/0006-m3-flash-settings.md, E1): the
// sections, live theme, validated writes, the default J-Link speed with "Automatic speed"
// (ADR-0111, ADR-0110 warning), the custom J-Link / nRF Util paths (checked before they are saved),
// dependency detection results and the read-only mode (settings.newer_schema).

#include "gui_test_support.h"
#include "hio/core/settings.h"
#include "pages/firmware_services.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/dependency_status.h"
#include "viewmodels/settings_view_model.h"

#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace {

using hio::gui::AppViewModel;
using hio::gui::DependencyStatus;
using hio::gui::FirmwareServices;
using hio::gui::SettingsViewModel;
using hio::gui::test::waitFor;

class SettingsTest : public hio::gui::test::GuiTest {
protected:
    void SetUp() override {
        GuiTest::SetUp();
        qputenv("HIO_NRFUTIL", (root() / "no-nrfutil" / "nrfutil").string().c_str());
        m_app = makeApp();
    }
    void TearDown() override {
        m_app.reset();
        qunsetenv("HIO_NRFUTIL");
        GuiTest::TearDown();
    }

    [[nodiscard]] AppViewModel& app() { return *m_app; }
    [[nodiscard]] SettingsViewModel& settings() {
        return *qobject_cast<SettingsViewModel*>(
            m_app->globalPageViewModel(SettingsViewModel::pageId));
    }
    void restart() { restartWith({}); }
    // Closes the app, lets `edit` change the settings file directly and starts the app again.
    void restartWith(const std::function<void()>& edit) {
        m_app.reset();
        if (edit) {
            edit();
        }
        m_app = makeApp();
    }

private:
    std::unique_ptr<AppViewModel> m_app;
};

TEST_F(SettingsTest, SectionsInRegistryOrder) {
    QStringList ids;
    for (const QVariant& section : settings().sections()) {
        ids.append(section.toMap().value(QStringLiteral("sectionId")).toString());
    }
    EXPECT_EQ(ids, (QStringList{QStringLiteral("general"), QStringLiteral("jlink"),
                                QStringLiteral("nrfutil"), QStringLiteral("about")}));
    EXPECT_EQ(settings().sections().at(1).toMap().value(QStringLiteral("title")).toString(),
              QStringLiteral("J-Link"));
}

TEST_F(SettingsTest, GeneralItemsApplyImmediately) {
    settings().setTheme(QStringLiteral("dark"));
    EXPECT_EQ(app().theme(), QStringLiteral("dark")); // live
    EXPECT_TRUE(settings().restoreTabs());
    settings().setRestoreTabs(false);
    EXPECT_FALSE(app().settingsStore()->value(QStringLiteral("ui/restoreTabs")).toBool());
    EXPECT_TRUE(settings().writeError().isEmpty());
    ASSERT_GE(settings().languages().size(), 3);
    EXPECT_EQ(settings().languages().at(0).toMap().value(QStringLiteral("name")).toString(),
              QStringLiteral("System"));
}

TEST_F(SettingsTest, DefaultSpeedIsAutomaticAndKeepsTheFixedValue) {
    EXPECT_TRUE(settings().autoSpeed());
    EXPECT_EQ(settings().defaultSpeedKhz(), hio::gui::ConnectionOptions::defaultFixedSpeedKhz);
    settings().setAutoSpeed(false);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("jlink/speedKhz")).toInt(), 4000);
    settings().setDefaultSpeedKhz(100);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("jlink/speedKhz")).toInt(), 100);
    EXPECT_FALSE(settings().speedWarning().isEmpty()); // ADR-0110: below 125 kHz
    settings().setDefaultSpeedKhz(60000);              // out of range: not saved
    EXPECT_EQ(settings().defaultSpeedKhz(), 100);
    settings().setAutoSpeed(true);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("jlink/speedKhz")).toInt(), 0);
    EXPECT_EQ(settings().defaultSpeedKhz(), 100); // kept while automatic
    EXPECT_TRUE(settings().speedWarning().isEmpty());

    // The same model as the ⋯ Advanced popover: a new tab starts with these defaults.
    settings().setAutoSpeed(false);
    settings().setDefaultSpeedKhz(1000);
    restart();
    EXPECT_FALSE(settings().autoSpeed());
    EXPECT_EQ(settings().defaultSpeedKhz(), 1000);
}

TEST_F(SettingsTest, ConnectionDefaults) {
    EXPECT_GT(settings().defaultLatencyMs(), 0); // the profile's value while 0 is stored
    settings().setDefaultLatencyMs(25);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("rtt/latencyMs")).toInt(), 25);
    settings().setDefaultLatencyMs(5000); // invalid: not saved
    EXPECT_EQ(settings().defaultLatencyMs(), 25);
    settings().setAutoReconnect(false);
    EXPECT_FALSE(app().settingsStore()->value(QStringLiteral("rtt/autoReconnect")).toBool());
    settings().setResetOnConnect(true);
    EXPECT_TRUE(app().settingsStore()->value(QStringLiteral("rtt/resetOnOpen")).toBool());
}

TEST_F(SettingsTest, DependencyDetection) {
    DependencyStatus* status = settings().dependencies();
    EXPECT_EQ(status->jlinkState(), DependencyStatus::Unknown); // nothing before the page is shown
    settings().pageShown();
    ASSERT_TRUE(waitFor([&] {
        return status->jlinkState() != DependencyStatus::Checking &&
               status->jlinkState() != DependencyStatus::Unknown &&
               status->nrfUtilState() != DependencyStatus::Checking;
    }));
    EXPECT_EQ(status->jlinkState(), DependencyStatus::Found)
        << status->jlinkError().detail().toStdString();
    EXPECT_EQ(status->jlinkVersion(), QStringLiteral("8.10"));
    EXPECT_TRUE(status->jlinkPath().contains(QStringLiteral("fake_jlink")));
    EXPECT_EQ(status->jlinkSourceText(),
              QStringLiteral("Set by the environment variable HIO_JLINK_LIBRARY"));
    EXPECT_EQ(status->nrfUtilState(), DependencyStatus::Problem);
    EXPECT_EQ(status->nrfUtilError().code(), QStringLiteral("dependency.nrfutil_not_found"));
    EXPECT_EQ(settings().nrfUtilStatusText(),
              QStringLiteral("Not found — needed only for LTE modem firmware"));
    EXPECT_TRUE(settings().aboutText().contains(QStringLiteral("J-Link Software: 8.10")));

    // With nRF Util (the fake).
    qputenv("HIO_NRFUTIL", QByteArray(HIO_FAKE_NRFUTIL));
    status->refresh();
    ASSERT_TRUE(waitFor([&] { return status->nrfUtilState() == DependencyStatus::Found; }));
    EXPECT_TRUE(status->nrfUtilVersion().startsWith(QStringLiteral("7.13.0")));
}

TEST_F(SettingsTest, CustomPathsAreCheckedBeforeTheyAreSaved) {
    settings().setJlinkLibraryMode(QStringLiteral("custom"));
    EXPECT_EQ(settings().jlinkLibraryMode(), QStringLiteral("custom"));
    settings().setJlinkLibraryPath(
        QUrl::fromLocalFile(QString::fromStdString((root() / "nothing.so").string())));
    EXPECT_TRUE(settings().jlinkPathChecking());
    ASSERT_TRUE(waitFor([&] { return !settings().jlinkPathChecking(); }));
    EXPECT_FALSE(settings().jlinkPathError().isEmpty());
    EXPECT_TRUE(app().settingsStore()->value(FirmwareServices::jlinkPathKey).toString().isEmpty());

    // A loadable library is saved.
    settings().setJlinkLibraryPath(QUrl::fromLocalFile(QStringLiteral(HIO_FAKE_JLINK_LIBRARY)));
    ASSERT_TRUE(waitFor([&] { return !settings().jlinkPathChecking(); }));
    EXPECT_TRUE(settings().jlinkPathError().isEmpty()) << settings().jlinkPathError().toStdString();
    EXPECT_EQ(app().settingsStore()->value(FirmwareServices::jlinkPathKey).toString(),
              QStringLiteral(HIO_FAKE_JLINK_LIBRARY));
    settings().setJlinkLibraryMode(QStringLiteral("auto"));
    EXPECT_TRUE(app().settingsStore()->value(FirmwareServices::jlinkPathKey).toString().isEmpty());
    EXPECT_EQ(settings().jlinkLibraryMode(), QStringLiteral("auto"));

    // nRF Util: a missing program is not saved, the fake is.
    settings().setNrfUtilPath(
        QUrl::fromLocalFile(QString::fromStdString((root() / "nrfutil").string())));
    ASSERT_TRUE(waitFor([&] { return !settings().nrfUtilPathChecking(); }));
    EXPECT_FALSE(settings().nrfUtilPathError().isEmpty());
    EXPECT_TRUE(
        app().settingsStore()->value(FirmwareServices::nrfUtilPathKey).toString().isEmpty());
    settings().setNrfUtilPath(QUrl::fromLocalFile(QStringLiteral(HIO_FAKE_NRFUTIL)));
    ASSERT_TRUE(waitFor([&] { return !settings().nrfUtilPathChecking(); }));
    EXPECT_EQ(app().settingsStore()->value(FirmwareServices::nrfUtilPathKey).toString(),
              QStringLiteral(HIO_FAKE_NRFUTIL));
    EXPECT_EQ(settings().nrfUtilMode(), QStringLiteral("custom"));
}

TEST_F(SettingsTest, ReadOnlySettingsFile) {
    // A file of a newer schema is opened read-only.
    restartWith([this] {
        QSettings raw(QString::fromStdU16String(settingsFile().u16string()), QSettings::IniFormat);
        raw.setValue(QStringLiteral("meta/schemaVersion"), hio::currentSettingsSchema + 1);
        raw.sync();
    });
    EXPECT_TRUE(settings().readOnly());
    settings().setRestoreTabs(false);
    EXPECT_FALSE(settings().writeError().isEmpty());
    EXPECT_TRUE(settings().restoreTabs());
}

} // namespace
