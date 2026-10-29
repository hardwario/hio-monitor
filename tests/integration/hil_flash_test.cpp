// Hardware-in-the-loop tests of M2 flashing (plan/features/0003, phase G1). Skipped unless
// HWIO_HIL=1 (see CMakeLists.txt). Environment:
//   HWIO_HIL_SN=SERIAL_NUMBER        the probe (default: the first free one)
//   HWIO_HIL_APP_HEX=PATH            a CHESTER application HEX file (flash tests)
//   HWIO_HIL_FIRMWARE_ID=ID          a catalog firmware ID (download test; needs the internet)
//   HWIO_HIL_LTE_SN=SERIAL_NUMBER    a probe on the LTE connector (LTE tests)
//   HWIO_HIL_LTE_BUNDLE=PATH         a CHESTER LTE bundle (.zip; needs nRF Util)
//   HWIO_HIL_DESTRUCTIVE=1           also run erase --all and recover, which ERASE THE PIB (use
//                                   a board whose PIB can be restored)

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/device/pib.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_registry.h"
#include "hwio/firmware/firmware_cache.h"
#include "hwio/firmware/firmware_catalog.h"
#include "hwio/firmware/firmware_image.h"
#include "hwio/firmware/intel_hex.h"
#include "hwio/firmware/lte_input.h"
#include "hwio/hw/jlink/library_locator.h"
#include "hwio/hw/jlink/programmer.h"

#include <QString>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using hwio::device::DeviceSession;
using hwio::device::OperationResult;

bool enabled(const char* variable) { return qEnvironmentVariable(variable) == QStringLiteral("1"); }

std::optional<std::uint32_t> serialFrom(const char* variable) {
    bool ok = false;
    const std::uint32_t value = qEnvironmentVariable(variable).toUInt(&ok);
    return ok ? std::optional(value) : std::nullopt;
}

std::filesystem::path pathFrom(const char* variable) {
    return {qEnvironmentVariable(variable).toStdU16String()};
}

class HilFlash : public ::testing::Test {
protected:
    void SetUp() override {
        if (!enabled("HWIO_HIL")) {
            GTEST_SKIP() << "hardware-in-the-loop test; set HWIO_HIL=1 to run it";
        }
        auto location = hwio::jlink::locateLibrary(hwio::jlink::defaultLocatorOptions({}, nullptr));
        ASSERT_TRUE(location) << location.error().detail;
        m_location = *location;
    }

    std::unique_ptr<DeviceSession> open(std::string_view target, std::optional<std::uint32_t> sn) {
        hwio::device::SessionOptions options;
        options.profile = hwio::device::ProfileRegistry::find("chester");
        options.targetId = target;
        options.probeSn = sn;
        options.speedKhz = 2000;
        options.latencyMs = 50;
        options.attachRtt = false;
        options.consoleFile = std::filesystem::path();
        auto session = DeviceSession::open(m_registry, m_location, std::move(options));
        EXPECT_TRUE(session) << session.error().message;
        return session ? std::move(*session) : nullptr;
    }

    static OperationResult run(DeviceSession& session,
                               const hwio::Result<hwio::device::OperationId>& id) {
        EXPECT_TRUE(id) << (id ? std::string() : id.error().message);
        if (!id) {
            return {};
        }
        OperationResult result = hwio::device::waitForOperation(session, *id);
        EXPECT_TRUE(result.status)
            << result.status.error().message << " / " << result.status.error().detail;
        return result;
    }

    static std::vector<std::byte> readPib(DeviceSession& session, std::string_view target) {
        const auto* profile = hwio::device::ProfileRegistry::find("chester");
        const auto range = hwio::device::pibRange(*hwio::device::findTarget(*profile, target));
        return run(session, session.readMemory(target, static_cast<std::uint32_t>(range.start),
                                               hwio::device::pibSize))
            .data;
    }

    static std::shared_ptr<const hwio::firmware::FirmwareImage> appImage() {
        const auto path = pathFrom("HWIO_HIL_APP_HEX");
        if (path.empty()) {
            return nullptr;
        }
        auto image = hwio::firmware::readIntelHexFile(path);
        EXPECT_TRUE(image) << image.error().detail;
        return image ? std::make_shared<const hwio::firmware::FirmwareImage>(std::move(*image))
                     : nullptr;
    }

private:
    hwio::jlink::LibraryLocation m_location;
    hwio::device::SessionRegistry m_registry;
};

TEST_F(HilFlash, FlashByFileVerifiesAndKeepsThePib) {
    const auto image = appImage();
    if (!image) {
        GTEST_SKIP() << "set HWIO_HIL_APP_HEX";
    }
    auto session = open("app", serialFrom("HWIO_HIL_SN"));
    ASSERT_TRUE(session);
    const auto before = readPib(*session, "app");
    const auto result = run(*session, session->flash("app", image, {}));
    ASSERT_TRUE(result.chip.has_value());
    EXPECT_EQ(result.chip.value_or(hwio::jlink::ChipInfo{}).name, "nRF52840");
    EXPECT_EQ(readPib(*session, "app"), before);
    std::cout << "flash of " << image->totalBytes() << " bytes took " << result.duration.count()
              << " ms\n";
}

TEST_F(HilFlash, EraseCodeKeepsThePibAndPibDecodes) {
    auto session = open("app", serialFrom("HWIO_HIL_SN"));
    ASSERT_TRUE(session);
    const auto before = readPib(*session, "app");
    ASSERT_EQ(before.size(), 128U);
    EXPECT_TRUE(hwio::device::decodePib(std::span<const std::byte, 128>(before.data(), 128)));
    run(*session, session->erase("app", hwio::device::EraseScope::Code));
    EXPECT_EQ(readPib(*session, "app"), before);
    if (const auto image = appImage()) {
        run(*session, session->flash("app", image, {})); // leave the board usable
    }
}

TEST_F(HilFlash, FlashByCatalogId) {
    const QString id = qEnvironmentVariable("HWIO_HIL_FIRMWARE_ID");
    if (id.isEmpty()) {
        GTEST_SKIP() << "set HWIO_HIL_FIRMWARE_ID";
    }
    hwio::firmware::FirmwareCatalog catalog(
        hwio::firmware::CatalogOptions{
            .baseUrl = hwio::firmware::catalogBaseUrl("https://firmware.hardwario.com/chester"),
            .transferTimeout = std::chrono::seconds(60),
            .maxBodyBytes = 32ULL * 1024 * 1024},
        nullptr);
    auto operation = catalog.download(*hwio::firmware::FirmwareId::parse(id.toStdString()),
                                      hwio::firmware::DownloadOptions{.noCache = true});
    const auto firmware = hwio::firmware::waitForDownload(*operation);
    ASSERT_TRUE(firmware) << firmware.error().detail;
    auto session = open("app", serialFrom("HWIO_HIL_SN"));
    ASSERT_TRUE(session);
    run(*session, session->flash("app", firmware->image, {}));
}

TEST_F(HilFlash, EraseAllAndRecoverApp) {
    if (!enabled("HWIO_HIL_DESTRUCTIVE")) {
        GTEST_SKIP() << "erases the PIB; set HWIO_HIL_DESTRUCTIVE=1";
    }
    auto session = open("app", serialFrom("HWIO_HIL_SN"));
    ASSERT_TRUE(session);
    run(*session, session->erase("app", hwio::device::EraseScope::All));
    run(*session, session->recover("app"));
    if (const auto image = appImage()) {
        run(*session, session->flash("app", image, {}));
    }
}

TEST_F(HilFlash, LteResetEraseAndRecover) {
    const auto sn = serialFrom("HWIO_HIL_LTE_SN");
    if (!sn) {
        GTEST_SKIP() << "set HWIO_HIL_LTE_SN (a probe on the LTE connector)";
    }
    auto session = open("lte", sn);
    ASSERT_TRUE(session);
    const auto reset = run(*session, session->reset("lte", hwio::device::ResetMode::Go));
    (void)reset;
    if (!enabled("HWIO_HIL_DESTRUCTIVE")) {
        GTEST_SKIP() << "erase/recover of the LTE MCU need HWIO_HIL_DESTRUCTIVE=1";
    }
    run(*session, session->erase("lte", hwio::device::EraseScope::All));
    const auto recovered = run(*session, session->recover("lte"));
    ASSERT_TRUE(recovered.chip.has_value());
    // Still debuggable after the disable-AP-protect image and a reset (ADR-0039).
    run(*session, session->reset("lte", hwio::device::ResetMode::Go));
}

TEST_F(HilFlash, LteBundle) {
    const auto sn = serialFrom("HWIO_HIL_LTE_SN");
    const auto bundlePath = pathFrom("HWIO_HIL_LTE_BUNDLE");
    if (!sn || bundlePath.empty()) {
        GTEST_SKIP() << "set HWIO_HIL_LTE_SN and HWIO_HIL_LTE_BUNDLE";
    }
    auto input = hwio::firmware::classifyLteInput(bundlePath);
    ASSERT_TRUE(input) << input.error().detail;
    auto* bundle = std::get_if<hwio::firmware::LteBundle>(&*input);
    ASSERT_NE(bundle, nullptr);
    auto session = open("lte", sn);
    ASSERT_TRUE(session);
    const auto result =
        run(*session,
            session->flashLteBundle(
                "lte", std::make_shared<const hwio::firmware::LteBundle>(std::move(*bundle)), {}));
    std::cout << "LTE bundle took " << result.duration.count() << " ms\n";
}

} // namespace
