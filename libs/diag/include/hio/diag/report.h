#pragma once

// The diagnostic report (ADR-0075, plan/features/0007): a zip with summary.txt, summary.json, the
// recent application logs (logs/) and the settings file (settings.ini). Every text is redacted:
// the home directory -> "~", the user name -> "<user>", the host name -> "<host>" (whole words;
// case-insensitive on Windows). Probe serial numbers and firmware ids are kept. Nothing is sent
// anywhere; the user attaches the file to a support request.
//
// Bounds: each log file contributes its last maxLogBytes (cut at the next line start), at most
// maxLogFiles files, the summary at most maxSummaryBytes, the whole zip at most maxReportBytes
// (logs are left out, newest kept, when it would grow beyond).

#include "hio/core/result.h"
#include "hio/diag/check.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cstddef>
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace hio::diag {

inline constexpr std::size_t maxLogBytes = 2ULL * 1024 * 1024;
inline constexpr std::size_t maxLogFiles = 4;
inline constexpr std::size_t maxSummaryBytes = 256ULL * 1024;
inline constexpr std::size_t maxReportBytes = 16ULL * 1024 * 1024;

struct RedactionContext {
    std::string home;             // e.g. "/home/jane" or "C:\Users\Jane"
    std::string user;             // "jane"
    std::string host;             // "jane-laptop"
    bool caseInsensitive = false; // Windows
};

// The context of this process (QDir::homePath, the user name from the environment, the host name).
[[nodiscard]] RedactionContext currentRedaction();

// Replaces the home path (also with the other separator on Windows) by "~" where it is the folder
// itself (followed by a separator, another non-name character or the end; not by a letter, digit,
// '_', '-' or '.': "/home/jane_old" is another folder), then the user name and the host name by
// "<user>" / "<host>" where they stand as whole words (not inside a longer word: letters, digits,
// '_' and '-' around them keep the text unchanged). The short host name (before the first '.')
// is replaced too when it has at least 3 characters. Empty or one-character names are not
// replaced.
[[nodiscard]] std::string redact(std::string_view text, const RedactionContext& context);

struct ReportInput {
    std::vector<CheckResult> doctor;
    QJsonObject app;   // version, gitHash, qt, compiler, os, arch, language
    QJsonObject tools; // EnvironmentFacts (J-Link Software, nRF Util)
    QJsonArray probes; // EnvironmentFacts probes
    QJsonArray tabs;   // GUI only: profile, target, serial number, state
    std::vector<std::filesystem::path> logFiles; // current + previous of the GUI and the CLI
    std::filesystem::path settingsFile;          // "" or missing = left out
    std::string createdUtc;                      // ISO 8601 report time
    RedactionContext redaction;
};

// The application part of ReportInput::app for this process (build info, OS, architecture).
[[nodiscard]] QJsonObject appInfoJson(std::string_view uiLanguage);

// The newest current + previous log files of the GUI and the CLI in `logDir` (at most
// maxLogFiles; part files of one run count as the run's file).
[[nodiscard]] std::vector<std::filesystem::path>
recentLogFiles(const std::filesystem::path& logDir);

// The default file name "hio-diagnostics-YYYYMMDD-HHMMSS.zip" (local time).
[[nodiscard]] std::string defaultReportFileName();

// The last `maxBytes` of `data`, starting at a line start: the window itself when the byte before
// it is a newline, otherwise the text after the first newline in the window (the whole window when
// it has no newline).
[[nodiscard]] std::string_view tailAtLineStart(std::string_view data, std::size_t maxBytes);

class ReportBuilder {
public:
    [[nodiscard]] static std::string summaryText(const ReportInput& input); // redacted
    [[nodiscard]] static QJsonObject summaryJson(const ReportInput& input); // redacted
    // Writes the zip to `target` through a temporary file in the same folder, renamed at the end:
    // a failed or cancelled write leaves no partial file. Returns the size in bytes.
    // Errors: diag.report_write_failed (path in detail), cancelled.
    [[nodiscard]] static Result<std::size_t> writeZip(const ReportInput& input,
                                                      const std::filesystem::path& target,
                                                      const std::stop_token& stop);
};

} // namespace hio::diag
