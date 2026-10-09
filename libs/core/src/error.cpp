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
    ErrorCode code;
    SourceText message;
    SourceText cause;
    SourceText suggestion;
};

// One row per ErrorCode, in enum order (checked below). No placeholders: values go into `detail`.
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
                 .wizard = std::nullopt};
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
