// Hardware-in-the-loop tests of M2 flashing (plan/features/0003, phase G1). Skipped unless
// HIO_HIL=1 (see CMakeLists.txt). Environment:
//   HIO_HIL_SN=SERIAL_NUMBER        the probe (default: the first free one)
//   HIO_HIL_APP_HEX=PATH            a CHESTER application HEX file (flash tests)
//   HIO_HIL_FIRMWARE_ID=ID          a catalog firmware ID (download test; needs the internet)
//   HIO_HIL_LTE_SN=SERIAL_NUMBER    a probe on the LTE connector (LTE tests)
//   HIO_HIL_LTE_BUNDLE=PATH         a CHESTER LTE bundle (.zip; needs nRF Util)
//   HIO_HIL_DESTRUCTIVE=1           also run erase --all and recover, which ERASE THE PIB (use
//                                   a board whose PIB can be restored)

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/pib.h"
#include "hio/device/profile.h"
#include "hio/device/session_registry.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_catalog.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/intel_hex.h"
#include "hio/firmware/lte_input.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/programmer.h"
#include "hio/hw/nrfutil/nrfutil.h"

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

using hio::device::DeviceSession;
using hio::device::OperationResult;

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
        if (!enabled("HIO_HIL")) {
            GTEST_SKIP() << "hardware-in-the-loop test; set HIO_HIL=1 to run it";
        }
        auto location = hio::jlink::locateLibrary(hio::jlink::defaultLocatorOptions({}, nullptr));
        ASSERT_TRUE(location) << location.error().detail;
        m_location = *location;
    }

    std::unique_ptr<DeviceSession> open(std::string_view target, std::optional<std::uint32_t> sn) {
        hio::device::SessionOptions options;
        options.profile = hio::device::ProfileRegistry::find("chester");
        options.targetId = target;
        options.probeSn = sn;
        options.speedKhz = 2000;
        options.latencyMs = 50;
        options.attachRtt = false;
        options.consoleFile = std::filesystem::path();
        options.nrfUtil = hio::nrfutil::defaultLocatorOptions(std::nullopt, nullptr);
        auto session = DeviceSession::open(m_registry, m_location, std::move(options));
        EXPECT_TRUE(session) << session.error().message;
        return session ? std::move(*session) : nullptr;
    }

    static OperationResult run(DeviceSession& session,
                               const hio::Result<hio::device::OperationId>& id) {
        EXPECT_TRUE(id) << (id ? std::string() : id.error().message);
        if (!id) {
            return {};
        }
        OperationResult result = hio::device::waitForOperation(session, *id);
        EXPECT_TRUE(result.status)
            << result.status.error().message << " / " << result.status.error().detail;
        return result;
    }

    static std::vector<std::byte> readPib(DeviceSession& session, std::string_view target) {
        const auto* profile = hio::device::ProfileRegistry::find("chester");
        const auto range = hio::device::pibRange(*hio::device::findTarget(*profile, target));
        return run(session, session.readMemory(target, static_cast<std::uint32_t>(range.start),
                                               hio::device::pibSize))
            .data;
    }

    static std::shared_ptr<const hio::firmware::FirmwareImage> appImage() {
        const auto path = pathFrom("HIO_HIL_APP_HEX");
        if (path.empty()) {
            return nullptr;
        }
        auto image = hio::firmware::readIntelHexFile(path);
        EXPECT_TRUE(image) << image.error().detail;
        return image ? std::make_shared<const hio::firmware::FirmwareImage>(std::move(*image))
                     : nullptr;
    }

private:
    hio::jlink::LibraryLocation m_location;
    hio::device::SessionRegistry m_registry;
};

TEST_F(HilFlash, FlashByFileVerifiesAndKeepsThePib) {
    const auto image = appImage();
    if (!image) {
        GTEST_SKIP() << "set HIO_HIL_APP_HEX";
    }
    auto session = open("app", serialFrom("HIO_HIL_SN"));
    ASSERT_TRUE(session);
    const auto before = readPib(*session, "app");
    const auto result = run(*session, session->flash("app", image, {}));
    ASSERT_TRUE(result.chip.has_value());
    EXPECT_EQ(result.chip.value_or(hio::jlink::ChipInfo{}).name, "nRF52840");
    EXPECT_EQ(readPib(*session, "app"), before);
    std::cout << "flash of " << image->totalBytes() << " bytes took " << result.duration.count()
              << " ms\n";
}

TEST_F(HilFlash, EraseCodeKeepsThePibAndPibDecodes) {
    auto session = open("app", serialFrom("HIO_HIL_SN"));
    ASSERT_TRUE(session);
    const auto before = readPib(*session, "app");
    ASSERT_EQ(before.size(), 128U);
    EXPECT_TRUE(hio::device::decodePib(std::span<const std::byte, 128>(before.data(), 128)));
    run(*session, session->erase("app", hio::device::EraseScope::Code));
    EXPECT_EQ(readPib(*session, "app"), before);
    if (const auto image = appImage()) {
        run(*session, session->flash("app", image, {})); // leave the board usable
    }
}

TEST_F(HilFlash, FlashByCatalogId) {
    const QString id = qEnvironmentVariable("HIO_HIL_FIRMWARE_ID");
    if (id.isEmpty()) {
        GTEST_SKIP() << "set HIO_HIL_FIRMWARE_ID";
    }
    hio::firmware::FirmwareCatalog catalog(
        hio::firmware::CatalogOptions{
            .baseUrl = hio::firmware::catalogBaseUrl("https://firmware.hardwario.com/chester"),
            .transferTimeout = std::chrono::seconds(60),
            .maxBodyBytes = 32ULL * 1024 * 1024},
        nullptr);
    auto operation = catalog.download(*hio::firmware::FirmwareId::parse(id.toStdString()),
                                      hio::firmware::DownloadOptions{.noCache = true});
    const auto firmware = hio::firmware::waitForDownload(*operation);
    ASSERT_TRUE(firmware) << firmware.error().detail;
    auto session = open("app", serialFrom("HIO_HIL_SN"));
    ASSERT_TRUE(session);
    run(*session, session->flash("app", firmware->image, {}));
}

TEST_F(HilFlash, EraseAllAndRecoverApp) {
    if (!enabled("HIO_HIL_DESTRUCTIVE")) {
        GTEST_SKIP() << "erases the PIB; set HIO_HIL_DESTRUCTIVE=1";
    }
    auto session = open("app", serialFrom("HIO_HIL_SN"));
    ASSERT_TRUE(session);
    run(*session, session->erase("app", hio::device::EraseScope::All));
    run(*session, session->recover("app"));
    if (const auto image = appImage()) {
        run(*session, session->flash("app", image, {}));
    }
}

TEST_F(HilFlash, LteResetEraseAndRecover) {
    const auto sn = serialFrom("HIO_HIL_LTE_SN");
    if (!sn) {
        GTEST_SKIP() << "set HIO_HIL_LTE_SN (a probe on the LTE connector)";
    }
    auto session = open("lte", sn);
    ASSERT_TRUE(session);
    const auto reset = run(*session, session->reset("lte", hio::device::ResetMode::Go));
    (void)reset;
    if (!enabled("HIO_HIL_DESTRUCTIVE")) {
        GTEST_SKIP() << "erase/recover of the LTE MCU need HIO_HIL_DESTRUCTIVE=1";
    }
    run(*session, session->erase("lte", hio::device::EraseScope::All));
    const auto recovered = run(*session, session->recover("lte"));
    ASSERT_TRUE(recovered.chip.has_value());
    // Still debuggable after the disable-AP-protect image and a reset (ADR-0039).
    run(*session, session->reset("lte", hio::device::ResetMode::Go));
}

TEST_F(HilFlash, LteBundle) {
    const auto sn = serialFrom("HIO_HIL_LTE_SN");
    const auto bundlePath = pathFrom("HIO_HIL_LTE_BUNDLE");
    if (!sn || bundlePath.empty()) {
        GTEST_SKIP() << "set HIO_HIL_LTE_SN and HIO_HIL_LTE_BUNDLE";
    }
    auto input = hio::firmware::classifyLteInput(bundlePath);
    ASSERT_TRUE(input) << input.error().detail;
    auto* bundle = std::get_if<hio::firmware::LteBundle>(&*input);
    ASSERT_NE(bundle, nullptr);
    auto session = open("lte", sn);
    ASSERT_TRUE(session);
    const auto result =
        run(*session,
            session->flashLteBundle(
                "lte", std::make_shared<const hio::firmware::LteBundle>(std::move(*bundle)), {}));
    std::cout << "LTE bundle took " << result.duration.count() << " ms\n";
}

} // namespace
