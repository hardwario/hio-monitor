#pragma once

// All CLI output goes through Output (plan/features/0001-m0-skeleton.md, ADR-0026).
//
// Human mode: results on stdout, errors on stderr as
//     Error: <message>          ("Error:" in red when stderr is a TTY and NO_COLOR is not set)
//     Cause: <cause>
//     What to do: <suggestion>
//     Details: <detail>
// (lines with empty text are omitted).
// Json mode: exactly one envelope per line on stdout, also for errors; streaming commands emit
// one envelope per event (NDJSON). Logs never go to stdout.

#include "hio/core/error.h"

#include <QJsonValue>

#include <cstdint>
#include <functional>
#include <ostream>
#include <string_view>

namespace hio::cli {

enum class OutputMode : std::uint8_t {
    Human,
    Json,
};

class Output {
public:
    // `out` / `err` must outlive the Output (normally std::cout / std::cerr).
    Output(OutputMode mode, std::ostream& out, std::ostream& err, bool colorErrors);

    [[nodiscard]] OutputMode mode() const noexcept { return m_mode; }

    // Final result of a command: `humanText` (Human) or {"schema":1,"ok":true,"data":...} (Json).
    void printResult(const QJsonValue& data, std::string_view humanText);
    void printError(const Error& error);
    // One event of a streaming command (NDJSON line in Json mode).
    void printEvent(const QJsonValue& event, std::string_view humanText);
    // A write to the result stream (stdout) failed, e.g. the reading end of a pipe was closed
    // (SIGPIPE is ignored, review R32). Later writes are no-ops.
    [[nodiscard]] bool outFailed() const { return m_out.get().fail(); }

private:
    void printJsonLine(const QJsonValue& data);

    OutputMode m_mode;
    std::reference_wrapper<std::ostream> m_out;
    std::reference_wrapper<std::ostream> m_err;
    bool m_colorErrors;
};

// Decides whether stderr gets colors: a TTY and NO_COLOR not set (non-empty). On Windows it
// enables virtual terminal processing of the console for its lifetime and restores the previous
// console mode in the destructor (the user's console is left as it was). One instance in main().
class StderrColor {
public:
    StderrColor();
    ~StderrColor();
    StderrColor(const StderrColor&) = delete;
    StderrColor& operator=(const StderrColor&) = delete;
    StderrColor(StderrColor&&) = delete;
    StderrColor& operator=(StderrColor&&) = delete;

    [[nodiscard]] bool enabled() const noexcept { return m_enabled; }

private:
    bool m_enabled = false;
#ifdef _WIN32
    bool m_restoreConsoleMode = false;
    std::uint32_t m_previousConsoleMode = 0;
#endif
};

} // namespace hio::cli
