#pragma once

// Local crash dumps (ADR-0123, plan/features/0015 B1). Nothing is ever uploaded.
//
// install() is called first thing in main() of the GUI and the CLI. It writes into `directory`
// (<AppLocalDataLocation>/crashes):
//   - Windows: an unhandled SEH exception -> crash-<app>-<UTC>.dmp (MiniDumpWriteDump,
//     MiniDumpNormal | MiniDumpWithThreadInfo);
//   - Linux / macOS: SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT -> crash-<app>-<UTC>.txt with the
//     signal, the fault address, the application name and version and the raw backtrace; the
//     handler is async-signal-safe (pre-opened directory, no allocation, write() only), runs on an
//     alternate signal stack (the installing thread's), then restores the default action and
//     re-raises the signal (the OS crash reporter / core dump still runs).
// Both also create the marker file `last-crash` (crashedLastRun(), acknowledge()).
// <UTC> is YYYYMMDD-HHMMSS.
//
// Bounds: at most maxCrashFiles crash files are kept (pruneOld(), the oldest are deleted; install()
// prunes too); a text file holds at most maxTextBytes (backtrace cut), a minidump larger than
// maxDumpBytes is deleted by pruneOld().
//
// install() does nothing (and succeeds) in sanitizer builds (HIO_SANITIZER: the sanitizers have
// their own handlers) and when the environment variable HIO_NO_CRASH_HANDLER=1 is set.
// Thread safety: install() once from the main thread before other threads start; the other
// functions are plain file operations.

#include "hio/core/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace hio::crash {

inline constexpr std::size_t maxCrashFiles = 5;
inline constexpr std::size_t reportCrashFiles = 3; // newest files in the diagnostic report
inline constexpr std::uintmax_t maxTextBytes = 64ULL * 1024;
inline constexpr std::uintmax_t maxDumpBytes = 8ULL * 1024 * 1024;

struct Config {
    std::filesystem::path directory; // <AppLocalDataLocation>/crashes
    std::string_view appName;        // "hio-monitor" | "hio"
    std::string_view version;        // HIO_VERSION + git hash
};

// The default directory: <AppLocalDataLocation>/crashes (QStandardPaths; the application identity
// must be set, hio::initApplicationIdentity()).
[[nodiscard]] std::filesystem::path defaultDirectory();

// Errors: io.write_failed (the directory cannot be created or opened), usage.invalid_argument
// (installed twice, an empty or too long name / version).
[[nodiscard]] Result<void> install(const Config& config);

// False when install() was skipped (sanitizer build, HIO_NO_CRASH_HANDLER=1) or not called.
[[nodiscard]] bool isInstalled() noexcept;

// Deletes all but the newest maxCrashFiles crash files and files above their size bound.
void pruneOld(const std::filesystem::path& directory);

// The newest crash files (crash-*.txt / crash-*.dmp), newest first, at most `max`.
[[nodiscard]] std::vector<std::filesystem::path> recent(const std::filesystem::path& directory,
                                                        std::size_t max);

// The marker file written by the handler exists.
[[nodiscard]] bool crashedLastRun(const std::filesystem::path& directory);

// Removes the marker (the notice was shown or dismissed).
void acknowledge(const std::filesystem::path& directory);

// The marker file name (`last-crash`) and the crash file pattern, for tests and the report.
inline constexpr std::string_view markerFileName = "last-crash";
[[nodiscard]] bool isCrashFileName(std::string_view name) noexcept;

} // namespace hio::crash
