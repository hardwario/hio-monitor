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

} // namespace hio
