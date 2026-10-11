#pragma once

// Progress of the CLI flashing commands (plan/features/0003, F1):
//   TTY stderr      one bar per phase ("Programming [#####     ]  45 %"), redrawn in place
//   other stderr    one line per phase ("Programming...")
//   --json          NDJSON events on stdout:
//                   {"type":"progress","step":1,"steps":2,"phase":"programming","done":..,"total":..}
//                   {"type":"warning","error":{...}}
// Info lines (human mode only) go to stderr. Thread safety: owner thread only.

#include "cli_app.h"
#include "hio/core/error.h"
#include "hio/device/operation.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace hio::cli {

class ProgressUi {
public:
    // `err` is the human progress stream (std::cerr); `tty`: it is a terminal (bars). `quiet`:
    // no progress at all (commands that print one result envelope: reset, pib read, uicr read).
    ProgressUi(const Context& context, std::ostream& err, bool tty, bool quiet = false);
    ~ProgressUi();
    ProgressUi(const ProgressUi&) = delete;
    ProgressUi& operator=(const ProgressUi&) = delete;
    ProgressUi(ProgressUi&&) = delete;
    ProgressUi& operator=(ProgressUi&&) = delete;

    void progress(const device::OperationProgress& progress);
    // A phase outside a session operation ("downloading").
    void progress(std::string_view phaseId, std::uint64_t done, std::uint64_t total, int step = 1,
                  int steps = 1);
    void warning(const Error& error);
    // An info line (human mode only).
    void info(std::string_view text);
    // Ends the current bar line.
    void finish();

    // "Programming [##########....................]  33 %" (exposed for tests).
    [[nodiscard]] static std::string barLine(std::string_view label, std::uint64_t done,
                                             std::uint64_t total, int step, int steps);
    [[nodiscard]] static std::string_view label(std::string_view phaseId);

private:
    const Context* m_context;
    std::ostream* m_err;
    bool m_tty;
    bool m_quiet;
    std::string m_phase; // current phase id ("" = none)
    int m_step = 0;
    bool m_lineOpen = false;
    int m_lastPercent = -1;
    std::string m_lastJson; // the last NDJSON event (identical ones are skipped)
};

} // namespace hio::cli
