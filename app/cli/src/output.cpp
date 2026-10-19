#include "output.h"

#include "hio/core/error.h"
#include "hio/core/json.h"

#include <QJsonValue>
#include <QtEnvironmentVariables>

#include <algorithm>
#include <cstddef>
#include <format>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
// NOMINMAX and WIN32_LEAN_AND_MEAN are set by app/cli/CMakeLists.txt.
#include <cstdio>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace hio::cli {

namespace {

constexpr std::string_view red = "\x1b[31m";
constexpr std::string_view reset = "\x1b[0m";

void printLine(std::ostream& stream, std::string_view label, std::string_view text) {
    if (!text.empty()) {
        stream << std::format("{} {}\n", label, text);
    }
}

} // namespace

Output::Output(OutputMode mode, std::ostream& out, std::ostream& err, bool colorErrors)
    : m_mode(mode), m_out(out), m_err(err), m_colorErrors(colorErrors) {}

void Output::printResult(const QJsonValue& data, std::string_view humanText) {
    if (m_mode == OutputMode::Json) {
        printJsonLine(data);
        return;
    }
    if (!humanText.empty()) {
        m_out.get() << humanText << (humanText.ends_with('\n') ? "" : "\n");
    }
    m_out.get().flush();
}

void Output::printError(const Error& error) {
    if (m_mode == OutputMode::Json) {
        m_out.get() << toCompactUtf8(errorEnvelope(error)) << '\n';
        m_out.get().flush();
        return;
    }
    if (m_colorErrors) {
        m_err.get() << std::format("{}Error:{} {}\n", red, reset, error.message);
    } else {
        m_err.get() << std::format("Error: {}\n", error.message);
    }
    printLine(m_err.get(), "Cause:", error.cause);
    printLine(m_err.get(), "What to do:", error.suggestion);
    printLine(m_err.get(), "Details:", error.detail);
    m_err.get().flush();
}

void Output::printFailure(const Error& error, const QJsonValue& data, std::string_view humanText) {
    if (m_mode == OutputMode::Json) {
        m_out.get() << toCompactUtf8(errorEnvelope(error, data)) << '\n';
        m_out.get().flush();
        return;
    }
    if (!humanText.empty()) {
        m_out.get() << humanText << (humanText.ends_with('\n') ? "" : "\n");
    }
    m_out.get().flush();
}

void Output::printEvent(const QJsonValue& event, std::string_view humanText) {
    printResult(event, humanText);
}

void Output::printJsonLine(const QJsonValue& data) {
    m_out.get() << toCompactUtf8(okEnvelope(data)) << '\n';
    m_out.get().flush();
}

StderrColor::StderrColor() {
    if (!qEnvironmentVariableIsEmpty("NO_COLOR")) {
        return;
    }
#ifdef _WIN32
    if (_isatty(_fileno(stderr)) == 0) {
        return;
    }
    HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD consoleMode = 0;
    if (handle == INVALID_HANDLE_VALUE || GetConsoleMode(handle, &consoleMode) == 0) {
        return;
    }
    if ((consoleMode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
        m_enabled = true;
        return;
    }
    if (SetConsoleMode(handle, consoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
        m_enabled = true;
        m_restoreConsoleMode = true;
        m_previousConsoleMode = consoleMode;
    }
#else
    m_enabled = isatty(STDERR_FILENO) != 0;
#endif
}

StderrColor::~StderrColor() {
#ifdef _WIN32
    if (m_restoreConsoleMode) {
        SetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), m_previousConsoleMode);
    }
#endif
}

namespace {

// Characters (UTF-8 code points) of `text`, for column alignment.
std::size_t displayWidth(std::string_view text) {
    return static_cast<std::size_t>(std::ranges::count_if(
        text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
}

std::string padded(std::string text, std::size_t width) {
    const std::size_t current = displayWidth(text);
    if (current < width) {
        text.append(width - current, ' ');
    }
    return text;
}

} // namespace

// A table with columns two spaces apart (widths in characters).
std::string formatTable(const std::vector<std::string>& header,
                        const std::vector<std::vector<std::string>>& rows) {
    std::vector<std::size_t> widths;
    for (std::size_t column = 0; column < header.size(); ++column) {
        std::size_t width = displayWidth(header[column]);
        for (const auto& row : rows) {
            width = std::max(width, displayWidth(row[column]));
        }
        widths.push_back(width + 2);
    }
    const auto formatRow = [&](const std::vector<std::string>& row) {
        std::string line;
        for (std::size_t column = 0; column < row.size(); ++column) {
            line += column + 1 < row.size() ? padded(row[column], widths[column]) : row[column];
        }
        return line + "\n";
    };
    std::string text = formatRow(header);
    for (const auto& row : rows) {
        text += formatRow(row);
    }
    return text;
}

} // namespace hio::cli
