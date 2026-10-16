#pragma once

// Checks and their results (plan/features/0007-m4-wizard-framework-diagnostics.md, ADR-0068).
//
// A Check is a pure function of its context that returns one CheckResult. CheckRunner runs a list
// of checks in order on the caller's thread, honours each check's `dependsOn` (a dependency that
// is not Ok or Warning skips the check), a per-check timeout and a stop token, and reports every
// result through a callback (Running before a check, the result after it).
//
// This header is the interface target hio::diag_types: libs/device uses CheckStatus for its
// DiagnoseStep (ADR-0069) without depending on the rest of libs/diag.
//
// Bounds: at most maxChecks checks per run; each timeout is at most maxCheckTimeout.

#include "hio/core/error.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace hio::diag {

enum class CheckStatus : std::uint8_t { Pending, Running, Ok, Warning, Failed, Skipped };

// "pending", "running", "ok", "warning", "failed", "skipped" (JSON, ADR-0070).
[[nodiscard]] constexpr std::string_view checkStatusId(CheckStatus status) noexcept {
    switch (status) {
    case CheckStatus::Pending:
        return "pending";
    case CheckStatus::Running:
        return "running";
    case CheckStatus::Ok:
        return "ok";
    case CheckStatus::Warning:
        return "warning";
    case CheckStatus::Failed:
        return "failed";
    case CheckStatus::Skipped:
        return "skipped";
    }
    return "failed";
}

// Ok or Warning: a check that depends on this one may run.
[[nodiscard]] constexpr bool isPassed(CheckStatus status) noexcept {
    return status == CheckStatus::Ok || status == CheckStatus::Warning;
}

struct CheckResult {
    std::string id; // kebab-case, stable (JSON, tests)
    CheckStatus status = CheckStatus::Pending;
    std::string title;               // translated at creation (ADR-0027)
    std::string result;              // short translated result text
    std::optional<hio::Error> error; // Warning / Failed only
};

// Bounds (plan/features/0007 § Bounds).
inline constexpr std::size_t maxChecks = 16;
inline constexpr std::chrono::milliseconds maxCheckTimeout{15000};

struct Check {
    std::string id;
    // English source text in context "hio::diag" (QT_TRANSLATE_NOOP), translated by the runner.
    const char* titleSourceText = "";
    std::chrono::milliseconds timeout{5000}; // per check, <= maxCheckTimeout
    // Ids of earlier checks that must be Ok or Warning; otherwise this check is Skipped. (The
    // plan's sketch calls it `requires`, which is a C++20 keyword.)
    std::vector<std::string> dependsOn;
    // Runs the check. The token is stopped at the timeout and on the run's stop request; the
    // check returns soon after (a blocking vendor call cannot be interrupted). Must not throw.
    std::function<CheckResult(std::stop_token)> run;
};

// Runs checks on the caller's thread (implemented in hio::diag, not in hio::diag_types). A watchdog
// thread owned by run() (std::jthread, joined before run() returns) stops a check's token at its
// timeout; a check that ran over its timeout and did not fail is reported as Warning
// diag.check_timeout. After a stop request the remaining checks are Skipped. More than maxChecks
// checks: the rest are Skipped with internal.unexpected (a programming error).
class CheckRunner {
public:
    using Callback = std::function<void(const CheckResult&)>; // Running before, result after
    [[nodiscard]] static std::vector<CheckResult>
    run(std::span<const Check> checks, const std::stop_token& stop, const Callback& callback = {});
};

// A result with the check's translated title (helpers for check implementations).
[[nodiscard]] CheckResult okResult(const Check& check, std::string result);
[[nodiscard]] CheckResult skippedResult(const Check& check, std::string result);
// Warning / Failed with the error; `result` defaults to the error's message.
[[nodiscard]] CheckResult problemResult(const Check& check, CheckStatus status, hio::Error error,
                                        std::string result = {});
// "✓" ok, "!" warning, "✗" failed, "–" skipped, "…" running, "○" pending (UTF-8).
[[nodiscard]] std::string_view checkStatusSymbol(CheckStatus status) noexcept;
// One aligned line per result: "<symbol> <title padded to `titleWidth`>  <result>" (UTF-8; the
// width counts characters, not bytes). titleWidth 0 = the longest title of `results`.
[[nodiscard]] std::string formatCheckLines(std::span<const CheckResult> results,
                                           std::size_t titleWidth = 0);
// The translated title of a check's source text (context "hio::diag").
[[nodiscard]] std::string checkTitle(const char* titleSourceText);

} // namespace hio::diag
