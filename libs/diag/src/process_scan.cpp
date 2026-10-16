#include "hio/diag/process_scan.h"

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "platform/process_source.h"

#include <QCoreApplication>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::diag {

namespace {

enum class Match : std::uint8_t { Exact, Prefix };

struct KnownHolder {
    std::string_view pattern; // lowercase, without ".exe"
    Match match;
    std::string_view displayName;
};

// ADR-0073: data, easy to extend when new tools appear.
constexpr std::array knownHolders{
    KnownHolder{.pattern = "jlinkexe", .match = Match::Exact, .displayName = "J-Link Commander"},
    KnownHolder{.pattern = "jlink", .match = Match::Exact, .displayName = "J-Link Commander"},
    KnownHolder{
        .pattern = "jlinkgdbserver", .match = Match::Prefix, .displayName = "J-Link GDB Server"},
    KnownHolder{
        .pattern = "jlinkrttviewer", .match = Match::Prefix, .displayName = "J-Link RTT Viewer"},
    KnownHolder{
        .pattern = "jlinkrttlogger", .match = Match::Prefix, .displayName = "J-Link RTT Logger"},
    KnownHolder{.pattern = "jflash", .match = Match::Prefix, .displayName = "J-Flash"},
    KnownHolder{.pattern = "ozone", .match = Match::Prefix, .displayName = "Ozone"},
    KnownHolder{.pattern = "nrfutil", .match = Match::Prefix, .displayName = "nRF Util"},
    KnownHolder{.pattern = "nrfjprog", .match = Match::Prefix, .displayName = "nrfjprog"},
    KnownHolder{
        .pattern = "nrf connect", .match = Match::Prefix, .displayName = "nRF Connect for Desktop"},
    KnownHolder{
        .pattern = "nrfconnect", .match = Match::Prefix, .displayName = "nRF Connect for Desktop"},
    KnownHolder{.pattern = "hiomon",
                .match = Match::Prefix,
                .displayName = "HARDWARIO Monitor (version 1)"},
    KnownHolder{.pattern = "hardwario monitor",
                .match = Match::Prefix,
                .displayName = "HARDWARIO Monitor (version 1)"},
    KnownHolder{.pattern = "pyocd", .match = Match::Exact, .displayName = "pyOCD"},
    KnownHolder{.pattern = "openocd", .match = Match::Exact, .displayName = "OpenOCD"},
};

// Our own executables use probe locks (ADR-0058) and are reported through `inUseBy`.
constexpr std::array ownExecutables{std::string_view("hio"), std::string_view("hio-monitor")};

std::string normalized(std::string_view name) {
    std::string lower(name);
    std::ranges::transform(lower, lower.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    if (lower.ends_with(".exe")) {
        lower.resize(lower.size() - 4);
    }
    return lower;
}

std::string fileName(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

std::string knownHolderName(std::string_view name) {
    const std::string lower = normalized(name);
    if (lower.empty() || std::ranges::find(ownExecutables, lower) != ownExecutables.end()) {
        return {};
    }
    for (const KnownHolder& holder : knownHolders) {
        const bool matches = holder.match == Match::Exact ? lower == holder.pattern
                                                          : lower.starts_with(holder.pattern);
        if (matches) {
            return std::string(holder.displayName);
        }
    }
    return {};
}

Result<std::vector<ProcessMatch>> findProbeHolders(IProcessSource& source,
                                                   const std::stop_token& stop) {
    auto processes = source.list(maxInspectedProcesses, stop);
    if (!processes) {
        return std::unexpected(std::move(processes).error());
    }
    const qint64 self = QCoreApplication::applicationPid();
    std::vector<ProcessMatch> matches;
    for (const IProcessSource::Process& process : *processes) {
        if (process.pid == self) {
            continue;
        }
        const std::string executable = fileName(process.executablePath);
        const std::string executableNormalized = normalized(executable);
        if (std::ranges::find(ownExecutables, executableNormalized) != ownExecutables.end() ||
            std::ranges::find(ownExecutables, normalized(process.name)) != ownExecutables.end()) {
            continue;
        }
        std::string display = knownHolderName(executable);
        std::string matched = executable;
        if (display.empty()) {
            display = knownHolderName(process.name);
            matched = process.name;
        }
        if (!display.empty()) {
            matches.push_back(ProcessMatch{
                .displayName = std::move(display), .executable = matched, .pid = process.pid});
        }
    }
    std::ranges::sort(matches, {}, &ProcessMatch::pid);
    return matches;
}

} // namespace hio::diag

namespace hio::diag {

namespace {

// An OS without a process source: the check reports Skipped through io.read_failed.
class NoProcessSource final : public IProcessSource {
public:
    Result<std::vector<Process>> list(std::size_t /*maxCount*/,
                                      const std::stop_token& /*stop*/) override {
        return std::unexpected(
            makeError(ErrorCode::IoReadFailed, "no process list on this operating system"));
    }
};

} // namespace

std::shared_ptr<IProcessSource> platformProcessSource() {
    if (auto source = platform::makeProcessSource()) {
        return source;
    }
    return std::make_shared<NoProcessSource>();
}

} // namespace hio::diag
