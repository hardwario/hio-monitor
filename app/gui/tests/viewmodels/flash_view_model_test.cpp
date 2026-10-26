// FlashViewModel, FirmwareSourceViewModel, RecentSourcesModel and FirmwareLoader
// (plan/features/0006-m3-flash-settings.md, D1-D4) against the fake J-Link library and the fake
// nRF Util: the flash state machine for each kind (incl. the LTE bundle step 1/2 -> 2/2 and a
// modem failure), cross-target phases, flashing from a disconnected tab, cancel, kind-mismatch
// hints, catalog ID validation and download (local HTTP fixture), the recent list, the
// confirmation texts, busy (internal.busy) and the interrupted flash (ADR-0109).

#include "gui_test_support.h"
#include "hex_builder.h"
#include "hio/core/error.h"
#include "hio/device/device_session.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/nrfutil/nrfutil.h"
#include "http_fixture.h"
#include "pages/firmware_services.h"
#include "settings_store.h"
#include "support/fake_jlink_scenario.h"
#include "support/test_page_view_model.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/dependency_status.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/firmware_source_view_model.h"
#include "viewmodels/flash_view_model.h"
#include "viewmodels/recent_sources_model.h"
#include "workers/firmware_loader.h"
#include "zip_builder.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtEnvironmentVariables>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::firmware::FirmwareImage;
using hio::firmware::Segment;
using hio::gui::AppViewModel;
using hio::gui::DeviceTabViewModel;
using hio::gui::FirmwareSourceViewModel;
using hio::gui::FlashViewModel;
using hio::gui::RecentSourcesModel;
using hio::gui::test::waitFor;
using Kind = FirmwareSourceViewModel::Kind;

// One probe with RTT; `chip` is the scenario's chip object (default: an nRF52840).
std::string probeScenario(const std::string& chip = "{}") {
    return std::string(R"({"libraryVersion": 81000, "probes": [{"serialNumber": 801000001,
        "product": "Test Probe", "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
        "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\n"}]},
        "chip": )") +
           chip + "}]}";
}

QString randomId() { return QUuid::createUuid().toString(QUuid::Id128); }

class FlashTest : public hio::gui::test::GuiTest {
protected:
    void SetUp() override {
        GuiTest::SetUp();
        qputenv("HIO_NRFUTIL", (root() / "no-nrfutil" / "nrfutil").string().c_str());
    }
    void TearDown() override {
        m_app.reset();
        qunsetenv("HIO_NRFUTIL");
        qunsetenv("HIO_FAKE_NRFUTIL_MODE");
        qunsetenv("HIO_CATALOG_URL");
        GuiTest::TearDown();
    }

    void start(const std::string& scenario, const QString& target = QStringLiteral("app")) {
        m_scenario = scenario;
        useScenario(m_scenario.c_str());
        m_app = makeApp();
        tab().configure(QStringLiteral("chester"), target, 801000001, QStringLiteral("Test Probe"));
    }

    [[nodiscard]] AppViewModel& app() { return *m_app; }
    [[nodiscard]] DeviceTabViewModel& tab() { return *m_app->currentTabViewModel(); }
    [[nodiscard]] FlashViewModel& flash() {
        return *qobject_cast<FlashViewModel*>(tab().pageViewModel(FlashViewModel::pageId));
    }
    [[nodiscard]] FirmwareSourceViewModel& source() { return *flash().source(); }

    [[nodiscard]] fs::path writeFile(const std::string& name, const QByteArray& bytes) const {
        const fs::path path = root() / name;
        QFile file(QString::fromStdString(path.string()));
        EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(bytes);
        return path;
    }

    [[nodiscard]] static FirmwareImage image(std::uint32_t address = 0x1000,
                                             std::size_t size = 4096, std::uint8_t value = 0x5A) {
        std::vector<Segment> segments;
        segments.push_back(
            Segment{.address = address, .data = std::vector<std::byte>(size, std::byte{value})});
        return *FirmwareImage::fromSegments(std::move(segments));
    }

    [[nodiscard]] fs::path hexFile(const std::string& name = "app.hex",
                                   const FirmwareImage& content = image()) const {
        return writeFile(name, QByteArray::fromStdString(hio::firmware::test::toHex(content)));
    }

    [[nodiscard]] fs::path bundleFile() const {
        const QByteArray modem = hio::firmware::test::zipBytes(
            {{.name = "firmware.update.image.segments.0.hex", .data = ":00000001FF\n"}});
        return writeFile("bundle.zip", hio::firmware::test::zipBytes(
                                           {{.name = "modem.zip", .data = modem.toStdString()},
                                            {.name = "application.hex",
                                             .data = hio::firmware::test::toHex(image())}}));
    }

    [[nodiscard]] fs::path modemFile() const {
        return writeFile(
            "mfw_nrf91x1_2.0.2.zip",
            hio::firmware::test::zipBytes(
                {{.name = "firmware.update.image.segments.0.hex", .data = ":00000001FF\n"}}));
    }

    bool load(const fs::path& path) {
        source().loadFile(QUrl::fromLocalFile(QString::fromStdString(path.string())));
        return waitFor(
            [this] { return source().sourceState() != FirmwareSourceViewModel::Loading; });
    }

    bool finished() {
        return waitFor(
            [this] {
                return flash().opState() != FlashViewModel::Running &&
                       flash().opState() != FlashViewModel::Cancelling;
            },
            std::chrono::seconds(60));
    }

    [[nodiscard]] QStringList phaseTitles() const {
        QStringList titles;
        const auto* page = qobject_cast<FlashViewModel*>(
            m_app->currentTabViewModel()->pageViewModel(FlashViewModel::pageId));
        for (const QVariant& phase : page->phases()) {
            titles.append(phase.toMap().value(QStringLiteral("title")).toString());
        }
        return titles;
    }

private:
    std::unique_ptr<AppViewModel> m_app;
    std::string m_scenario;
};

TEST_F(FlashTest, KindDefaultsFromTheTabTarget) {
    start(probeScenario(), QStringLiteral("lte"));
    EXPECT_EQ(flash().kind(), Kind::LteFirmware);
    EXPECT_EQ(flash().targetId(), QStringLiteral("lte"));
    EXPECT_EQ(source().kind(), Kind::LteFirmware);
    flash().setKind(Kind::AppFirmware);
    EXPECT_EQ(flash().targetId(), QStringLiteral("app"));
    // Re-choosing the selected kind changes nothing.
    flash().setKind(Kind::AppFirmware);
    EXPECT_EQ(flash().kind(), Kind::AppFirmware);
    ASSERT_EQ(flash().kinds().size(), 3);
    EXPECT_TRUE(flash()
                    .kinds()
                    .at(0)
                    .toMap()
                    .value(QStringLiteral("mcu"))
                    .toString()
                    .contains(QStringLiteral("nRF52840")));
}

TEST_F(FlashTest, TheLastKindIsRememberedPerTabInMemoryOnly) {
    start(probeScenario(), QStringLiteral("lte"));
    flash().setKind(Kind::ModemFirmware);
    // Nothing is persisted (review F4, user decision 2026-10-16: no flash/lastKind key).
    EXPECT_FALSE(app().settingsStore()->value(QStringLiteral("flash/lastKind")).isValid());
    // The tab keeps its choice when its target changes (no session).
    tab().setConsoleTarget(QStringLiteral("app"));
    EXPECT_EQ(flash().kind(), Kind::ModemFirmware);

    // Another tab starts from its own target, not from the first tab's choice.
    DeviceTabViewModel* first = &tab();
    app().newTab();
    ASSERT_NE(&tab(), first);
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001,
                                QStringLiteral("Test Probe")));
    EXPECT_EQ(flash().kind(), Kind::AppFirmware);
    EXPECT_EQ(flash().targetId(), QStringLiteral("app"));
}

TEST_F(FlashTest, LoadedHexIsReadyWithSummaryAndRecent) {
    start(probeScenario());
    EXPECT_EQ(flash().cannotFlashReason(),
              QStringLiteral("Choose a firmware file or enter a catalog ID"));
    ASSERT_TRUE(load(hexFile()));
    ASSERT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready);
    EXPECT_TRUE(source().sourceSummary().startsWith(QStringLiteral("app.hex · Intel HEX · ")));
    EXPECT_TRUE(source().sourceSummary().contains(QStringLiteral("0x00001000–0x00002000")));
    EXPECT_FALSE(source().hasUicr());
    EXPECT_TRUE(flash().canFlash());
    EXPECT_EQ(flash().flashSummary(), QStringLiteral("Application firmware · app.hex"));
    ASSERT_EQ(source().recent()->count(), 1);
    EXPECT_EQ(source().recent()->sourceAt(0),
              QString::fromStdString((root() / "app.hex").string()));
}

TEST_F(FlashTest, AKindChangeDoesNotAddToRecent) {
    start(probeScenario());
    ASSERT_TRUE(load(hexFile()));
    ASSERT_EQ(source().recent()->count(), 1);
    // The HEX fits LTE firmware too, but only an explicit load adds it to the LTE list (F7).
    flash().setKind(Kind::LteFirmware);
    ASSERT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready);
    EXPECT_EQ(source().recent()->count(), 0);
    EXPECT_EQ(source().recent()->entries().size(), 1);
    flash().setKind(Kind::AppFirmware);
    EXPECT_EQ(source().recent()->count(), 1);
}

TEST_F(FlashTest, AFileOfAnotherKindShowsAHintAndNeverSwitchesSilently) {
    start(probeScenario());
    ASSERT_TRUE(load(modemFile()));
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Invalid);
    EXPECT_EQ(source().suggestedKind(), Kind::ModemFirmware);
    EXPECT_EQ(source().kindHint(),
              QStringLiteral("This is LTE modem firmware — switch to LTE modem firmware?"));
    EXPECT_EQ(flash().kind(), Kind::AppFirmware);
    EXPECT_FALSE(flash().canFlash());
    source().acceptSuggestedKind();
    EXPECT_EQ(flash().kind(), Kind::ModemFirmware);
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready);
    EXPECT_TRUE(source().modemNoteVisible());

    // An LTE bundle under application firmware suggests LTE firmware.
    flash().setKind(Kind::AppFirmware);
    ASSERT_TRUE(load(bundleFile()));
    EXPECT_EQ(source().suggestedKind(), Kind::LteFirmware);
    EXPECT_EQ(source().kindHint(),
              QStringLiteral("This is an LTE firmware bundle — switch to LTE firmware?"));

    // An Intel HEX under modem firmware suggests the tab's MCU (application here).
    flash().setKind(Kind::ModemFirmware);
    ASSERT_TRUE(load(hexFile()));
    EXPECT_EQ(source().suggestedKind(), Kind::AppFirmware);
    // A file that is not firmware at all is an error (no hint).
    ASSERT_TRUE(load(writeFile("notes.txt", "hello")));
    EXPECT_EQ(source().suggestedKind(), -1);
    EXPECT_TRUE(source().error().valid());
}

TEST_F(FlashTest, HexUnderModemFirmwareSuggestsLteOnAnLteTab) {
    start(probeScenario(), QStringLiteral("lte"));
    flash().setKind(Kind::ModemFirmware);
    ASSERT_TRUE(load(hexFile()));
    EXPECT_EQ(source().suggestedKind(), Kind::LteFirmware);
    EXPECT_EQ(
        source().kindHint(),
        QStringLiteral("This is an Intel HEX file, not modem firmware — switch to LTE firmware?"));
}

TEST_F(FlashTest, CatalogIdValidation) {
    start(probeScenario());
    EXPECT_TRUE(source().catalogAvailable());
    EXPECT_FALSE(source().checkCatalogId(QStringLiteral("3f2a")));
    EXPECT_EQ(source().catalogIdError(), QStringLiteral("The ID has 32 characters 0–9 a–f."));
    EXPECT_TRUE(source().checkCatalogId(QString()));
    EXPECT_TRUE(source().catalogIdError().isEmpty());
    source().loadCatalogId(QStringLiteral("zz000000000000000000000000000000"));
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::None);
    EXPECT_FALSE(source().catalogIdError().isEmpty());
    flash().setKind(Kind::ModemFirmware);
    EXPECT_FALSE(source().catalogAvailable());
}

TEST_F(FlashTest, CatalogDownloadAndErrors) {
    hio::firmware::test::HttpFixture http;
    ASSERT_TRUE(http.listening());
    qputenv("HIO_CATALOG_URL", http.baseUrl().toUtf8());
    const QString id = randomId();
    http.set(QStringLiteral("/chester/%1/hex").arg(id),
             hio::firmware::test::HttpFixture::Response::ok(
                 QByteArray::fromStdString(hio::firmware::test::toHex(image()))));
    start(probeScenario());

    source().loadCatalogId(id.toUpper());
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Downloading);
    ASSERT_TRUE(
        waitFor([this] { return source().sourceState() != FirmwareSourceViewModel::Downloading; }));
    ASSERT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready)
        << source().error().code().toStdString();
    EXPECT_EQ(source().sourceName(), QStringLiteral("Catalog ID %1").arg(id));
    EXPECT_EQ(source().recent()->sourceAt(0), RecentSourcesModel::catalogPrefix + id);

    // Not in the catalog (404).
    source().loadCatalogId(randomId());
    ASSERT_TRUE(
        waitFor([this] { return source().sourceState() != FirmwareSourceViewModel::Downloading; }));
    EXPECT_EQ(source().sourceState(), FirmwareSourceViewModel::Invalid);
    EXPECT_EQ(source().error().code(), QStringLiteral("firmware.not_found_in_catalog"));
    EXPECT_EQ(source().error().primaryAction(), hio::gui::error_action::ChooseFile);
}

TEST_F(FlashTest, RecentListIsBoundedPerKindAndRemovable) {
    start(probeScenario());
    RecentSourcesModel* recent = source().recent();
    for (int i = 0; i < 12; ++i) {
        recent->add(QStringLiteral("app"), QStringLiteral("/fw/app-%1.hex").arg(i));
    }
    recent->add(QStringLiteral("modem"), QStringLiteral("/fw/mfw.zip"));
    EXPECT_EQ(recent->entries().size(), RecentSourcesModel::maxEntries);
    EXPECT_EQ(recent->count(), 9); // the modem entry is the 10th
    EXPECT_EQ(recent->sourceAt(0), QStringLiteral("/fw/app-11.hex"));
    // Re-adding moves to the front instead of duplicating.
    recent->add(QStringLiteral("app"), QStringLiteral("/fw/app-5.hex"));
    EXPECT_EQ(recent->sourceAt(0), QStringLiteral("/fw/app-5.hex"));
    EXPECT_EQ(recent->entries().size(), RecentSourcesModel::maxEntries);
    source().removeRecent(0);
    EXPECT_NE(recent->sourceAt(0), QStringLiteral("/fw/app-5.hex"));
    flash().setKind(Kind::ModemFirmware);
    EXPECT_EQ(recent->count(), 1);
    EXPECT_EQ(recent->data(recent->index(0), RecentSourcesModel::TypeRole).toString(),
              QStringLiteral("Zip archive"));
}

TEST_F(FlashTest, FlashFromADisconnectedTab) {
    start(probeScenario());
    ASSERT_EQ(tab().session(), nullptr);
    ASSERT_TRUE(load(hexFile()));
    flash().flash();
    // ensureSession() opened the console target; the page is busy while the operation runs.
    ASSERT_NE(tab().session(), nullptr);
    EXPECT_EQ(flash().opState(), FlashViewModel::Running);
    EXPECT_EQ(tab().busyPage(), FlashViewModel::pageId);
    EXPECT_EQ(tab().busyText(), QStringLiteral("Flashing…"));
    EXPECT_EQ(phaseTitles(),
              (QStringList{QStringLiteral("Connecting"), QStringLiteral("Erasing"),
                           QStringLiteral("Programming"), QStringLiteral("Verifying"),
                           QStringLiteral("Resetting")}));
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded)
        << flash().error().detail().toStdString();
    EXPECT_TRUE(
        flash().resultText().startsWith(QStringLiteral("Firmware flashed and verified in ")));
    EXPECT_EQ(flash().chipText(), QStringLiteral("Chip: nRF52840"));
    EXPECT_TRUE(tab().busyPage().isEmpty());
    EXPECT_TRUE(flash().flashOperation());
    flash().dismissResult();
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);
    EXPECT_TRUE(flash().canFlash()); // the source stays loaded
}

TEST_F(FlashTest, CancelDuringVerify) {
    start(probeScenario(R"({"readDelayMs": 200})"));
    ASSERT_TRUE(load(hexFile("big.hex", image(0x1000, std::size_t{64} * 1024))));
    flash().flash();
    ASSERT_EQ(flash().opState(), FlashViewModel::Running);
    ASSERT_TRUE(waitFor([this] { return flash().progressText().contains(QStringLiteral("%")); },
                        std::chrono::seconds(30)));
    flash().cancel();
    EXPECT_EQ(flash().opState(), FlashViewModel::Cancelling);
    EXPECT_FALSE(flash().cancelAvailable());
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Cancelled);
    EXPECT_EQ(flash().resultText(),
              QStringLiteral(
                  "Flashing was cancelled. The device may not start until you flash it again."));
    EXPECT_TRUE(tab().busyPage().isEmpty());
}

TEST_F(FlashTest, CrossTargetPhasesAndConnectorWarning) {
    start(probeScenario());
    flash().setKind(Kind::LteFirmware);
    // ADR-0057 Phase 0: said before starting.
    EXPECT_TRUE(flash().connectorWarning().contains(QStringLiteral("LTE connector")));
    ASSERT_TRUE(load(hexFile()));
    flash().flash();
    ASSERT_EQ(flash().opState(), FlashViewModel::Running);
    const QStringList phases = phaseTitles();
    ASSERT_FALSE(phases.isEmpty());
    EXPECT_EQ(phases.front(), QStringLiteral("Switching MCU"));
    EXPECT_EQ(phases.back(), QStringLiteral("Restoring console"));
    ASSERT_TRUE(finished());
    // The probe is on the application MCU: the LTE operation fails early with the connector hint.
    EXPECT_EQ(flash().opState(), FlashViewModel::Failed);
    EXPECT_EQ(flash().error().code(), QStringLiteral("target.wrong_family"));
    EXPECT_EQ(flash().errorAction(), hio::gui::error_action::Retry);
    flash().setKind(Kind::AppFirmware); // ignored only while running
    EXPECT_TRUE(flash().connectorWarning().isEmpty());
}

TEST_F(FlashTest, LteBundleRunsInTwoStepsWithNrfUtil) {
    qputenv("HIO_NRFUTIL", QByteArray(HIO_FAKE_NRFUTIL));
    start(probeScenario(R"({"family": "nrf91"})"), QStringLiteral("lte"));
    ASSERT_TRUE(load(bundleFile()));
    ASSERT_EQ(source().sourceState(), FirmwareSourceViewModel::Ready);
    EXPECT_TRUE(source().sourceSummary().contains(QStringLiteral("LTE bundle")));
    EXPECT_TRUE(source().modemNoteVisible());
    ASSERT_TRUE(waitFor([this] { return flash().canFlash(); })); // nRF Util detected
    flash().flash();
    ASSERT_EQ(flash().opState(), FlashViewModel::Running);
    EXPECT_EQ(flash().steps(), 2);
    EXPECT_EQ(flash().stepText(), QStringLiteral("Step 1 of 2"));
    bool sawStepTwo = false;
    ASSERT_TRUE(waitFor(
        [&] {
            sawStepTwo = sawStepTwo || flash().step() == 2;
            return flash().opState() != FlashViewModel::Running;
        },
        std::chrono::seconds(60)));
    EXPECT_TRUE(sawStepTwo);
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded)
        << flash().error().detail().toStdString();
    EXPECT_TRUE(
        flash().resultText().startsWith(QStringLiteral("LTE modem firmware and LTE firmware")));
}

TEST_F(FlashTest, ModemFailureIsReported) {
    qputenv("HIO_NRFUTIL", QByteArray(HIO_FAKE_NRFUTIL));
    qputenv("HIO_FAKE_NRFUTIL_MODE", "fail");
    start(probeScenario(R"({"family": "nrf91"})"), QStringLiteral("lte"));
    flash().setKind(Kind::ModemFirmware);
    ASSERT_TRUE(load(modemFile()));
    ASSERT_TRUE(waitFor([this] { return flash().canFlash(); })); // nRF Util detected
    EXPECT_FALSE(flash().haltAvailable());
    flash().flash();
    EXPECT_EQ(tab().busyText(), QStringLiteral("Updating the modem…"));
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Failed);
    EXPECT_EQ(flash().error().code(), QStringLiteral("firmware.modem_update_failed"));
}

// Review F5 / ADR-0114: the nRF Util path is read from the settings when the modem update is
// requested, not when the session was opened.
TEST_F(FlashTest, AChangedNrfUtilPathReachesAnOpenSession) {
    qunsetenv("HIO_NRFUTIL"); // the setting decides (the environment would win over it)
    start(probeScenario(R"({"family": "nrf91"})"), QStringLiteral("lte"));
    hio::gui::SettingsStore& settings = *app().settingsStore();
    ASSERT_TRUE(
        settings.setValue(QStringLiteral("nrfutil/path"),
                          QString::fromStdString((root() / "no-nrfutil" / "nrfutil").string())));
    tab().connectDevice();
    const QPointer<hio::device::DeviceSession> opened(tab().session());
    ASSERT_FALSE(opened.isNull());
    flash().setKind(Kind::ModemFirmware);
    ASSERT_TRUE(load(modemFile()));
    ASSERT_TRUE(waitFor([this] { return !flash().prerequisiteText().isEmpty(); }));
    EXPECT_FALSE(flash().canFlash());

    // A new path in Settings while the session stays open.
    ASSERT_TRUE(
        settings.setValue(QStringLiteral("nrfutil/path"), QString::fromUtf8(HIO_FAKE_NRFUTIL)));
    ASSERT_TRUE(waitFor([this] { return flash().canFlash(); }));
    flash().flash();
    ASSERT_EQ(flash().opState(), FlashViewModel::Running);
    EXPECT_EQ(tab().session(), opened.data()); // no new session
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded)
        << flash().error().code().toStdString() << " " << flash().error().detail().toStdString();
}

TEST_F(FlashTest, MissingNrfUtilIsAPrerequisite) {
    start(probeScenario(), QStringLiteral("lte"));
    flash().setKind(Kind::ModemFirmware);
    ASSERT_TRUE(waitFor([this] { return !flash().prerequisiteText().isEmpty(); }));
    EXPECT_EQ(flash().prerequisiteText(),
              QStringLiteral("nRF Util was not found. It is needed only for LTE modem firmware."));
    ASSERT_TRUE(load(modemFile()));
    EXPECT_FALSE(flash().canFlash());
    EXPECT_EQ(flash().cannotFlashReason(), QStringLiteral("nRF Util was not found"));
    // Application firmware does not need it.
    flash().setKind(Kind::AppFirmware);
    EXPECT_TRUE(flash().prerequisiteText().isEmpty());
}

TEST_F(FlashTest, AnotherBusyPageDisablesFlash) {
    start(probeScenario());
    ASSERT_TRUE(load(hexFile()));
    ASSERT_TRUE(flash().canFlash());
    auto* testPage = qobject_cast<hio::gui::test::TestPageViewModel*>(
        tab().pageViewModel(hio::gui::test::TestPageViewModel::pageId));
    ASSERT_NE(testPage, nullptr);
    testPage->startWork();
    EXPECT_FALSE(flash().canFlash());
    EXPECT_EQ(flash().cannotFlashReason(), QStringLiteral("Another operation is running"));
    EXPECT_FALSE(flash().canRunMaintenance());
    flash().flash(); // ignored
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);
    testPage->finishWork();
    EXPECT_TRUE(flash().canFlash());
}

TEST_F(FlashTest, UicrRecordsNeedTheOption) {
    start(probeScenario());
    std::vector<Segment> segments;
    segments.push_back(
        Segment{.address = 0x1000, .data = std::vector<std::byte>(16, std::byte{1})});
    segments.push_back(
        Segment{.address = 0x10001200, .data = std::vector<std::byte>(4, std::byte{2})});
    ASSERT_TRUE(load(hexFile("uicr.hex", *FirmwareImage::fromSegments(std::move(segments)))));
    EXPECT_TRUE(source().hasUicr());
    EXPECT_FALSE(flash().canFlash());
    EXPECT_EQ(flash().cannotFlashReason(),
              QStringLiteral("The firmware writes the UICR: allow it in Options"));
    source().setAllowUicr(true);
    EXPECT_TRUE(flash().canFlash());
    // Never carried over to another image (ADR-0036).
    ASSERT_TRUE(load(hexFile()));
    EXPECT_FALSE(source().allowUicr());
}

TEST_F(FlashTest, ConfirmationTexts) {
    start(probeScenario());
    QVariantMap app = flash().describeEraseAll();
    EXPECT_EQ(app.value(QStringLiteral("title")).toString(),
              QStringLiteral("Erase everything on the application MCU?"));
    EXPECT_EQ(app.value(QStringLiteral("acceptText")).toString(),
              QStringLiteral("Erase everything"));
    EXPECT_EQ(app.value(QStringLiteral("consequence")).toString(),
              QStringLiteral("This cannot be undone."));
    QStringList items = app.value(QStringLiteral("items")).toStringList();
    ASSERT_EQ(items.size(), 3);
    EXPECT_TRUE(items.at(2).startsWith(QStringLiteral("Product Information Block")));
    const QVariantMap recoverApp = flash().describeRecover();
    EXPECT_EQ(recoverApp.value(QStringLiteral("title")).toString(),
              QStringLiteral("Recover the application MCU?"));

    EXPECT_TRUE(recoverApp.value(QStringLiteral("items"))
                    .toStringList()
                    .contains(QStringLiteral(
                        "Product Information Block (serial number, hardware version, …)")));

    flash().setKind(Kind::LteFirmware);
    EXPECT_EQ(flash().maintenanceTitle(), QStringLiteral("Maintenance — LTE MCU"));
    const QVariantMap lte = flash().describeEraseAll();
    EXPECT_EQ(lte.value(QStringLiteral("title")).toString(),
              QStringLiteral("Erase everything on the LTE MCU?"));
    // ERASEALL and recover erase the PIB on the nRF91 too (review F1, ADR-0038).
    const QStringList lteItems = lte.value(QStringLiteral("items")).toStringList();
    ASSERT_EQ(lteItems.size(), 3);
    EXPECT_TRUE(lteItems.at(2).startsWith(QStringLiteral("Product Information Block")));
    const QVariantMap recoverLte = flash().describeRecover();
    EXPECT_TRUE(recoverLte.value(QStringLiteral("body"))
                    .toString()
                    .contains(QStringLiteral("small image")));
    const QStringList recoverLteItems = recoverLte.value(QStringLiteral("items")).toStringList();
    ASSERT_EQ(recoverLteItems.size(), 3);
    EXPECT_TRUE(recoverLteItems.at(2).startsWith(QStringLiteral("Product Information Block")));
    // Modem firmware acts on the LTE MCU as well.
    flash().setKind(Kind::ModemFirmware);
    EXPECT_EQ(flash().describeEraseAll().value(QStringLiteral("items")).toStringList().size(), 3);
}

TEST_F(FlashTest, TheLteMcuHasNoCodeOnlyErase) {
    start(probeScenario());
    EXPECT_TRUE(flash().eraseCodeAvailable()); // application MCU: defaultErase Code
    flash().setKind(Kind::LteFirmware);
    EXPECT_FALSE(flash().eraseCodeAvailable()); // LTE MCU: defaultErase All (review F2)
    flash().eraseCode();                        // refused
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);
    EXPECT_TRUE(tab().busyPage().isEmpty());
    flash().setKind(Kind::ModemFirmware);
    EXPECT_FALSE(flash().eraseCodeAvailable());
}

TEST_F(FlashTest, AConfirmationIsRefusedWhenTheMcuChanged) {
    start(probeScenario());
    flash().requestConfirmation(false);
    EXPECT_EQ(flash().confirmation().value(QStringLiteral("title")).toString(),
              QStringLiteral("Erase everything on the application MCU?"));
    // The dialog named the application MCU; the kind (and its MCU) changed meanwhile (F11).
    flash().setKind(Kind::LteFirmware);
    flash().confirm();
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);
    EXPECT_TRUE(tab().busyPage().isEmpty());
    EXPECT_EQ(tab().session(), nullptr);
    // Not kept for later either.
    flash().setKind(Kind::AppFirmware);
    flash().confirm();
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);
}

TEST_F(FlashTest, MaintenanceActionsRunAsOperations) {
    start(probeScenario());
    flash().reset(true);
    EXPECT_EQ(tab().busyText(), QStringLiteral("Resetting…"));
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded)
        << flash().error().detail().toStdString();
    EXPECT_EQ(flash().resultText(), QStringLiteral("The application MCU was reset and halted."));
    EXPECT_FALSE(flash().flashOperation());
    flash().dismissResult();

    flash().requestConfirmation(false);
    EXPECT_EQ(flash().confirmation().value(QStringLiteral("acceptText")).toString(),
              QStringLiteral("Erase everything"));
    flash().confirm();
    EXPECT_EQ(tab().busyText(), QStringLiteral("Erasing…"));
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded);
    EXPECT_EQ(flash().resultText(),
              QStringLiteral("Everything on the application MCU was erased."));
    flash().confirm(); // nothing pending
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded);
    flash().dismissResult();

    flash().eraseCode();
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded);
    flash().dismissResult();

    flash().requestConfirmation(true);
    flash().confirm();
    ASSERT_TRUE(finished());
    EXPECT_EQ(flash().opState(), FlashViewModel::Succeeded)
        << flash().error().detail().toStdString();
    EXPECT_EQ(flash().resultText(), QStringLiteral("The application MCU was recovered."));
}

TEST_F(FlashTest, AFailedDownloadIsAnInterruptedFlash) {
    start(probeScenario(R"({"writeFault": true})"));
    ASSERT_TRUE(load(hexFile()));
    flash().flash();
    ASSERT_TRUE(finished());
    ASSERT_EQ(flash().opState(), FlashViewModel::Failed);
    EXPECT_EQ(flash().error().code(), QStringLiteral("target.program_failed"));
    EXPECT_EQ(flash().resultText(),
              QStringLiteral(
                  "Flashing was interrupted. The firmware may be partly written: flash it again."));
    // [Try again] runs the same flash again.
    flash().triggerErrorAction(hio::gui::error_action::Retry);
    EXPECT_EQ(flash().opState(), FlashViewModel::Running);
    ASSERT_TRUE(finished());
}

TEST_F(FlashTest, RetryFlashesTheStoredRequestAfterASourceChange) {
    start(probeScenario(R"({"writeFault": true})"));
    ASSERT_TRUE(load(hexFile("first.hex")));
    flash().flash();
    ASSERT_TRUE(finished());
    ASSERT_EQ(flash().opState(), FlashViewModel::Failed);
    // Another file arrives while the result is shown (F6: the page no longer accepts drops there;
    // the view model must not depend on it).
    ASSERT_TRUE(load(hexFile("second.hex", image(0x2000))));
    ASSERT_EQ(source().sourceName(), QStringLiteral("second.hex"));
    flash().triggerErrorAction(hio::gui::error_action::Retry);
    ASSERT_EQ(flash().opState(), FlashViewModel::Running);
    EXPECT_EQ(flash().operationTitle(),
              QStringLiteral("Flashing first.hex to the application MCU"));
    ASSERT_TRUE(finished());
}

TEST_F(FlashTest, ADependencyErrorOffersTheSettings) {
    qputenv("HIO_NRFUTIL", QByteArray(HIO_FAKE_NRFUTIL));
    start(probeScenario(R"({"family": "nrf91"})"), QStringLiteral("lte"));
    flash().setKind(Kind::ModemFirmware);
    ASSERT_TRUE(load(modemFile()));
    ASSERT_TRUE(waitFor([this] { return flash().canFlash(); })); // nRF Util detected
    // nRF Util disappears before the session opens (it is located when the session opens).
    qputenv("HIO_NRFUTIL", (root() / "no-nrfutil" / "nrfutil").string().c_str());
    flash().flash();
    ASSERT_TRUE(finished());
    ASSERT_EQ(flash().opState(), FlashViewModel::Failed);
    EXPECT_EQ(flash().error().code(), QStringLiteral("dependency.nrfutil_not_found"));
    EXPECT_EQ(flash().errorAction(), hio::gui::error_action::OpenSettings); // F10
    int settingsRequests = 0;
    QObject::connect(&flash(), &FlashViewModel::settingsRequested, &flash(),
                     [&settingsRequests] { ++settingsRequests; });
    flash().triggerErrorAction(hio::gui::error_action::OpenSettings);
    EXPECT_EQ(settingsRequests, 1);
    EXPECT_EQ(flash().opState(), FlashViewModel::Idle);
}

TEST_F(FlashTest, ALostSessionEndsTheOperationAsFailed) {
    start(probeScenario(R"({"readDelayMs": 200})"));
    ASSERT_TRUE(load(hexFile("big.hex", image(0x1000, std::size_t{64} * 1024))));
    flash().flash();
    ASSERT_EQ(flash().opState(), FlashViewModel::Running);
    ASSERT_TRUE(waitFor([this] { return flash().progressText().contains(QStringLiteral("%")); },
                        std::chrono::seconds(30)));
    // The tab's session goes away under the running operation (shutdown).
    tab().shutdown();
    EXPECT_EQ(tab().session(), nullptr);
    EXPECT_EQ(flash().opState(), FlashViewModel::Failed);
    EXPECT_EQ(flash().error().code(), QStringLiteral("target.not_connected"));
    EXPECT_TRUE(tab().busyPage().isEmpty());
    EXPECT_FALSE(flash().cancelAvailable());
}

TEST_F(FlashTest, ClosingTheTabMidOperationCancelsAndCloses) {
    start(probeScenario(R"({"readDelayMs": 200})"));
    ASSERT_TRUE(load(hexFile("big.hex", image(0x1000, std::size_t{64} * 1024))));
    flash().flash();
    ASSERT_EQ(flash().opState(), FlashViewModel::Running);
    ASSERT_TRUE(waitFor([this] { return flash().progressText().contains(QStringLiteral("%")); },
                        std::chrono::seconds(30)));
    const QPointer<DeviceTabViewModel> closing(&tab());
    const QPointer<FlashViewModel> page(&flash());
    // [Cancel and close] of the busy confirmation: cancels, then closes on closeReady.
    app().closeTabObject(closing.data(), true);
    EXPECT_EQ(page->opState(), FlashViewModel::Cancelling);
    ASSERT_TRUE(waitFor([&closing] { return closing.isNull(); }, std::chrono::seconds(30)));
    EXPECT_TRUE(page.isNull()); // deleted with its tab
}

TEST_F(FlashTest, PhaseTitlesAreTranslatedTexts) {
    EXPECT_EQ(FlashViewModel::phaseTitle(hio::firmware::ProgramPhase::Modem),
              QStringLiteral("Updating the modem"));
    EXPECT_EQ(FlashViewModel::phaseTitle(hio::firmware::ProgramPhase::SwitchingTarget),
              QStringLiteral("Switching MCU"));
}

// ---- FirmwareLoader ---------------------------------------------------------------------------

class LoaderTest : public hio::gui::test::GuiTest {};

TEST_F(LoaderTest, NewestRequestOfARequesterWins) {
    hio::gui::FirmwareLoader loader(root() / "instances");
    const fs::path missing = root() / "missing.hex";
    std::vector<std::uint64_t> delivered;
    QObject::connect(&loader, &hio::gui::FirmwareLoader::fileLoaded, &loader,
                     [&](quint64 generation, const hio::Result<hio::gui::LoadedSource>&) {
                         delivered.push_back(generation);
                     });
    const int requester = 0;
    std::uint64_t last = 0;
    for (int i = 0; i < 8; ++i) {
        last = loader.loadFile(&requester, missing);
    }
    ASSERT_TRUE(waitFor([&] { return !delivered.empty() && delivered.back() == last; }));
    // At most the running request and the newest pending one deliver.
    EXPECT_LE(delivered.size(), 2U);
}

TEST_F(LoaderTest, ARequestDroppedFromTheFullQueueIsAnswered) {
    // The worker is kept busy by an nRF Util version probe that answers after 30 s.
    const fs::path log = root() / "nrfutil.log";
    qputenv("HIO_FAKE_NRFUTIL_MODE", "slowversion");
    qputenv("HIO_FAKE_NRFUTIL_LOG", QByteArray::fromStdString(log.string()));
    auto loader = std::make_unique<hio::gui::FirmwareLoader>(root() / "instances");
    std::vector<std::pair<std::uint64_t, ErrorCode>> answers;
    QObject::connect(loader.get(), &hio::gui::FirmwareLoader::fileLoaded, loader.get(),
                     [&](quint64 generation, const hio::Result<hio::gui::LoadedSource>& result) {
                         answers.emplace_back(generation, result ? ErrorCode::InternalUnexpected
                                                                 : result.error().code);
                     });
    hio::nrfutil::LocatorOptions options;
    options.option = fs::path(HIO_FAKE_NRFUTIL);
    const int prober = 0;
    (void)loader->checkNrfUtil(&prober, options);
    ASSERT_TRUE(waitFor([&log] { return fs::exists(log); })); // the probe runs

    // Five requesters: the queue holds four, the oldest is dropped and answered (F8).
    const std::vector<int> requesters(5, 0);
    std::vector<std::uint64_t> generations;
    generations.reserve(requesters.size());
    for (const int& requester : requesters) {
        generations.push_back(loader->loadFile(&requester, root() / "missing.hex"));
    }
    ASSERT_TRUE(waitFor([&answers] { return !answers.empty(); }));
    ASSERT_EQ(answers.size(), 1U);
    EXPECT_EQ(answers.front().first, generations.front());
    EXPECT_EQ(answers.front().second, ErrorCode::InternalQueueFull);

    // Quitting does not wait for the probe: it is killed (F9).
    const auto before = std::chrono::steady_clock::now();
    loader.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::seconds(5));
    qunsetenv("HIO_FAKE_NRFUTIL_MODE");
    qunsetenv("HIO_FAKE_NRFUTIL_LOG");
}

TEST_F(LoaderTest, LoadErrorsAreHioErrors) {
    const auto result = hio::gui::loadFirmwareFile(root() / "missing.hex");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::IoFileNotFound);
}

} // namespace
