// HARDWARIO CHESTER (plan/features/0002-m1-jlink-rtt.md, plan/features/0003-m2-flashing.md;
// py-hardwario hardwario/chester/cli/app.py, lte.py, hardwario/device/nrfjprog.py).

#include "hio/device/profile.h"
#include "hio/firmware/designated_firmware.h"
#include "hio/hw/jlink/programmer.h"
#include "profiles.h"

#include <QtTranslation>

#include <array>
#include <optional>

namespace hio::device::profiles {

namespace {

// FICR INFO.PART values and J-Link device names. The nRF9151 reports 0x9120 (py-hardwario maps
// the nrfjprog name NRF9120_xxAA to the J-Link name NRF9151_XXCA).
constexpr std::array appParts{
    ChipPart{.part = 0x52840, .displayName = "nRF52840", .jlinkDevice = "NRF52840_xxAA"},
};
constexpr std::array ltePartsList{
    ChipPart{.part = 0x9120, .displayName = "nRF9151", .jlinkDevice = "NRF9151_XXCA"},
    ChipPart{.part = 0x9160, .displayName = "nRF9160", .jlinkDevice = "NRF9160_xxAA"},
};

// FICR / CTRL-AP (nRF52840 and nRF91 product specifications; AP indexes and CTRL-AP IDR values to
// confirm in Phase 0). The masked IDRs are equal (a Nordic CTRL-AP); the AP index (1 / 4) is the
// family discriminator (review R15).
constexpr jlink::ChipFamily nrf52{.name = "nRF52",
                                  .ficrPart = 0x10000100,
                                  .ficrVariant = 0x10000104,
                                  .ctrlApIndex = 1,
                                  .ctrlApIdr = 0x02880000};
constexpr jlink::ChipFamily nrf91{.name = "nRF91",
                                  .ficrPart = 0x00FF0140,
                                  .ficrVariant = 0x00FF0144,
                                  .ctrlApIndex = 4,
                                  .ctrlApIdr = 0x12880000};

// Connection guides (ADR-0077). Neutral wording until HARDWARIO provides the connector names, the
// cable type and the pin-1 marking (open question 22, before 0009 is Done).
constexpr std::array appGuide{
    ConnectionGuideStep{
        .id = "cable",
        .illustrationSlot = "chester-app-cable",
        .titleSourceText = QT_TRANSLATE_NOOP("hio::device", "Cable"),
        .bodySourceText = QT_TRANSLATE_NOOP(
            "hio::device",
            "Connect the J-Link flat cable to the programming connector of the application MCU."),
    },
};
constexpr std::array lteGuide{
    ConnectionGuideStep{
        .id = "cable",
        .illustrationSlot = "chester-lte-cable",
        .titleSourceText = QT_TRANSLATE_NOOP("hio::device", "Cable"),
        .bodySourceText = QT_TRANSLATE_NOOP(
            "hio::device",
            "Connect the J-Link flat cable to the programming connector of the LTE MCU."),
    },
};

// BLE (ADR-0089, plan 0011 decisions Q8): the Zephyr shell over the Nordic UART Service.
constexpr std::array blePairingGuide{
    ConnectionGuideStep{
        .id = "bluetooth",
        .illustrationSlot = "ble-enable",
        .titleSourceText = QT_TRANSLATE_NOOP("hio::device", "Bluetooth"),
        .bodySourceText = QT_TRANSLATE_NOOP(
            "hio::device", "Make sure the device is powered and its Bluetooth is on."),
    },
    ConnectionGuideStep{
        .id = "pair",
        .illustrationSlot = "ble-pair-os",
        .titleSourceText = QT_TRANSLATE_NOOP("hio::device", "Pairing"),
        .bodySourceText = QT_TRANSLATE_NOOP(
            "hio::device", "When the system asks for the passkey, enter it in the system dialog."),
    },
};

// ADR-0165: the LTE firmware HARDWARIO supports for CHESTER (docs.hardwario.com, "LTE Modem over
// J-Link"); SHA-256 measured 2026-10-20. A new version is a profile change in a release.
constexpr firmware::DesignatedFirmware lteFirmware{
    .name = "HARDWARIO LTE firmware",
    .version = "1.7.0",
    .url = "https://docs.hardwario.com/download/hio-chester-lte-v1.7.0.zip",
    .sha256 =
        firmware::sha256FromHex("95d301a11738bcd9a9df4726159361d64fd8f681c3546f479d46b49f4bf0fe49"),
    .kind = firmware::FirmwareKind::LteBundle,
};

// ADR-0167: the documentation list of application firmware, English first (the fallback).
constexpr std::array firmwareDocs{
    LocalizedUrl{.language = "en",
                 .url =
                     "https://docs.hardwario.com/chester/catalog-applications/catalog-applications/"
                     "#application-firmware"},
    LocalizedUrl{
        .language = "cs",
        .url = "https://docs.hardwario.com/cs/chester/catalog-applications/catalog-applications/"
               "#application-firmware"},
};

constexpr std::array chesterTargets{
    // Application MCU: the RTT console of `hio chester app console` / `command`.
    TargetProfile{.id = "app",
                  .displayName = QT_TRANSLATE_NOOP("hio::device", "application"),
                  .jlinkDevice = "NRF52840_xxAA",
                  .rttTerminal = "Terminal",
                  .rttLogger = "Logger",
                  .hashPrefixIsLog = true,
                  .defaultSpeedKhz = std::nullopt, // automatic (ADR-0090)
                  .defaultLatencyMs = 50,
                  .minTargetMillivolts = 1000,
                  .parts = appParts,
                  .chipFamily = nrf52,
                  .flash = {.start = 0x00000000, .end = 0x00100000},
                  .uicr = {.start = 0x10001000, .end = 0x10002000},
                  .pibOffset = 0x80,
                  .pibErasableWithUicr = true,
                  .recoverImage = RecoverImage::None,
                  .defaultErase = EraseScope::Code,
                  .connectionGuide = appGuide,
                  .designatedFirmware = std::nullopt},
    // LTE modem MCU (nRF9151 or nRF9160). Programming only; no RTT console. Connected with the
    // first part's J-Link name, then with the exact part's name (to confirm in Phase 0). The PIB
    // lies in the UICR OTP area, which a UICR erase does not touch.
    TargetProfile{.id = "lte",
                  .displayName = QT_TRANSLATE_NOOP("hio::device", "LTE"),
                  .jlinkDevice = "NRF9151_XXCA",
                  .rttTerminal = "",
                  .rttLogger = "",
                  .hashPrefixIsLog = false,
                  .defaultSpeedKhz = std::nullopt, // automatic (ADR-0090)
                  .defaultLatencyMs = 50,
                  .minTargetMillivolts = 1000,
                  .parts = ltePartsList,
                  .chipFamily = nrf91,
                  .flash = {.start = 0x00000000, .end = 0x00100000},
                  .uicr = {.start = 0x00FF8000, .end = 0x00FF9000},
                  .pibOffset = 0x108,
                  .pibErasableWithUicr = false,
                  .recoverImage = RecoverImage::Nrf91DisableApProtect,
                  .defaultErase = EraseScope::All,
                  .connectionGuide = lteGuide,
                  .designatedFirmware = lteFirmware},
};

} // namespace

constinit const DeviceProfile chester{
    .id = "chester",
    .displayName = "CHESTER",
    .targets = chesterTargets,
    .firmwareCatalogUrl = "https://firmware.hardwario.com/chester",
    .powerHintSourceText =
        QT_TRANSLATE_NOOP("hio::device", "Power CHESTER with batteries or the USB-C power input."),
    .ble =
        BleProfile{
            .namePattern = "CHESTER",
            .serviceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
            .rxUuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
            .txUuid = "6e400003-b5a3-f393-e0a9-e50e24dcca9e",
            .shellPrompt = "bt_nus:~$ ",
            .consoleTargetId = "app",
            .passkeyHintSourceText = QT_TRANSLATE_NOOP(
                "hio::device",
                "The passkey is 123456 unless it was changed when the device was configured."),
            .pairingGuide = blePairingGuide,
        },
    .firmwareDocsUrl = firmwareDocs};

} // namespace hio::device::profiles
