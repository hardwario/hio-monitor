#pragma once

// Application diagnostic log (ADR-0020): Qt logging categories + our own message handler that
// writes to stderr (configurable threshold) and to a bounded file sink.
//
// Bounds (plan/features/0001-m0-skeleton.md):
//   - one file per process run: <gui|cli>-YYYYMMDD-HHMMSS-<pid>.log (UTC), in the log directory
//   - at maxFileBytes the run continues in <name>.part2.log, <name>.part3.log, ...
//   - at startup and on every rotation the files of the own AppKind (gui-* or cli-*) are pruned to
//     retentionFor(kind): the newest maxFiles files and at most maxTotalBytes in total. Files of
//     the other kind are never touched, so frequent CLI runs cannot delete a running GUI's log (on
//     POSIX an open file can be deleted). Pruning is by age: once a file is deleted, all older ones
//     are deleted too. The current file is always kept; deletion failures are ignored.
//   - a single message is truncated to maxMessageBytes
// Line format: 2026-10-09T12:34:56.789Z [inf] hio.core (0x7f...) text
// The file receives every message that passes the Qt category filter (debug and above by default);
// stderr only receives messages at or above Options::stderrLevel.
// Thread safety: the message handler may be called from any thread (one mutex in the sink). The
// file is flushed on warning and above and when a fatal message or std::terminate is handled.

#include "hio/core/result.h"

#include <QLoggingCategory>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace hio::log {

Q_DECLARE_LOGGING_CATEGORY(lcCore) // "hio.core"
Q_DECLARE_LOGGING_CATEGORY(
    lcJLink) // "hio.jlink"  (libs/hw/jlink, incl. the J-Link library's own output)
Q_DECLARE_LOGGING_CATEGORY(lcDevice) // "hio.device" (libs/device)
Q_DECLARE_LOGGING_CATEGORY(
    lcFirmware)                     // "hio.firmware" (libs/firmware: images, zips, catalog, cache)
Q_DECLARE_LOGGING_CATEGORY(lcFlash) // "hio.flash" (programming workflows, nRF Util; plan 0003)
Q_DECLARE_LOGGING_CATEGORY(lcDiag)  // "hio.diag" (libs/diag: checks, report; plan 0007)
Q_DECLARE_LOGGING_CATEGORY(lcBle)   // "hio.ble" (libs/hw/ble, BLE sessions; plan 0011)
Q_DECLARE_LOGGING_CATEGORY(lcMcp)   // "hio.mcp" (libs/mcp, libs/mcp_tools, `hio mcp`; plan 0013)

enum class AppKind : std::uint8_t {
    Gui,
    Cli,
};

enum class Level : std::uint8_t {
    Debug,    // qDebug
    Info,     // qInfo
    Warning,  // qWarning
    Error,    // qCritical
    Critical, // qFatal
};

struct Options {
    Level stderrLevel = Level::Critical;
    // Default: QStandardPaths::AppLocalDataLocation/logs (requires initApplicationIdentity()).
    std::optional<std::filesystem::path> logDir;
};

inline constexpr std::uintmax_t maxFileBytes = 10ULL * 1024 * 1024;
inline constexpr std::size_t maxMessageBytes = 64ULL * 1024;

// Retention of the log files of one AppKind (decided 2026-10-09: separate quotas per kind).
struct RetentionLimits {
    std::size_t maxFiles;         // including the current file
    std::uintmax_t maxTotalBytes; // including the current file
};

inline constexpr RetentionLimits guiRetention{.maxFiles = 10, .maxTotalBytes = 50ULL * 1024 * 1024};
inline constexpr RetentionLimits cliRetention{.maxFiles = 20, .maxTotalBytes = 50ULL * 1024 * 1024};

[[nodiscard]] constexpr RetentionLimits retentionFor(AppKind kind) noexcept {
    return kind == AppKind::Gui ? guiRetention : cliRetention;
}

class Sink;

// The default log directory (<AppLocalData>/logs; requires initApplicationIdentity()), empty when
// the platform has none. Used by Logging::init and the diagnostic report (plan/features/0007).
[[nodiscard]] std::filesystem::path defaultLogDirectory();

// RAII: installs the message handler and a std::terminate handler; the destructor flushes and
// restores the previous handlers. Only one instance may exist at a time (process-wide handler).
class Logging {
    struct Passkey {
        explicit Passkey() = default;
    };

public:
    // Fails only when another Logging instance is alive (`internal.unexpected`). An unwritable log
    // directory is not an error: logging continues on stderr only and a warning is recorded.
    [[nodiscard]] static Result<std::unique_ptr<Logging>> init(AppKind kind,
                                                               const Options& options);

    Logging(Passkey key, std::unique_ptr<Sink> sink);
    ~Logging();
    Logging(const Logging&) = delete;
    Logging& operator=(const Logging&) = delete;
    Logging(Logging&&) = delete;
    Logging& operator=(Logging&&) = delete;

    // Path of the file currently written, empty when logging to stderr only.
    [[nodiscard]] std::filesystem::path currentFile() const;

private:
    std::unique_ptr<Sink> m_sink;
};

} // namespace hio::log
