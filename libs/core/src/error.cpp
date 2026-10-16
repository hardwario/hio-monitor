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
    case ErrorCode::ProbeIoFailed:
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
