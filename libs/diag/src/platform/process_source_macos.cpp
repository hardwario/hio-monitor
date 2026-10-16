// macOS process source (ADR-0073): libproc proc_listallpids + proc_pidpath + proc_name.

#include <QtSystemDetection>

#ifdef Q_OS_MACOS

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "platform/process_source.h"

#include <QtTypes>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <libproc.h>
#include <memory>
#include <stop_token>
#include <string>
#include <sys/param.h> // MAXCOMLEN
#include <sys/proc_info.h>
#include <sys/types.h> // pid_t
#include <utility>
#include <vector>

namespace hio::diag::platform {

namespace {

class MacProcessSource final : public IProcessSource {
public:
    Result<std::vector<Process>> list(std::size_t maxCount, const std::stop_token& stop) override {
        std::vector<pid_t> pids(maxCount);
        const int bytes =
            proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
        if (bytes <= 0) {
            return std::unexpected(makeError(ErrorCode::IoReadFailed, "proc_listallpids failed"));
        }
        // proc_listallpids returns the number of PIDs (not bytes) on current macOS versions.
        const auto count = std::min(pids.size(), static_cast<std::size_t>(bytes));
        std::vector<Process> processes;
        std::array<char, PROC_PIDPATHINFO_MAXSIZE> path{};
        std::array<char, 2 * MAXCOMLEN + 1> name{};
        for (std::size_t i = 0; i < count; ++i) {
            if (stop.stop_requested()) {
                return std::unexpected(makeError(ErrorCode::Timeout, "process list incomplete"));
            }
            const pid_t pid = pids.at(i);
            if (pid <= 0) {
                continue;
            }
            Process process;
            process.pid = pid;
            if (const int length =
                    proc_pidpath(pid, path.data(), static_cast<std::uint32_t>(path.size()));
                length > 0) {
                process.executablePath.assign(path.data(), static_cast<std::size_t>(length));
            }
            if (const int length =
                    proc_name(pid, name.data(), static_cast<std::uint32_t>(name.size()));
                length > 0) {
                process.name.assign(name.data(), static_cast<std::size_t>(length));
            }
            if (!process.name.empty() || !process.executablePath.empty()) {
                processes.push_back(std::move(process));
            }
        }
        return processes;
    }
};

} // namespace

std::shared_ptr<IProcessSource> makeProcessSource() { return std::make_shared<MacProcessSource>(); }

} // namespace hio::diag::platform

#endif
