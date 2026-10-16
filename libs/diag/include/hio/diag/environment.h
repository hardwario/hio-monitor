#pragma once

// The environment checks shared by `hio doctor` and the wizards (plan/features/0007 § Check list
// texts, ADR-0068), in this order:
//   jlink-software  J-Link Software located, loadable, version >= 8.00         Failed when not
//   nrfutil         nRF Util and its device command (only for the LTE modem)   Warning when not
//   usb-access      Linux USB access to SEGGER devices (ADR-0072)              Failed / Skipped
//   probe-holders   programs that may hold the probe (ADR-0073)                Warning
//   probes          J-Link probes                                              Warning when none
//   probe-firmware  firmware of the free probes                                Warning when
//   outdated app-dirs        settings and log folders writable                          Failed when
//   not
// `probes` and `probe-firmware` need `jlink-software` (Skipped otherwise). The library copy loaded
// by `jlink-software` (ADR-0030, own instance directory) is kept for `probes` and unloaded after
// `probe-firmware` (or when the checks are destroyed).
//
// The checks run on one caller-owned thread (CheckRunner); they use no Qt objects across threads.

#include "hio/diag/check.h"
#include "hio/diag/process_scan.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"
#include "hio/hw/nrfutil/nrfutil.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hio::diag {

// What the checks found, for the diagnostic report (filled while they run).
struct EnvironmentFacts {
    std::optional<std::filesystem::path> jlinkLibrary;
    std::uint32_t jlinkVersion = 0;
    std::optional<std::filesystem::path> nrfUtil;
    std::string nrfUtilVersion;       // "7.13.0" ("" unknown)
    std::string nrfUtilDeviceVersion; // "2.7.4"
    struct Probe {
        jlink::ProbeInfo info;
        std::string inUseBy; // "hio-monitor (PID 4242)", "" when free
        std::optional<jlink::FirmwareStatus> firmware;
    };
    std::vector<Probe> probes; // bounded by the library's probe list
};

[[nodiscard]] QJsonObject toJson(const EnvironmentFacts& facts);

struct EnvironmentContext {
    // Overrides as in M1/M2: CLI option > environment > setting > auto (defaultEnvironment()).
    jlink::LocatorOptions jlink;
    nrfutil::LocatorOptions nrfutil;
    std::filesystem::path settingsDir;
    std::filesystem::path logDir;
    std::shared_ptr<IProcessSource> processes; // nullptr = the platform source
    // Library copies and probe locks; empty = the defaults (JLinkLibrary::defaultInstanceRoot(),
    // ProbeLock::defaultDirectory()).
    std::filesystem::path instanceRoot;
    std::filesystem::path lockDirectory;
    std::filesystem::path usbSysfsRoot = "/sys"; // ADR-0072 (tests: a temporary tree)
    std::filesystem::path usbDevRoot = "/dev";
    // Filled by the checks when set (diagnostic report).
    std::shared_ptr<EnvironmentFacts> facts;
};

// The context of this user: the given CLI overrides, the environment, the settings, the default
// settings and log directories (requires initApplicationIdentity()).
[[nodiscard]] EnvironmentContext
defaultEnvironment(std::optional<std::filesystem::path> jlinkOption = std::nullopt,
                   std::optional<std::filesystem::path> nrfUtilOption = std::nullopt);

[[nodiscard]] std::vector<Check> environmentChecks(const EnvironmentContext& context);

// The ids in execution order (UI check lists before the results arrive).
[[nodiscard]] std::vector<std::string> environmentCheckIds();

} // namespace hio::diag
