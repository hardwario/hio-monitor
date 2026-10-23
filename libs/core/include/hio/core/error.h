#pragma once

// hio::Error - the single error type of the backend (ADR-0007, ADR-0024).
//
// Every error has a stable ErrorCode. The code has a stable dotted string id (part of the public
// CLI/JSON contract, never changes meaning once released) and an ErrorCategory, which is also
// the CLI exit code. errorTable below is the single source of truth for both.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hio {

enum class ErrorCategory : std::uint8_t {
    Usage,
    Io,
    Dependency,
    Probe,
    Target,
    Firmware,
    Network,
    Timeout,
    Cancelled,
    Internal,
};

// Order must match errorTable (checked by static_asserts below).
enum class ErrorCode : std::uint8_t {
    InternalUnexpected,
    InternalQueueFull,
    UsageInvalidArgument,
    IoFileNotFound,
    IoReadFailed,
    IoWriteFailed,
    SettingsNewerSchema,
    Timeout,
    Cancelled,
    // M1 (plan/features/0002-m1-jlink-rtt.md)
    DependencyJLinkNotFound,
    DependencyJLinkLoadFailed,
    DependencyJLinkTooOld,
    ProbeNotFound,
    ProbeInUseByOtherProcess,
    ProbeOpenFailed,
    ProbeFirmwareOutdated,
    TargetNoPower,
    TargetConnectFailed,
    TargetRttNotFound,
    TargetRttBufferNotFound,
    TargetRttWriteTimeout,
    TargetRttReadFailed,
    InternalSessionLimit,
    UsageConfirmationRequired,
    TargetRttWriteFailed,      // review fix 2026-10-09
    ProbeFirmwareUpdateFailed, // review R11, 2026-10-10
    TargetNotConnected,        // review R18, 2026-10-10
    // M2 (plan/features/0003-m2-flashing.md)
    FirmwareHexParseError,
    FirmwareInvalidImage,
    FirmwareOutOfRange,
    FirmwareUicrInImage,
    FirmwareVerifyFailed,
    FirmwareNotFoundInCatalog,
    FirmwareModemUpdateFailed,
    NetworkDownloadFailed,
    TargetWrongFamily,
    TargetProtected,
    TargetProgramFailed,
    TargetEraseFailed,
    TargetRecoverFailed,
    PibInvalid,
    DependencyNrfUtilNotFound,
    DependencyNrfUtilDeviceMissing,
    DependencyNrfUtilTooOld,
    InternalBusy,
    ProbeIoFailed, // review R52, 2026-10-11
    // M4 (plan/features/0007-m4-wizard-framework-diagnostics.md)
    UsbAccessDenied,
    ProbeHeldByProgram,
    DiagReportWriteFailed,
    DiagCheckTimeout,
    // M5 (plan/features/0011-m5-ble-backend-cli.md)
    BleAdapterNotFound,
    BleAdapterOff,
    BlePermissionDenied,
    BleStackUnavailable,
    BleDeviceNotFound,
    BleConnectFailed,
    BleServiceNotFound,
    BleNotPaired,
    BlePairingFailed,
    BlePairingNotSupported,
    BleWriteFailed,
    BleWriteTimeout,
    BleDisconnected,
    BleInUseByOtherProcess,
    // M6 (plan/features/0013-m6-mcp-core-cli.md)
    McpPortInUse,
    McpListenFailed,
    McpTokenFile,
    McpToolNotAllowed,
    McpConsentDenied,
    McpConsentTimeout,
    McpDeviceAmbiguous,
    McpDeviceNotOpen,
    McpCapabilityMissing,
    McpTooManyRequests,
    TargetNotHalted,
    TargetAddressOutOfRange,
    TargetMemoryAccessFailed,
    IoFileExists,
    DependencyJLinkFunctionMissing,
};

// Wizards the UI can offer to fix an error (M4). Serialized as kebab-case ids.
enum class WizardId : std::uint8_t {
    FirstRunSetup,
    ConnectDevice,
    FlashFirmware,
    Troubleshooting,
    BlePairing,
};

struct ErrorInfo {
    ErrorCode code;
    std::string_view id;
    ErrorCategory category;
};

// Single source of truth: code -> stable id -> category. Index == enum value.
inline constexpr std::array errorTable{
    ErrorInfo{.code = ErrorCode::InternalUnexpected,
              .id = "internal.unexpected",
              .category = ErrorCategory::Internal},
    ErrorInfo{.code = ErrorCode::InternalQueueFull,
              .id = "internal.queue_full",
              .category = ErrorCategory::Internal},
    ErrorInfo{.code = ErrorCode::UsageInvalidArgument,
              .id = "usage.invalid_argument",
              .category = ErrorCategory::Usage},
    ErrorInfo{.code = ErrorCode::IoFileNotFound,
              .id = "io.file_not_found",
              .category = ErrorCategory::Io},
    ErrorInfo{
        .code = ErrorCode::IoReadFailed, .id = "io.read_failed", .category = ErrorCategory::Io},
    ErrorInfo{
        .code = ErrorCode::IoWriteFailed, .id = "io.write_failed", .category = ErrorCategory::Io},
    ErrorInfo{.code = ErrorCode::SettingsNewerSchema,
              .id = "settings.newer_schema",
              .category = ErrorCategory::Io},
    ErrorInfo{.code = ErrorCode::Timeout, .id = "timeout", .category = ErrorCategory::Timeout},
    ErrorInfo{
        .code = ErrorCode::Cancelled, .id = "cancelled", .category = ErrorCategory::Cancelled},
    ErrorInfo{.code = ErrorCode::DependencyJLinkNotFound,
              .id = "dependency.jlink_not_found",
              .category = ErrorCategory::Dependency},
    ErrorInfo{.code = ErrorCode::DependencyJLinkLoadFailed,
              .id = "dependency.jlink_load_failed",
              .category = ErrorCategory::Dependency},
    ErrorInfo{.code = ErrorCode::DependencyJLinkTooOld,
              .id = "dependency.jlink_too_old",
              .category = ErrorCategory::Dependency},
    ErrorInfo{.code = ErrorCode::ProbeNotFound,
              .id = "probe.not_found",
              .category = ErrorCategory::Probe},
    ErrorInfo{.code = ErrorCode::ProbeInUseByOtherProcess,
              .id = "probe.in_use_by_other_process",
              .category = ErrorCategory::Probe},
    ErrorInfo{.code = ErrorCode::ProbeOpenFailed,
              .id = "probe.open_failed",
              .category = ErrorCategory::Probe},
    ErrorInfo{.code = ErrorCode::ProbeFirmwareOutdated,
              .id = "probe.firmware_outdated",
              .category = ErrorCategory::Probe},
    ErrorInfo{.code = ErrorCode::TargetNoPower,
              .id = "target.no_power",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::TargetConnectFailed,
              .id = "target.connect_failed",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::TargetRttNotFound,
              .id = "target.rtt_not_found",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::TargetRttBufferNotFound,
              .id = "target.rtt_buffer_not_found",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::TargetRttWriteTimeout,
              .id = "target.rtt_write_timeout",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::TargetRttReadFailed,
              .id = "target.rtt_read_failed",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::InternalSessionLimit,
              .id = "internal.session_limit",
              .category = ErrorCategory::Internal},
    ErrorInfo{.code = ErrorCode::UsageConfirmationRequired,
              .id = "usage.confirmation_required",
              .category = ErrorCategory::Usage},
    ErrorInfo{.code = ErrorCode::TargetRttWriteFailed,
              .id = "target.rtt_write_failed",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::ProbeFirmwareUpdateFailed,
              .id = "probe.firmware_update_failed",
              .category = ErrorCategory::Probe},
    ErrorInfo{.code = ErrorCode::TargetNotConnected,
              .id = "target.not_connected",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::FirmwareHexParseError,
              .id = "firmware.hex_parse_error",
              .category = ErrorCategory::Firmware},
    ErrorInfo{.code = ErrorCode::FirmwareInvalidImage,
              .id = "firmware.invalid_image",
              .category = ErrorCategory::Firmware},
    ErrorInfo{.code = ErrorCode::FirmwareOutOfRange,
              .id = "firmware.out_of_range",
              .category = ErrorCategory::Firmware},
    ErrorInfo{.code = ErrorCode::FirmwareUicrInImage,
              .id = "firmware.uicr_in_image",
              .category = ErrorCategory::Firmware},
    ErrorInfo{.code = ErrorCode::FirmwareVerifyFailed,
              .id = "firmware.verify_failed",
              .category = ErrorCategory::Firmware},
    ErrorInfo{.code = ErrorCode::FirmwareNotFoundInCatalog,
              .id = "firmware.not_found_in_catalog",
              .category = ErrorCategory::Firmware},
    ErrorInfo{.code = ErrorCode::FirmwareModemUpdateFailed,
              .id = "firmware.modem_update_failed",
              .category = ErrorCategory::Firmware},
    ErrorInfo{.code = ErrorCode::NetworkDownloadFailed,
              .id = "network.download_failed",
              .category = ErrorCategory::Network},
    ErrorInfo{.code = ErrorCode::TargetWrongFamily,
              .id = "target.wrong_family",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::TargetProtected,
              .id = "target.protected",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::TargetProgramFailed,
              .id = "target.program_failed",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::TargetEraseFailed,
              .id = "target.erase_failed",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::TargetRecoverFailed,
              .id = "target.recover_failed",
              .category = ErrorCategory::Target},
    ErrorInfo{
        .code = ErrorCode::PibInvalid, .id = "pib.invalid", .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::DependencyNrfUtilNotFound,
              .id = "dependency.nrfutil_not_found",
              .category = ErrorCategory::Dependency},
    ErrorInfo{.code = ErrorCode::DependencyNrfUtilDeviceMissing,
              .id = "dependency.nrfutil_device_missing",
              .category = ErrorCategory::Dependency},
    ErrorInfo{.code = ErrorCode::DependencyNrfUtilTooOld,
              .id = "dependency.nrfutil_too_old",
              .category = ErrorCategory::Dependency},
    ErrorInfo{.code = ErrorCode::InternalBusy,
              .id = "internal.busy",
              .category = ErrorCategory::Internal},
    ErrorInfo{.code = ErrorCode::ProbeIoFailed,
              .id = "probe.io_failed",
              .category = ErrorCategory::Probe},
    ErrorInfo{.code = ErrorCode::UsbAccessDenied,
              .id = "usb.access_denied",
              .category = ErrorCategory::Probe},
    ErrorInfo{.code = ErrorCode::ProbeHeldByProgram,
              .id = "probe.held_by_program",
              .category = ErrorCategory::Probe},
    ErrorInfo{.code = ErrorCode::DiagReportWriteFailed,
              .id = "diag.report_write_failed",
              .category = ErrorCategory::Io},
    ErrorInfo{.code = ErrorCode::DiagCheckTimeout,
              .id = "diag.check_timeout",
              .category = ErrorCategory::Timeout},
    ErrorInfo{.code = ErrorCode::BleAdapterNotFound,
              .id = "ble.adapter_not_found",
              .category = ErrorCategory::Dependency},
    ErrorInfo{.code = ErrorCode::BleAdapterOff,
              .id = "ble.adapter_off",
              .category = ErrorCategory::Dependency},
    ErrorInfo{.code = ErrorCode::BlePermissionDenied,
              .id = "ble.permission_denied",
              .category = ErrorCategory::Dependency},
    ErrorInfo{.code = ErrorCode::BleStackUnavailable,
              .id = "ble.stack_unavailable",
              .category = ErrorCategory::Dependency},
    ErrorInfo{.code = ErrorCode::BleDeviceNotFound,
              .id = "ble.device_not_found",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::BleConnectFailed,
              .id = "ble.connect_failed",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::BleServiceNotFound,
              .id = "ble.service_not_found",
              .category = ErrorCategory::Target},
    ErrorInfo{
        .code = ErrorCode::BleNotPaired, .id = "ble.not_paired", .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::BlePairingFailed,
              .id = "ble.pairing_failed",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::BlePairingNotSupported,
              .id = "ble.pairing_not_supported",
              .category = ErrorCategory::Usage},
    ErrorInfo{.code = ErrorCode::BleWriteFailed,
              .id = "ble.write_failed",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::BleWriteTimeout,
              .id = "ble.write_timeout",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::BleDisconnected,
              .id = "ble.disconnected",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::BleInUseByOtherProcess,
              .id = "ble.in_use_by_other_process",
              .category = ErrorCategory::Probe},
    ErrorInfo{
        .code = ErrorCode::McpPortInUse, .id = "mcp.port_in_use", .category = ErrorCategory::Io},
    ErrorInfo{.code = ErrorCode::McpListenFailed,
              .id = "mcp.listen_failed",
              .category = ErrorCategory::Io},
    ErrorInfo{
        .code = ErrorCode::McpTokenFile, .id = "mcp.token_file", .category = ErrorCategory::Io},
    ErrorInfo{.code = ErrorCode::McpToolNotAllowed,
              .id = "mcp.tool_not_allowed",
              .category = ErrorCategory::Usage},
    ErrorInfo{.code = ErrorCode::McpConsentDenied,
              .id = "mcp.consent_denied",
              .category = ErrorCategory::Cancelled},
    ErrorInfo{.code = ErrorCode::McpConsentTimeout,
              .id = "mcp.consent_timeout",
              .category = ErrorCategory::Timeout},
    ErrorInfo{.code = ErrorCode::McpDeviceAmbiguous,
              .id = "mcp.device_ambiguous",
              .category = ErrorCategory::Usage},
    ErrorInfo{.code = ErrorCode::McpDeviceNotOpen,
              .id = "mcp.device_not_open",
              .category = ErrorCategory::Usage},
    ErrorInfo{.code = ErrorCode::McpCapabilityMissing,
              .id = "mcp.capability_missing",
              .category = ErrorCategory::Usage},
    ErrorInfo{.code = ErrorCode::McpTooManyRequests,
              .id = "mcp.too_many_requests",
              .category = ErrorCategory::Internal},
    ErrorInfo{.code = ErrorCode::TargetNotHalted,
              .id = "target.not_halted",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::TargetAddressOutOfRange,
              .id = "target.address_out_of_range",
              .category = ErrorCategory::Target},
    ErrorInfo{.code = ErrorCode::TargetMemoryAccessFailed,
              .id = "target.memory_access_failed",
              .category = ErrorCategory::Target},
    ErrorInfo{
        .code = ErrorCode::IoFileExists, .id = "io.file_exists", .category = ErrorCategory::Io},
    ErrorInfo{.code = ErrorCode::DependencyJLinkFunctionMissing,
              .id = "dependency.jlink_function_missing",
              .category = ErrorCategory::Dependency},
};

// Compile-time checks of errorTable (ids unique and well-formed, index == enum value, every
// enumerator present) are in error.cpp.

[[nodiscard]] constexpr std::string_view idOf(ErrorCode code) noexcept {
    return errorTable.at(static_cast<std::size_t>(code)).id;
}

[[nodiscard]] constexpr ErrorCategory categoryOf(ErrorCode code) noexcept {
    return errorTable.at(static_cast<std::size_t>(code)).category;
}

// Looks up a code by its stable id ("io.file_not_found"); nullopt for unknown ids.
[[nodiscard]] std::optional<ErrorCode> errorCodeFromId(std::string_view id) noexcept;

// Stable lowercase id of a category ("probe", "io", ...), used in JSON output.
[[nodiscard]] std::string_view categoryId(ErrorCategory category) noexcept;

struct Error {
    ErrorCode code = ErrorCode::InternalUnexpected;
    std::string message;    // what happened (translated, UTF-8)
    std::string cause;      // why it probably happened (translated, UTF-8)
    std::string suggestion; // what the user should do (translated, UTF-8)
    std::string detail;     // technical detail: codes, paths, versions (not translated)
    std::optional<WizardId> wizard;
};

// Error with the default texts of `code`, translated into the current process language
// (context "hio::core", ADR-0027). The texts have no placeholders; values such as paths go into
// `detail`. Call sites that know more use the full hio::fail(code, message, cause, ...).
[[nodiscard]] Error makeError(ErrorCode code, std::string detail = {});

// `cancelled` because the reader of the result stream went away (e.g. `hio ... --json | head`, a
// closed stdout; review R32, R44): the default cause and suggestion of `cancelled` with the message
// "The output was closed." instead of "Cancelled by user.".
[[nodiscard]] Error outputClosedError(std::string detail);

} // namespace hio
