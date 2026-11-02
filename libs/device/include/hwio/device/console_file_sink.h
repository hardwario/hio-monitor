#pragma once

// ConsoleFileSink - the console log file of a session (plan/features/0002-m1-jlink-rtt.md), in
// rttt's line format:
//     2026-10-09 12:34:56.789 > text     shell output
//     2026-10-09 12:34:56.789 # text     log line
//     2026-10-09 12:34:56.789 < text     command sent
//     2026-10-09 12:34:56.789 @ text     connection change
// (local time), with a header block naming the probe when the file is opened. ADR-0186: a command
// with secret arguments (TargetProfile::sensitiveCommandPrefixes of the config's profile and
// target) is written as "< <prefix> <redacted>", and so is a shell's echo of it in the output
// (after the profile's shell prompt or a Zephyr "<name>:~$ " prompt); plan 0028 (U2): a shell
// output line "<key>: <value>" whose key is one of the profile's sensitiveOutputKeys (CHESTER
// `info show`: claim token, BLE passkey) is written as "<key>: <redacted>"; the files are
// owner-only (0600 on Unix, created so).
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
// Plan 0028 (ADR-0200): adoptIdentity() names a default-mode file after the device firmware once
// it is known: <directory>/<consoleFileBase(profile, target|"ble", identity)>-<stamp>-<sn|id>.log
// (e.g. chester-app-clime-v3.0.0-20261102-101500-683012345.log); the first identity renames every
// part of the session written so far (D4, D5; same stamp and key, a "-2" ... "-9" suffix before
// ".log" when the name exists already; a part that cannot be renamed keeps its name, D11) and
// writes the marker "@ Firmware: <name> <version>" (D6). A different identity later continues in a
// new file with a new stamp (D3; the marker lines name the two files): another firmware (name or
// version), or another device (both device serials known and different, review round 1 R1; an
// empty serial keeps the stored one). A collision suffix is free for every part (R3); a part that
// is already gone (pruned) is skipped. While renaming, the old and the new path stay in the
// open-files list, so another sink of this process never prunes the live file (R2). An explicit
// file is never renamed.
//
// Writes are buffered (at most maxBufferedBytes) and written by flush(), which the session worker
// calls every cycle (BLE: the BLE thread every <= 250 ms, ADR-0128). Thread safety: none; owned by
// the session worker (J-Link) or the BLE thread (BLE).

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/firmware_identity.h"
#include "hwio/device/session_key.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

class QFile;

namespace hwio::device {

struct ConsoleFileConfig {
    // nullopt = default mode in `directory`; a path = explicit file (appended).
    std::optional<std::filesystem::path> explicitFile;
    std::filesystem::path directory; // default mode
    std::string profileId;
    std::string targetId; // J-Link file names; BLE names use "ble"
    // The probe and BLE locks consulted by the retention (review R45); empty = the default.
    std::filesystem::path lockDirectory;
    // ADR-0156 (M6, `hwio mcp --console-file-dir`): default mode in this directory instead of
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

    // Plan 0028 (ADR-0200, D3-D6, D11): see the file comment. nullopt, the identity the file has
    // already and an explicit file: no-op. Errors: io.write_failed (the reopened file).
    [[nodiscard]] Status adoptIdentity(const std::optional<FirmwareIdentity>& identity,
                                       std::chrono::system_clock::time_point now);
    [[nodiscard]] const std::optional<FirmwareIdentity>& identity() const noexcept {
        return m_identity;
    }

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
    // The generated file name for `identity` with m_stamp and m_keySuffix, free for `parts` parts
    // (with a "-2" ... "-9" suffix when taken); nullopt when all are taken.
    [[nodiscard]] std::optional<std::filesystem::path>
    generatedName(const std::optional<FirmwareIdentity>& identity, int parts) const;
    [[nodiscard]] Status renameForIdentity(const FirmwareIdentity& identity,
                                           std::chrono::system_clock::time_point now);
    [[nodiscard]] Status continueInNewFile(const FirmwareIdentity& identity,
                                           std::chrono::system_clock::time_point now,
                                           bool deviceChanged);

    ConsoleFileConfig m_config;
    std::filesystem::path m_base;    // default mode: <name>.log; explicit: P
    std::filesystem::path m_current; // the file being written
    std::unique_ptr<QFile> m_file;
    std::uintmax_t m_written = 0; // bytes in m_current
    int m_part = 1;
    bool m_rotationFailed = false; // explicit file: rename to P.1 failed, not retried
    std::optional<Error> m_warning;
    std::string m_buffer; // <= maxBufferedBytes (+ one line)
    // Plan 0028: the parts of a generated name and the identity the name carries.
    std::string m_headerText;
    std::string m_nameTarget; // the target id, "ble" for a BLE session
    std::string m_stamp;      // yyyyMMdd-HHmmss (UTC)
    std::string m_keySuffix;  // the probe SN or the sanitised BLE id
    std::optional<FirmwareIdentity> m_identity;
    bool m_renameFailureLogged = false;
    // The length of a shell prompt at the start of an echoed line (0 = none).
    [[nodiscard]] std::size_t echoPromptLength(std::string_view text) const noexcept;

    // ADR-0186: input lines matching these are written redacted (the target's profile list).
    std::span<const std::string_view> m_sensitivePrefixes;
    // Plan 0028 (U2): output lines "<key>: <secret>" are written as "<key>: <redacted>".
    std::span<const std::string_view> m_sensitiveOutput;
    // Review R4: the profile's shell prompts (RTT target, BLE); "" = none.
    std::array<std::string_view, 2> m_prompts{};
};

// The session key of a generated console file name (old and plan 0028 names, with .partN and the
// "-2" ... "-9" collision suffix); nullopt for other names. Used by the retention (review R45).
[[nodiscard]] std::optional<SessionKey> consoleFileSessionKey(const std::filesystem::path& file);

// Prefix of a line in the console file (" > ", " # ", " < ", " @ ").
[[nodiscard]] std::string_view consoleFilePrefix(const ConsoleLine& line) noexcept;

} // namespace hwio::device
