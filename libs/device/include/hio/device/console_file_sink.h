#pragma once

// ConsoleFileSink - the console log file of a session (plan/features/0002-m1-jlink-rtt.md), in
// rttt's line format:
//     2026-10-09 12:34:56.789 > text     shell output
//     2026-10-09 12:34:56.789 # text     log line
//     2026-10-09 12:34:56.789 < text     command sent
//     2026-10-09 12:34:56.789 @ text     connection change
// (local time), with a header block naming the probe when the file is opened.
//
// Default mode: one file per session (in ConsoleFileConfig::consoleFileDir when set, ADR-0156),
// <directory>/<profile>-<target>-YYYYMMDD-HHMMSS-<sn>.log
// for a J-Link session, <directory>/<profile>-ble-YYYYMMDD-HHMMSS-<sanitised id>.log for a BLE
// session (ADR-0128; sanitisedBleId()) (UTC); at maxDefaultFileBytes the session continues in
// <name>.part2.log, .part3.log, ...; at open and on every rotation the console files (*.log) in the
// directory are pruned to the newest maxDefaultFiles files and maxDefaultTotalBytes in total (by
// age; deletion failures are ignored). A file that a live session is writing is never deleted
// (review R45, R48): in this process the files a sink has open (any session); for a probe whose
// ProbeLock (ADR-0058) or a Bluetooth device whose BleLock (ADR-0083) has a live owner in another
// process, only the newest file of that session key (parsed from the default file name), and only
// when it was written since that owner took the lock. Closed .partN files of a live session are
// pruned like any other file. Live files count towards the bounds, which may therefore be exceeded
// by at most one file per live session. Explicit file (--console-file P): appended to; when it
// exceeds maxExplicitFileBytes it is renamed to P.1 (replacing an older P.1) and a new P is
// started. When that rename fails, the file is not rotated again in this session (one warning,
// takeWarning()) and keeps being appended to, up to maxExplicitFileBytesAfterFailedRotation; beyond
// that flush() fails (io.write_failed) and the session stops writing the file (review R26).
//
// Writes are buffered (at most maxBufferedBytes) and written by flush(), which the session worker
// calls every cycle (BLE: the BLE thread every <= 250 ms, ADR-0128). Thread safety: none; owned by
// the session worker (J-Link) or the BLE thread (BLE).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/session_key.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

class QFile;

namespace hio::device {

struct ConsoleFileConfig {
    // nullopt = default mode in `directory`; a path = explicit file (appended).
    std::optional<std::filesystem::path> explicitFile;
    std::filesystem::path directory; // default mode
    std::string profileId;
    std::string targetId; // J-Link file names; BLE names use "ble"
    // The probe and BLE locks consulted by the retention (review R45); empty = the default.
    std::filesystem::path lockDirectory;
    // ADR-0156 (M6, `hio mcp --console-file-dir`): default mode in this directory instead of
    // `directory`, with the same generated file name. The retention bounds apply there too, but
    // only to files with a generated session file name, so other files of a user's directory are
    // never deleted. Ignored for an explicit file.
    std::optional<std::filesystem::path> consoleFileDir = std::nullopt;
};

class ConsoleFileSink {
    struct Passkey {
        explicit Passkey() = default;
    };

public:
    static constexpr std::uintmax_t maxDefaultFileBytes = 10ULL * 1024 * 1024;
    static constexpr std::size_t maxDefaultFiles = 50;
    static constexpr std::uintmax_t maxDefaultTotalBytes = 200ULL * 1024 * 1024;
    static constexpr std::uintmax_t maxExplicitFileBytes = 100ULL * 1024 * 1024;
    static constexpr std::uintmax_t maxExplicitFileBytesAfterFailedRotation =
        2 * maxExplicitFileBytes;
    static constexpr std::size_t maxBufferedBytes = 256ULL * 1024;

    // AppLocalDataLocation/console.
    [[nodiscard]] static std::filesystem::path defaultDirectory();

    // Opens the file and writes the header block. Errors: io.write_failed.
    [[nodiscard]] static Result<std::unique_ptr<ConsoleFileSink>>
    open(const ConsoleFileConfig& config, const SessionKey& key, std::string_view headerText,
         std::chrono::system_clock::time_point now);
    // The J-Link form (M1 call sites): open(config, ProbeKey{probeSerialNumber}, ...).
    [[nodiscard]] static Result<std::unique_ptr<ConsoleFileSink>>
    open(const ConsoleFileConfig& config, std::uint32_t probeSerialNumber,
         std::string_view headerText, std::chrono::system_clock::time_point now);

    ConsoleFileSink(Passkey key, ConsoleFileConfig config, std::filesystem::path baseFile);
    ~ConsoleFileSink();
    ConsoleFileSink(const ConsoleFileSink&) = delete;
    ConsoleFileSink& operator=(const ConsoleFileSink&) = delete;
    ConsoleFileSink(ConsoleFileSink&&) = delete;
    ConsoleFileSink& operator=(ConsoleFileSink&&) = delete;

    // Buffers one line (flushes when the buffer bound is reached). Errors: io.write_failed.
    [[nodiscard]] Status write(const ConsoleLine& line);
    [[nodiscard]] Status writeConnection(std::string_view text,
                                         std::chrono::system_clock::time_point time);
    // Writes the buffer to the file (rotating first if needed). Errors: io.write_failed.
    [[nodiscard]] Status flush();

    [[nodiscard]] const std::filesystem::path& currentFile() const noexcept { return m_current; }
    // A problem that did not stop the file (a failed rotation, io.write_failed); returned once.
    [[nodiscard]] std::optional<Error> takeWarning();

private:
    [[nodiscard]] Status openCurrent();
    [[nodiscard]] Status append(std::string_view prefix, std::string_view text,
                                std::chrono::system_clock::time_point time);
    [[nodiscard]] Status rotateIfNeeded(std::size_t incoming);
    void closeFile();
    void prune() const;

    ConsoleFileConfig m_config;
    std::filesystem::path m_base;    // default mode: <name>.log; explicit: P
    std::filesystem::path m_current; // the file being written
    std::unique_ptr<QFile> m_file;
    std::uintmax_t m_written = 0; // bytes in m_current
    int m_part = 1;
    bool m_rotationFailed = false; // explicit file: rename to P.1 failed, not retried
    std::optional<Error> m_warning;
    std::string m_buffer; // <= maxBufferedBytes (+ one line)
};

// Prefix of a line in the console file (" > ", " # ", " < ", " @ ").
[[nodiscard]] std::string_view consoleFilePrefix(const ConsoleLine& line) noexcept;

} // namespace hio::device
