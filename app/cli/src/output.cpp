#include "output.h"

#include "hio/core/error.h"
#include "hio/core/json.h"

#include <QJsonValue>
#include <QtEnvironmentVariables>

#include <format>
#include <functional>
#include <ostream>
#include <string_view>

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

} // namespace hio::cli
