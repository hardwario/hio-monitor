#include "hio/core/error.h"

#include "hio/core/result.h"

#include <QCoreApplication>
#include <QtTranslation>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace hio {

namespace {

consteval bool errorTableIndexMatchesEnum() {
    for (std::size_t i = 0; i < errorTable.size(); ++i) {
        if (static_cast<std::size_t>(errorTable.at(i).code) != i) {
            return false;
        }
    }
    return true;
}

consteval bool isIdChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_';
}

consteval bool errorTableIdsValid() {
    for (std::size_t i = 0; i < errorTable.size(); ++i) {
        const std::string_view id = errorTable.at(i).id;
        if (id.empty() || !std::ranges::all_of(id, isIdChar)) {
            return false;
        }
        for (std::size_t j = i + 1; j < errorTable.size(); ++j) {
            if (errorTable.at(j).id == id) {
                return false;
            }
        }
    }
    return true;
}

static_assert(errorTableIndexMatchesEnum(),
              "errorTable: the index of each row must equal its ErrorCode value");
static_assert(errorTableIdsValid(),
              "errorTable: ids must be unique, non-empty, lowercase dotted strings");
// Every enumerator has a row. The switch has no default, so -Wswitch (an error with -Werror) fails
// the build until a new ErrorCode is listed here; the static_assert then requires its row.
consteval bool isEnumerator(ErrorCode code) {
    switch (code) {
    case ErrorCode::InternalUnexpected:
    case ErrorCode::InternalQueueFull:
    case ErrorCode::UsageInvalidArgument:
    case ErrorCode::IoFileNotFound:
    case ErrorCode::IoReadFailed:
    case ErrorCode::IoWriteFailed:
    case ErrorCode::SettingsNewerSchema:
    case ErrorCode::Timeout:
    case ErrorCode::Cancelled:
    case ErrorCode::DependencyJLinkNotFound:
    case ErrorCode::DependencyJLinkLoadFailed:
    case ErrorCode::DependencyJLinkTooOld:
    case ErrorCode::ProbeNotFound:
    case ErrorCode::ProbeInUseByOtherProcess:
    case ErrorCode::ProbeOpenFailed:
    case ErrorCode::ProbeFirmwareOutdated:
    case ErrorCode::TargetNoPower:
    case ErrorCode::TargetConnectFailed:
    case ErrorCode::TargetRttNotFound:
    case ErrorCode::TargetRttBufferNotFound:
    case ErrorCode::TargetRttWriteTimeout:
    case ErrorCode::TargetRttReadFailed:
    case ErrorCode::InternalSessionLimit:
    case ErrorCode::UsageConfirmationRequired:
    case ErrorCode::TargetRttWriteFailed:
    case ErrorCode::ProbeFirmwareUpdateFailed:
    case ErrorCode::TargetNotConnected:
    case ErrorCode::FirmwareHexParseError:
    case ErrorCode::FirmwareInvalidImage:
    case ErrorCode::FirmwareOutOfRange:
    case ErrorCode::FirmwareUicrInImage:
    case ErrorCode::FirmwareVerifyFailed:
    case ErrorCode::FirmwareNotFoundInCatalog:
    case ErrorCode::FirmwareModemUpdateFailed:
    case ErrorCode::NetworkDownloadFailed:
    case ErrorCode::TargetWrongFamily:
    case ErrorCode::TargetProtected:
    case ErrorCode::TargetProgramFailed:
    case ErrorCode::TargetEraseFailed:
    case ErrorCode::TargetRecoverFailed:
    case ErrorCode::PibInvalid:
    case ErrorCode::DependencyNrfUtilNotFound:
    case ErrorCode::DependencyNrfUtilDeviceMissing:
    case ErrorCode::DependencyNrfUtilTooOld:
    case ErrorCode::InternalBusy:
    case ErrorCode::ProbeIoFailed:
    case ErrorCode::UsbAccessDenied:
    case ErrorCode::ProbeHeldByProgram:
    case ErrorCode::DiagReportWriteFailed:
    case ErrorCode::DiagCheckTimeout:
    case ErrorCode::BleAdapterNotFound:
    case ErrorCode::BleAdapterOff:
    case ErrorCode::BlePermissionDenied:
    case ErrorCode::BleStackUnavailable:
    case ErrorCode::BleDeviceNotFound:
    case ErrorCode::BleConnectFailed:
    case ErrorCode::BleServiceNotFound:
    case ErrorCode::BleNotPaired:
    case ErrorCode::BlePairingFailed:
    case ErrorCode::BlePairingNotSupported:
    case ErrorCode::BleWriteFailed:
    case ErrorCode::BleWriteTimeout:
    case ErrorCode::BleDisconnected:
    case ErrorCode::BleInUseByOtherProcess:
    case ErrorCode::BleNoPairingAgent:
    case ErrorCode::McpPortInUse:
    case ErrorCode::McpListenFailed:
    case ErrorCode::McpTokenFile:
    case ErrorCode::McpToolNotAllowed:
    case ErrorCode::McpConsentDenied:
    case ErrorCode::McpConsentTimeout:
    case ErrorCode::McpDeviceAmbiguous:
    case ErrorCode::McpDeviceNotOpen:
    case ErrorCode::McpCapabilityMissing:
    case ErrorCode::McpTooManyRequests:
    case ErrorCode::TargetNotHalted:
    case ErrorCode::TargetAddressOutOfRange:
    case ErrorCode::TargetMemoryAccessFailed:
    case ErrorCode::IoFileExists:
    case ErrorCode::DependencyJLinkFunctionMissing:
    case ErrorCode::FirmwareChecksumMismatch:
    case ErrorCode::UpdateNetwork:
    case ErrorCode::UpdateRateLimited:
    case ErrorCode::UpdateBadResponse:
    case ErrorCode::UpdateTooLarge:
        return true;
    }
    return false;
}

consteval bool everyEnumeratorHasRow() {
    using Underlying = std::underlying_type_t<ErrorCode>;
    for (std::size_t value = 0; value <= std::numeric_limits<Underlying>::max(); ++value) {
        if (isEnumerator(static_cast<ErrorCode>(value)) && value >= errorTable.size()) {
            return false;
        }
    }
    return true;
}

static_assert(everyEnumeratorHasRow(), "errorTable: every ErrorCode enumerator must have a row");

// ---- default texts (ADR-0027: translated at creation, context "hio::core") ---------------------

struct SourceText {
    const char* source;
    const char* comment;
};

struct DefaultTexts {
    ErrorCode code = ErrorCode::InternalUnexpected;
    SourceText message{};
    SourceText cause{};
    SourceText suggestion{};
    std::optional<WizardId> wizard = std::nullopt; // the wizard that helps to fix it (M4)
};

// One row per ErrorCode, in enum order (checked below). No placeholders: values go into `detail`,
// or the call site builds its own text with %1 placeholders (e.g. hio::jlink, hio::device).
constexpr std::array defaultTexts{
    DefaultTexts{
        .code = ErrorCode::InternalUnexpected,
        .message = QT_TRANSLATE_NOOP3("hio::core", "An unexpected internal error occurred.",
                                      "internal.unexpected message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "This is a bug in HARDWARIO Monitor.",
                                    "internal.unexpected cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Please report it with the log file (see Details).",
                               "internal.unexpected suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::InternalQueueFull,
        .message = QT_TRANSLATE_NOOP3("hio::core", "An internal buffer is full.",
                                      "internal.queue_full message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "This is a bug in HARDWARIO Monitor or the computer is overloaded.",
            "internal.queue_full cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Please report it with the log file (see Details).",
                               "internal.queue_full suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::UsageInvalidArgument,
        .message = QT_TRANSLATE_NOOP3("hio::core", "An argument or option is invalid.",
                                      "usage.invalid_argument message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "The request contains a value that is not supported.",
                               "usage.invalid_argument cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Check the arguments and try again.",
                                         "usage.invalid_argument suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::IoFileNotFound,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "The file was not found.", "io.file_not_found message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "The path is wrong or the file was moved or deleted.",
                               "io.file_not_found cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Check the file path (see Details).",
                                         "io.file_not_found suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::IoReadFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The file could not be read.",
                                      "io.read_failed message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "You do not have permission to read it or the disk reported an error.",
            "io.read_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core",
                                         "Check the file path and its permissions (see Details).",
                                         "io.read_failed suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::IoWriteFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The file could not be written.",
                                      "io.write_failed message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The disk is full or read-only, or you do not have permission to write.",
            "io.write_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Check the path, the free disk space and the permissions (see Details).",
            "io.write_failed suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::SettingsNewerSchema,
        .message = QT_TRANSLATE_NOOP3(
            "hio::core", "Settings were written by a newer version and are opened read-only.",
            "settings.newer_schema message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "An older version of HARDWARIO Monitor is running now.",
                               "settings.newer_schema cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Changes will not be saved; update HARDWARIO Monitor.",
                               "settings.newer_schema suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::Timeout,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The operation timed out.", "timeout message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The other side did not respond in time.",
                                    "timeout cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Try it again.", "timeout suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::Cancelled,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Cancelled by user.", "cancelled message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The operation was stopped before it finished.",
                                    "cancelled cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Run the operation again if needed.",
                                         "cancelled suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::DependencyJLinkNotFound,
        .message = QT_TRANSLATE_NOOP3("hio::core", "SEGGER J-Link Software was not found.",
                                      "dependency.jlink_not_found message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "It is not installed, or it is installed in a non-standard location.",
            "dependency.jlink_not_found cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core",
            "Install J-Link Software 8.00 or newer, or set its path with --jlink-library.",
            "dependency.jlink_not_found suggestion"),
        .wizard = WizardId::FirstRunSetup,
    },
    DefaultTexts{
        .code = ErrorCode::DependencyJLinkLoadFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The J-Link library could not be loaded.",
                                      "dependency.jlink_load_failed message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "The installation is damaged, is for another processor "
                                    "architecture, or lacks a required function.",
                                    "dependency.jlink_load_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Reinstall J-Link Software.",
                                         "dependency.jlink_load_failed suggestion"),
        .wizard = WizardId::FirstRunSetup,
    },
    DefaultTexts{
        .code = ErrorCode::DependencyJLinkTooOld,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The J-Link Software is too old.",
                                      "dependency.jlink_too_old message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "HARDWARIO Monitor needs J-Link Software 8.00 or newer.",
                                    "dependency.jlink_too_old cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Install the current J-Link Software.",
                                         "dependency.jlink_too_old suggestion"),
        .wizard = WizardId::FirstRunSetup,
    },
    DefaultTexts{
        .code = ErrorCode::ProbeNotFound,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "No J-Link probe found.", "probe.not_found message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "The USB cable is disconnected or the J-Link is not powered.",
                                    "probe.not_found cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core",
                                         "Check the USB cable between the J-Link and the computer.",
                                         "probe.not_found suggestion"),
        .wizard = WizardId::ConnectDevice,
    },
    DefaultTexts{
        .code = ErrorCode::ProbeInUseByOtherProcess,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The J-Link is used by another program.",
                                      "probe.in_use_by_other_process message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "Another HARDWARIO Monitor window or hio command is using it.",
                                    "probe.in_use_by_other_process cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Close the other program, or choose another J-Link.",
                               "probe.in_use_by_other_process suggestion"),
        .wizard = WizardId::Troubleshooting, // M4 (plan/features/0007)
    },
    DefaultTexts{
        .code = ErrorCode::ProbeOpenFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The J-Link probe could not be opened.",
                                      "probe.open_failed message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "It is used by another program or session, or the USB "
                                    "permissions are missing (Linux udev rules).",
                                    "probe.open_failed cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Close other programs using the J-Link and try again.",
                               "probe.open_failed suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::ProbeFirmwareOutdated,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The J-Link firmware is outdated.",
                                      "probe.firmware_outdated message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core",
            "The J-Link firmware differs from the firmware bundled with the J-Link Software.",
            "probe.firmware_outdated cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Run `hio jlink update-firmware`.",
                                         "probe.firmware_outdated suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::TargetNoPower,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "The device has no power.", "target.no_power message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "The device is not powered or the flat cable is disconnected.",
                                    "target.no_power cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Power the device and check the flat cable.",
                                         "target.no_power suggestion"),
        .wizard = WizardId::ConnectDevice,
    },
    DefaultTexts{
        .code = ErrorCode::TargetConnectFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Could not connect to the device.",
                                      "target.connect_failed message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "The flat cable is disconnected or in the wrong connector, or "
                                    "the MCU is in a low-power or protected state.",
                                    "target.connect_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core",
                                         "Check the flat cable between the J-Link and the device.",
                                         "target.connect_failed suggestion"),
        .wizard = WizardId::ConnectDevice,
    },
    DefaultTexts{
        .code = ErrorCode::TargetRttNotFound,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The device's console (RTT) was not found.",
                                      "target.rtt_not_found message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The firmware has no RTT console, is still booting, or the CPU is halted.",
            "target.rtt_not_found cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Reset the device or flash firmware with RTT console support.",
            "target.rtt_not_found suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::TargetRttBufferNotFound,
        .message = QT_TRANSLATE_NOOP3("hio::core", "An RTT buffer was not found.",
                                      "target.rtt_buffer_not_found message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The firmware is older or different.",
                                    "target.rtt_buffer_not_found cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Use firmware with the CHESTER shell.",
                                         "target.rtt_buffer_not_found suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::TargetRttWriteTimeout,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The device is not reading commands.",
                                      "target.rtt_write_timeout message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The firmware is hung or its shell is disabled.",
                                    "target.rtt_write_timeout cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Reset the device.",
                                         "target.rtt_write_timeout suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::TargetRttReadFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Reading from the device failed.",
                                      "target.rtt_read_failed message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "A cable was disconnected or the device was reset.",
                               "target.rtt_read_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Check the cables; the console reconnects automatically if enabled.",
            "target.rtt_read_failed suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::InternalSessionLimit,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Too many devices are open (max 8).",
                                      "internal.session_limit message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "HARDWARIO Monitor works with at most 8 devices at the same time.",
            "internal.session_limit cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Close a device first.",
                                         "internal.session_limit suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::UsageConfirmationRequired,
        .message = QT_TRANSLATE_NOOP3("hio::core", "This operation needs confirmation.",
                                      "usage.confirmation_required message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "The command runs non-interactively, so it cannot ask.",
                               "usage.confirmation_required cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Re-run with --yes.",
                                         "usage.confirmation_required suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::TargetRttWriteFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Writing to the device failed.",
                                      "target.rtt_write_failed message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "A cable was disconnected or the device was reset.",
                               "target.rtt_write_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Check the cables; the console reconnects automatically if enabled.",
            "target.rtt_write_failed suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::ProbeFirmwareUpdateFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The J-Link firmware could not be updated.",
                                      "probe.firmware_update_failed message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core",
            "The update failed or the probe still reports a firmware different from the library's.",
            "probe.firmware_update_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core",
            "Disconnect and reconnect the J-Link, then run `hio jlink update-firmware` again.",
            "probe.firmware_update_failed suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::TargetNotConnected,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The device is not connected.",
                                      "target.not_connected message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "The console is disconnected or still reconnecting.",
                               "target.not_connected cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Reconnect the console (F4).",
                                         "target.not_connected suggestion"),
        .wizard = WizardId::ConnectDevice,
    },
    // ---- M2 (plan/features/0003-m2-flashing.md) ----
    DefaultTexts{
        .code = ErrorCode::FirmwareHexParseError,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The firmware file is not a valid HEX file.",
                                      "firmware.hex_parse_error message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The file is damaged or it is not a HEX file.",
                                    "firmware.hex_parse_error cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Use the .hex file produced by the build or download it again.",
            "firmware.hex_parse_error suggestion"),
        .wizard = WizardId::FlashFirmware,
    },
    DefaultTexts{
        .code = ErrorCode::FirmwareInvalidImage,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The file is not a supported firmware file.",
                                      "firmware.invalid_image message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "It is not a CHESTER LTE bundle, a modem firmware zip or a HEX "
                                    "file, or the zip exceeds the safety limits.",
                                    "firmware.invalid_image cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Use a CHESTER .hex file or LTE bundle (.zip).",
                               "firmware.invalid_image suggestion"),
        .wizard = WizardId::FlashFirmware,
    },
    DefaultTexts{
        .code = ErrorCode::FirmwareOutOfRange,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The firmware does not fit the MCU.",
                                      "firmware.out_of_range message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The image was built for another MCU.",
                                    "firmware.out_of_range cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Check that the firmware is for this MCU.",
                                         "firmware.out_of_range suggestion"),
        .wizard = WizardId::FlashFirmware,
    },
    DefaultTexts{
        .code = ErrorCode::FirmwareUicrInImage,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "The firmware also writes the UICR configuration area.",
                               "firmware.uicr_in_image message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The build includes UICR settings.",
                                    "firmware.uicr_in_image cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core",
                                         "Re-run with --allow-uicr if this is intended; the "
                                         "Product Information Block is kept.",
                                         "firmware.uicr_in_image suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::FirmwareVerifyFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Verification of the written firmware failed.",
                                      "firmware.verify_failed message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The connection is unstable, or the flash is protected or faulty.",
            "firmware.verify_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Flash again; if it repeats, lower the J-Link speed or check the cables.",
            "firmware.verify_failed suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::FirmwareNotFoundInCatalog,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The firmware was not found in the catalog.",
                                      "firmware.not_found_in_catalog message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The firmware ID is wrong or it was deleted.",
                                    "firmware.not_found_in_catalog cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Check the firmware ID.",
                                         "firmware.not_found_in_catalog suggestion"),
        .wizard = WizardId::FlashFirmware,
    },
    DefaultTexts{
        .code = ErrorCode::FirmwareModemUpdateFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The modem firmware update failed.",
                                      "firmware.modem_update_failed message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "The modem firmware zip is wrong, the connection was lost, or "
                                    "nRF Util reported an error (see Details).",
                                    "firmware.modem_update_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Check the cable and power, then flash the modem firmware again.",
            "firmware.modem_update_failed suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::NetworkDownloadFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Downloading the firmware failed.",
                                      "network.download_failed message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core",
            "There is no internet connection, a proxy blocks it, or the server failed.",
            "network.download_failed cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Check the internet connection and try again.",
                               "network.download_failed suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::TargetWrongFamily,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The J-Link is connected to another MCU.",
                                      "target.wrong_family message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "The J-Link is connected to the other connector of the device.",
                                    "target.wrong_family cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Connect the J-Link to the connector of the MCU you want to program.",
            "target.wrong_family suggestion"),
        .wizard = WizardId::ConnectDevice,
    },
    DefaultTexts{
        .code = ErrorCode::TargetProtected,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The MCU is read-back protected.",
                                      "target.protected message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "Access port protection (APPROTECT) is enabled.",
                                    "target.protected cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core",
                                         "Recover the MCU (this erases it), then flash it again.",
                                         "target.protected suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::TargetProgramFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Programming the MCU failed.",
                                      "target.program_failed message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The connection was lost, the power is low or the MCU is protected.",
            "target.program_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Check the cables and power, then try again.",
                                         "target.program_failed suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::TargetEraseFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Erasing the MCU failed.",
                                      "target.erase_failed message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The connection was lost, the power is low or the MCU is protected.",
            "target.erase_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Check the cables and power, then try again.",
                                         "target.erase_failed suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::TargetRecoverFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Recovering the MCU failed.",
                                      "target.recover_failed message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "The device has no power or its control access port does "
                                    "not answer.",
                                    "target.recover_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Power-cycle the device and try again.",
                                         "target.recover_failed suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::PibInvalid,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "The Product Information Block is missing or damaged.",
                               "pib.invalid message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "It was never written, or it was erased by a full erase or a "
                                    "recover.",
                                    "pib.invalid cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Contact HARDWARIO support to restore it.",
                                         "pib.invalid suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::DependencyNrfUtilNotFound,
        .message = QT_TRANSLATE_NOOP3("hio::core", "nRF Util was not found.",
                                      "dependency.nrfutil_not_found message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "It is not installed; it is needed only for the modem firmware.",
            "dependency.nrfutil_not_found cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Install nRF Util and its device command, or set its path with --nrfutil.",
            "dependency.nrfutil_not_found suggestion"),
        .wizard = WizardId::FirstRunSetup,
    },
    DefaultTexts{
        .code = ErrorCode::DependencyNrfUtilDeviceMissing,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The nRF Util device command is not installed.",
                                      "dependency.nrfutil_device_missing message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "nRF Util is installed without its device command.",
                               "dependency.nrfutil_device_missing cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Run `nrfutil install device`.",
                                         "dependency.nrfutil_device_missing suggestion"),
        .wizard = WizardId::FirstRunSetup,
    },
    DefaultTexts{
        .code = ErrorCode::DependencyNrfUtilTooOld,
        .message = QT_TRANSLATE_NOOP3("hio::core", "nRF Util is too old.",
                                      "dependency.nrfutil_too_old message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "HARDWARIO Monitor needs a newer nRF Util or device command.",
                                    "dependency.nrfutil_too_old cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core",
                                         "Run `nrfutil self-upgrade` and `nrfutil upgrade device`.",
                                         "dependency.nrfutil_too_old suggestion"),
        .wizard = WizardId::FirstRunSetup,
    },
    DefaultTexts{
        .code = ErrorCode::InternalBusy,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Another operation is running on this device.",
                                      "internal.busy message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "Only one operation can run at a time.",
                                    "internal.busy cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Wait for it to finish or cancel it.",
                                         "internal.busy suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::ProbeIoFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Communication with the J-Link failed.",
                                      "probe.io_failed message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core",
            "The J-Link did not answer: a USB glitch, or the USB cable was disconnected.",
            "probe.io_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core",
            "Check the USB cable between the J-Link and the computer; the console reconnects "
            "automatically if enabled.",
            "probe.io_failed suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    // M4 (plan/features/0007-m4-wizard-framework-diagnostics.md, ADR-0072, ADR-0073, ADR-0075)
    DefaultTexts{
        .code = ErrorCode::UsbAccessDenied,
        .message = QT_TRANSLATE_NOOP3(
            "hio::core", "This computer does not allow access to the J-Link (USB permissions).",
            "usb.access_denied message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The udev rule for SEGGER devices is missing.",
                                    "usb.access_denied cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core",
            "Install the udev rule shown in the setup wizard, then reconnect the J-Link.",
            "usb.access_denied suggestion"),
        .wizard = WizardId::FirstRunSetup,
    },
    DefaultTexts{
        .code = ErrorCode::ProbeHeldByProgram,
        .message = QT_TRANSLATE_NOOP3("hio::core",
                                      "Another program is running and may be using the J-Link.",
                                      "probe.held_by_program message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "Another debugging or programming tool keeps the probe open.",
                                    "probe.held_by_program cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Close the other program and try again.",
                                         "probe.held_by_program suggestion"),
        .wizard = WizardId::Troubleshooting,
    },
    DefaultTexts{
        .code = ErrorCode::DiagReportWriteFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The diagnostic report could not be saved.",
                                      "diag.report_write_failed message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The folder is not writable or the disk is full.",
                                    "diag.report_write_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Choose another folder.",
                                         "diag.report_write_failed suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::DiagCheckTimeout,
        .message = QT_TRANSLATE_NOOP3("hio::core", "A check did not finish in time.",
                                      "diag.check_timeout message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The system is slow or a tool does not answer.",
                                    "diag.check_timeout cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Run the checks again.",
                                         "diag.check_timeout suggestion"),
    },
    // ---- M5 (plan/features/0011-m5-ble-backend-cli.md) ----
    DefaultTexts{
        .code = ErrorCode::BleAdapterNotFound,
        .message = QT_TRANSLATE_NOOP3("hio::core", "No Bluetooth adapter was found.",
                                      "ble.adapter_not_found message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The computer has no Bluetooth or it is disabled in the system.",
            "ble.adapter_not_found cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Enable Bluetooth or connect a USB Bluetooth adapter.",
                               "ble.adapter_not_found suggestion"),
        .wizard = WizardId::BlePairing,
    },
    DefaultTexts{
        .code = ErrorCode::BleAdapterOff,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "Bluetooth is turned off.", "ble.adapter_off message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core",
            "Bluetooth is turned off in the system settings or with a hardware switch.",
            "ble.adapter_off cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Turn Bluetooth on in the system settings.",
                                         "ble.adapter_off suggestion"),
        .wizard = WizardId::BlePairing,
    },
    DefaultTexts{
        .code = ErrorCode::BlePermissionDenied,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "HARDWARIO Monitor is not allowed to use Bluetooth.",
                               "ble.permission_denied message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "Bluetooth access was denied in the privacy settings.",
                               "ble.permission_denied cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core",
                                         "Allow Bluetooth for HARDWARIO Monitor (or your terminal) "
                                         "in System Settings → Privacy & Security → Bluetooth.",
                                         "ble.permission_denied suggestion"),
        .wizard = WizardId::FirstRunSetup,
    },
    DefaultTexts{
        .code = ErrorCode::BleStackUnavailable,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "The Bluetooth service of the system is not available.",
                               "ble.stack_unavailable message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The Bluetooth service of the system (BlueZ on Linux) is not running.",
            "ble.stack_unavailable cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Start the Bluetooth service: `sudo systemctl start bluetooth`.",
            "ble.stack_unavailable suggestion"),
        .wizard = WizardId::BlePairing,
    },
    DefaultTexts{
        .code = ErrorCode::BleDeviceNotFound,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The Bluetooth device was not found.",
                                      "ble.device_not_found message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The device is out of range, turned off or its Bluetooth is disabled.",
            "ble.device_not_found cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Bring the device closer and make sure its Bluetooth is on.",
            "ble.device_not_found suggestion"),
        .wizard = WizardId::BlePairing,
    },
    DefaultTexts{
        .code = ErrorCode::BleConnectFailed,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "Could not connect to the device over Bluetooth.",
                               "ble.connect_failed message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The signal is weak or the device is busy with another connection.",
            "ble.connect_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core",
            "Move closer, disconnect other phones or computers from the device and try again.",
            "ble.connect_failed suggestion"),
        .wizard = WizardId::BlePairing,
    },
    DefaultTexts{
        .code = ErrorCode::BleServiceNotFound,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "The device does not offer the console service (NUS).",
                               "ble.service_not_found message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The firmware has no Bluetooth shell.",
                                    "ble.service_not_found cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Use firmware with the Bluetooth console enabled.",
                               "ble.service_not_found suggestion"),
        .wizard = WizardId::BlePairing,
    },
    DefaultTexts{
        .code = ErrorCode::BleNotPaired,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The device is not paired with this computer.",
                                      "ble.not_paired message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "Pairing was cancelled, failed or was removed on one side.",
                                    "ble.not_paired cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core",
                                         "Pair the device: enter its passkey when the system asks.",
                                         "ble.not_paired suggestion"),
        .wizard = WizardId::BlePairing,
    },
    DefaultTexts{
        .code = ErrorCode::BlePairingFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Pairing with the device failed.",
                                      "ble.pairing_failed message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "A wrong passkey, a timeout or a stale bond on one side.",
                                    "ble.pairing_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Remove the device in the system Bluetooth settings and pair again.",
            "ble.pairing_failed suggestion"),
        .wizard = WizardId::BlePairing,
    },
    DefaultTexts{
        .code = ErrorCode::BlePairingNotSupported,
        .message = QT_TRANSLATE_NOOP3("hio::core",
                                      "This system pairs Bluetooth devices only when connecting.",
                                      "ble.pairing_not_supported message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "macOS pairs a device automatically when it connects.",
                               "ble.pairing_not_supported cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Connect to the device; macOS asks for the passkey.",
                               "ble.pairing_not_supported suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::BleWriteFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Sending to the device over Bluetooth failed.",
                                      "ble.write_failed message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The link was lost during the write.",
                                    "ble.write_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Reconnect.", "ble.write_failed suggestion"),
        .wizard = WizardId::BlePairing,
    },
    DefaultTexts{
        .code = ErrorCode::BleWriteTimeout,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The device is not reading commands.",
                                      "ble.write_timeout message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core", "The firmware is busy or the link is stalled.",
                                    "ble.write_timeout cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Reconnect; if it repeats, restart the device.",
                               "ble.write_timeout suggestion"),
        .wizard = WizardId::BlePairing,
    },
    DefaultTexts{
        .code = ErrorCode::BleDisconnected,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "The Bluetooth connection to the device was lost.",
                               "ble.disconnected message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "The device is out of range, was restarted or turned off.",
                                    "ble.disconnected cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core",
            "Bring the device closer; the console reconnects automatically if enabled.",
            "ble.disconnected suggestion"),
        .wizard = WizardId::BlePairing,
    },
    DefaultTexts{
        .code = ErrorCode::BleInUseByOtherProcess,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The device is used by another program.",
                                      "ble.in_use_by_other_process message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "Another HARDWARIO Monitor window or `hio` command is connected to it.",
            "ble.in_use_by_other_process cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Close the other program.",
                                         "ble.in_use_by_other_process suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::BleNoPairingAgent,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "No program is ready to ask for the Bluetooth passkey.",
                               "ble.no_pairing_agent message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core",
            "No pairing agent is running (on GNOME the agent may only be active in Settings → "
            "Bluetooth).",
            "ble.no_pairing_agent cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core",
            "Pair the device from the system Bluetooth settings, or with bluetoothctl: "
            "`agent KeyboardOnly`, `default-agent`, `pair <id>`.",
            "ble.no_pairing_agent suggestion"),
        .wizard = WizardId::BlePairing,
    },
    DefaultTexts{
        .code = ErrorCode::McpPortInUse,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The MCP port is already in use.",
                                      "mcp.port_in_use message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "Another HARDWARIO Monitor, `hio mcp --http` or another MCP "
                                    "server (for example rttt) is listening on it.",
                                    "mcp.port_in_use cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Stop the other server or choose another port with --port.",
            "mcp.port_in_use suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::McpListenFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The MCP server could not start.",
                                      "mcp.listen_failed message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The operating system refused to open the listening socket on 127.0.0.1.",
            "mcp.listen_failed cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Choose another port with --port.",
                                         "mcp.listen_failed suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::McpTokenFile,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The MCP token file cannot be read or created.",
                                      "mcp.token_file message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "The file or its folder has the wrong permissions, or the file "
                                    "does not contain a valid token.",
                                    "mcp.token_file cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Check the file permissions, or delete the file to create a new token.",
            "mcp.token_file suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::McpToolNotAllowed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "This MCP tool is not allowed by the server.",
                                      "mcp.tool_not_allowed message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "Destructive tools must be allowed when the server starts.",
                                    "mcp.tool_not_allowed cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core",
            "Start the server with --allow and the tool's category, or with --allow all.",
            "mcp.tool_not_allowed suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::McpConsentDenied,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The user did not allow this action.",
                                      "mcp.consent_denied message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The user declined the permission request in HARDWARIO Monitor.",
            "mcp.consent_denied cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Ask the user to allow it in HARDWARIO Monitor.",
                               "mcp.consent_denied suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::McpConsentTimeout,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Nobody answered the permission request.",
                                      "mcp.consent_timeout message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core",
            "The permission request in HARDWARIO Monitor was not answered within 2 minutes.",
            "mcp.consent_timeout cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core",
                                         "Ask the user to watch HARDWARIO Monitor and try again.",
                                         "mcp.consent_timeout suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::McpDeviceAmbiguous,
        .message = QT_TRANSLATE_NOOP3(
            "hio::core",
            "Several devices are connected and the tool does not know which one to use.",
            "mcp.device_ambiguous message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The device argument was left out while more than one device is open.",
            "mcp.device_ambiguous cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Pass the device argument; list_devices shows the ids.",
                               "mcp.device_ambiguous suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::McpDeviceNotOpen,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The device is not connected.",
                                      "mcp.device_not_open message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "It was never connected, it was disconnected, or its tab was closed.",
            "mcp.device_not_open cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Call connect first.",
                                         "mcp.device_not_open suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::McpCapabilityMissing,
        .message = QT_TRANSLATE_NOOP3("hio::core", "This tool needs a J-Link connection.",
                                      "mcp.capability_missing message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "The device is connected over Bluetooth, which provides only the console.",
            "mcp.capability_missing cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Connect the device with a J-Link probe.",
                                         "mcp.capability_missing suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::McpTooManyRequests,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Too many MCP requests are running.",
                                      "mcp.too_many_requests message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "The server runs at most 32 tool calls at the same time.",
                                    "mcp.too_many_requests cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Wait for running calls to finish and try again.",
                               "mcp.too_many_requests suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::TargetNotHalted,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "The CPU is running.", "target.not_halted message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "Registers can be read only while the core is halted.",
                               "target.not_halted cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Halt the core first.", "target.not_halted suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::TargetAddressOutOfRange,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "The address range is not allowed for this operation.",
                               "target.address_out_of_range message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core",
            "Memory writes are allowed only in the RAM and peripheral ranges of the target.",
            "target.address_out_of_range cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Use an address inside the RAM or peripheral ranges of the target.",
            "target.address_out_of_range suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::TargetMemoryAccessFailed,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Reading or writing target memory failed.",
                                      "target.memory_access_failed message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "The address is not mapped, or the core is in a fault state.",
                                    "target.memory_access_failed cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Check the address and try again after a reset.",
                               "target.memory_access_failed suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::IoFileExists,
        .message = QT_TRANSLATE_NOOP3("hio::core", "The output file already exists.",
                                      "io.file_exists message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "Existing files are never overwritten by this command.",
                               "io.file_exists cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Choose a new file name or delete the file first.",
                               "io.file_exists suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::DependencyJLinkFunctionMissing,
        .message = QT_TRANSLATE_NOOP3(
            "hio::core",
            "The installed J-Link software does not provide a function this operation needs.",
            "dependency.jlink_function_missing message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "The J-Link library is older than, or different from, the "
                                    "versions HARDWARIO Monitor was tested with.",
                                    "dependency.jlink_function_missing cause"),
        .suggestion = QT_TRANSLATE_NOOP3(
            "hio::core", "Install the latest J-Link Software and Documentation Pack.",
            "dependency.jlink_function_missing suggestion"),
    },
    DefaultTexts{
        .code = ErrorCode::FirmwareChecksumMismatch,
        .message = QT_TRANSLATE_NOOP3("hio::core",
                                      "The downloaded firmware is damaged (checksum mismatch).",
                                      "firmware.checksum_mismatch message"),
        .cause =
            QT_TRANSLATE_NOOP3("hio::core", "The file was changed or damaged during the download.",
                               "firmware.checksum_mismatch cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core", "Try again; if it repeats, report it to HARDWARIO.",
                               "firmware.checksum_mismatch suggestion"),
        .wizard = std::nullopt,
    },
    // M7 update check (plan/features/0016, ADR-0116).
    DefaultTexts{
        .code = ErrorCode::UpdateNetwork,
        .message = QT_TRANSLATE_NOOP3("hio::core", "Could not reach GitHub to check for updates.",
                                      "update.network message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core", "There is no internet connection, or a firewall or proxy blocks GitHub.",
            "update.network cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Check your connection or try again later.",
                                         "update.network suggestion"),
        .wizard = std::nullopt,
    },
    DefaultTexts{
        .code = ErrorCode::UpdateRateLimited,
        .message = QT_TRANSLATE_NOOP3("hio::core", "GitHub temporarily refused the update check.",
                                      "update.rate_limited message"),
        .cause = QT_TRANSLATE_NOOP3(
            "hio::core",
            "GitHub limits how often a network address may ask; other computers on the same "
            "network may have used up the limit.",
            "update.rate_limited cause"),
        .suggestion = QT_TRANSLATE_NOOP3("hio::core", "Try again in an hour.",
                                         "update.rate_limited suggestion"),
        .wizard = std::nullopt,
    },
    DefaultTexts{
        .code = ErrorCode::UpdateBadResponse,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "The update information from GitHub could not be read.",
                               "update.bad_response message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "GitHub sent an unexpected answer, or a proxy changed it.",
                                    "update.bad_response cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core",
                               "Try again later; if it persists, check the releases page: "
                               "https://github.com/hardwario/hio-monitor/releases",
                               "update.bad_response suggestion"),
        .wizard = std::nullopt,
    },
    // Plan 0016: an internal bound was hit; reported with the bad_response texts (the bound in
    // `detail`).
    DefaultTexts{
        .code = ErrorCode::UpdateTooLarge,
        .message =
            QT_TRANSLATE_NOOP3("hio::core", "The update information from GitHub could not be read.",
                               "update.bad_response message"),
        .cause = QT_TRANSLATE_NOOP3("hio::core",
                                    "GitHub sent an unexpected answer, or a proxy changed it.",
                                    "update.bad_response cause"),
        .suggestion =
            QT_TRANSLATE_NOOP3("hio::core",
                               "Try again later; if it persists, check the releases page: "
                               "https://github.com/hardwario/hio-monitor/releases",
                               "update.bad_response suggestion"),
        .wizard = std::nullopt,
    },
};

consteval bool defaultTextsComplete() {
    if (defaultTexts.size() != errorTable.size()) {
        return false;
    }
    for (std::size_t i = 0; i < defaultTexts.size(); ++i) {
        const DefaultTexts& row = defaultTexts.at(i);
        if (static_cast<std::size_t>(row.code) != i) {
            return false;
        }
        for (const SourceText& text : {row.message, row.cause, row.suggestion}) {
            if (text.source == nullptr || std::string_view(text.source).empty()) {
                return false;
            }
        }
    }
    return true;
}

static_assert(defaultTextsComplete(),
              "defaultTexts: every ErrorCode needs a row (in enum order) with non-empty texts");

std::string translate(const SourceText& text) {
    return QCoreApplication::translate("hio::core", text.source, text.comment).toStdString();
}

} // namespace

Error makeError(ErrorCode code, std::string detail) {
    const DefaultTexts& texts = defaultTexts.at(static_cast<std::size_t>(code));
    return Error{.code = code,
                 .message = translate(texts.message),
                 .cause = translate(texts.cause),
                 .suggestion = translate(texts.suggestion),
                 .detail = std::move(detail),
                 .wizard = texts.wizard};
}

Error outputClosedError(std::string detail) {
    Error error = makeError(ErrorCode::Cancelled, std::move(detail));
    error.message = QCoreApplication::translate("hio::core", "The output was closed.",
                                                "cancelled message when the stdout was closed")
                        .toStdString();
    return error;
}

std::unexpected<Error> fail(ErrorCode code, std::string detail) {
    return std::unexpected<Error>(makeError(code, std::move(detail)));
}

std::optional<ErrorCode> errorCodeFromId(std::string_view id) noexcept {
    const auto* const it =
        std::ranges::find_if(errorTable, [id](const ErrorInfo& info) { return info.id == id; });
    if (it == errorTable.end()) {
        return std::nullopt;
    }
    return it->code;
}

std::string_view categoryId(ErrorCategory category) noexcept {
    switch (category) {
    case ErrorCategory::Usage:
        return "usage";
    case ErrorCategory::Io:
        return "io";
    case ErrorCategory::Dependency:
        return "dependency";
    case ErrorCategory::Probe:
        return "probe";
    case ErrorCategory::Target:
        return "target";
    case ErrorCategory::Firmware:
        return "firmware";
    case ErrorCategory::Network:
        return "network";
    case ErrorCategory::Timeout:
        return "timeout";
    case ErrorCategory::Cancelled:
        return "cancelled";
    case ErrorCategory::Internal:
        return "internal";
    }
    return "internal";
}

std::unexpected<Error> fail(ErrorCode code, std::string message, std::string cause,
                            std::string suggestion, std::string detail) {
    return std::unexpected<Error>(Error{.code = code,
                                        .message = std::move(message),
                                        .cause = std::move(cause),
                                        .suggestion = std::move(suggestion),
                                        .detail = std::move(detail),
                                        .wizard = std::nullopt});
}

} // namespace hio
