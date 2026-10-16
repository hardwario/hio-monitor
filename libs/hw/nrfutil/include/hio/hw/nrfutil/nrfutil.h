#pragma once

// Nordic nRF Util as an optional subprocess for the nRF91 modem firmware (ADR-0016, ADR-0042,
// plan/features/0003-m2-flashing.md). Only this library starts nRF Util.
//
// Locator precedence: --nrfutil option > environment HIO_NRFUTIL > setting nrfutil/path > PATH
// lookup of `nrfutil` (`nrfutil.exe`) > standard install directories (~/.nrfutil/bin,
// %USERPROFILE%\.nrfutil\bin, /usr/local/bin). An override that does not exist is an error.
//
// Versions (ADR-0042): `nrfutil --version` and `nrfutil device --version`, 10 s each. The minimums
// below are provisional until the Phase 0 hardware spike records the tested versions.
//
// Modem update: `nrfutil device program --firmware <zip> --serial-number <SN> --json` (arguments as
// a list, no shell, environment inherited; exact arguments and the J-Link serial number format to
// confirm in Phase 0). NDJSON progress lines are parsed tolerantly (unknown fields ignored,
// progress optional). stdout and stderr are capped at maxOutputBytes each (excess dropped and
// counted, the tail kept for `detail`); overall timeout 10 min; cancel = terminate, kill after
// 5 s. The QProcess is owned by the calling thread (the session worker) and driven with its
// blocking waits; no event loop is needed.

#include "hio/core/result.h"
#include "hio/core/settings.h"
#include "hio/firmware/program_progress.h"

#include <QJsonObject>

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

class QProcess;

namespace hio::nrfutil {

// ---- locating -------------------------------------------------------------------------------

inline constexpr std::string_view environmentVariable = "HIO_NRFUTIL";

struct LocatorOptions {
    std::optional<std::filesystem::path> option;      // CLI --nrfutil
    std::optional<std::filesystem::path> environment; // HIO_NRFUTIL
    std::optional<std::filesystem::path> setting;     // nrfutil/path
    // Directories searched for the executable, in order (default: the PATH entries).
    std::vector<std::filesystem::path> searchPath;
    // Standard install directories searched after the PATH.
    std::vector<std::filesystem::path> installDirs;
};

// "nrfutil/path", empty = auto.
[[nodiscard]] SettingKey<std::filesystem::path> pathSetting();

// Options with the environment, the setting (when given), the PATH and the install directories of
// this OS filled in.
[[nodiscard]] LocatorOptions defaultLocatorOptions(std::optional<std::filesystem::path> option,
                                                   const Settings* settings);

// Errors: dependency.nrfutil_not_found (searched paths in `detail`).
[[nodiscard]] Result<std::filesystem::path> locate(const LocatorOptions& options);

// ---- versions -------------------------------------------------------------------------------

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    friend auto operator<=>(const Version&, const Version&) = default;
};

[[nodiscard]] std::string formatVersion(const Version& version);
// The first "X.Y.Z" in `text` (e.g. "nrfutil 7.13.0 (abc 2024-10-01)").
[[nodiscard]] std::optional<Version> parseVersion(std::string_view text);

// Provisional minimums (ADR-0042 "_tbd_"; to confirm in Phase 0).
inline constexpr Version minimumCoreVersion{.major = 7, .minor = 0, .patch = 0};
inline constexpr Version minimumDeviceVersion{.major = 2, .minor = 0, .patch = 0};

struct NrfUtilInfo {
    std::filesystem::path executable;
    Version core;
    Version device;
};

// Runs the two version probes. A stop request ends a running probe at once (the process is
// killed; polled every 100 ms). Errors: dependency.nrfutil_not_found (does not start),
// dependency.nrfutil_device_missing, dependency.nrfutil_too_old, timeout, cancelled (stop).
[[nodiscard]] Result<NrfUtilInfo>
checkVersions(const std::filesystem::path& executable,
              std::chrono::milliseconds timeout = std::chrono::seconds(10),
              const std::stop_token& stop = {});

// ---- modem update ---------------------------------------------------------------------------

class IModemUpdater {
public:
    IModemUpdater() = default;
    virtual ~IModemUpdater() = default;
    IModemUpdater(const IModemUpdater&) = delete;
    IModemUpdater& operator=(const IModemUpdater&) = delete;
    IModemUpdater(IModemUpdater&&) = delete;
    IModemUpdater& operator=(IModemUpdater&&) = delete;

    // Programs the modem firmware zip through the probe `probeSn` (our session has released the
    // probe). Progress phase Modem. Errors: firmware.modem_update_failed, timeout, cancelled.
    [[nodiscard]] virtual Status updateModem(const std::filesystem::path& modemZip,
                                             std::uint32_t probeSn,
                                             const firmware::ProgressFn& progress,
                                             std::stop_token stop) = 0;
};

struct ModemUpdaterTiming {
    std::chrono::milliseconds overallTimeout{std::chrono::minutes(10)};
    std::chrono::milliseconds terminateGrace{std::chrono::seconds(5)};
    std::chrono::milliseconds poll{100};
    std::size_t maxOutputBytes = 1024ULL * 1024; // per stream
};

class NrfUtilModemUpdater final : public IModemUpdater {
public:
    explicit NrfUtilModemUpdater(std::filesystem::path executable, ModemUpdaterTiming timing = {});

    [[nodiscard]] Status updateModem(const std::filesystem::path& modemZip, std::uint32_t probeSn,
                                     const firmware::ProgressFn& progress,
                                     std::stop_token stop) override;

    // The arguments of the program call (exposed for tests).
    [[nodiscard]] static std::vector<std::string>
    programArguments(const std::filesystem::path& modemZip, std::uint32_t probeSn);

private:
    // Waits until `process` ends, calling `drain` for its output; terminates (then kills) it on a
    // stop request (cancelled) or at the overall timeout (timeout).
    [[nodiscard]] Status waitForProcess(QProcess& process, const std::function<void()>& drain,
                                        const std::stop_token& stop) const;

    std::filesystem::path m_executable;
    ModemUpdaterTiming m_timing;
};

// What one NDJSON line of `nrfutil ... --json` says (exposed for tests).
struct JsonEvent {
    std::optional<int> percent;  // progress 0..100
    std::optional<bool> success; // a task_end result
    std::string message;         // error / description text
};
[[nodiscard]] JsonEvent parseJsonEvent(const QJsonObject& line);

} // namespace hio::nrfutil
