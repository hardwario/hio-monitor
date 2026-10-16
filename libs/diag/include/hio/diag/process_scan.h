#pragma once

// Programs that may hold the J-Link probe (ADR-0073, plan/features/0007). The running processes are
// matched against a compiled-in list of known holders (J-Link Commander, GDB server, nRF Util,
// ...). Our own processes (this PID, `hio`, `hio-monitor`) are excluded: they use probe locks
// (ADR-0058). The result is a hint; the application never terminates other processes.
//
// Bounds: at most maxInspectedProcesses processes; the check gives up after processScanTimeout.

#include "hio/core/result.h"

#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace hio::diag {

inline constexpr std::size_t maxInspectedProcesses = 4096;
inline constexpr std::chrono::milliseconds processScanTimeout{2000};

struct ProcessMatch {
    std::string displayName; // "J-Link Commander" (product names, not translated)
    std::string executable;  // the matched process name or executable file name
    qint64 pid = 0;
};

// The running processes (platform implementation in src/platform/, or a test fake).
class IProcessSource {
public:
    IProcessSource() = default;
    virtual ~IProcessSource() = default;
    IProcessSource(const IProcessSource&) = delete;
    IProcessSource& operator=(const IProcessSource&) = delete;
    IProcessSource(IProcessSource&&) = delete;
    IProcessSource& operator=(IProcessSource&&) = delete;

    struct Process {
        qint64 pid = 0;
        std::string name;           // process name (Linux: /proc/<pid>/comm, max. 15 characters)
        std::string executablePath; // "" when unknown (no permission, Windows)
    };
    // At most `maxCount` processes. Errors: timeout (stop requested before the list was complete),
    // io.read_failed (the process list is not available, e.g. a sandbox).
    [[nodiscard]] virtual Result<std::vector<Process>> list(std::size_t maxCount,
                                                            const std::stop_token& stop) = 0;
};

// The source of this operating system.
[[nodiscard]] std::shared_ptr<IProcessSource> platformProcessSource();

// The display name of a known holder whose process name or executable file name is `name`
// (case-insensitive; a trailing ".exe" is ignored), "" for other programs and for our own.
[[nodiscard]] std::string knownHolderName(std::string_view name);

// The known holders among the running processes (by PID). Errors: those of IProcessSource::list.
[[nodiscard]] Result<std::vector<ProcessMatch>> findProbeHolders(IProcessSource& source,
                                                                 const std::stop_token& stop);

} // namespace hio::diag
