#include "hio/diag/check.h"

#include "hio/core/error.h"
#include "hio/core/log.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QString>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace hio::diag {

namespace {

using SteadyClock = std::chrono::steady_clock;

// Stops the current check's token at its deadline. Owned by one CheckRunner::run() call; the
// thread is joined by the destructor (std::jthread).
class Watchdog {
public:
    Watchdog() : m_thread([this](const std::stop_token& stop) { loop(stop); }) {}
    ~Watchdog() = default; // jthread: request_stop + join; loop() wakes on the stop token
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    Watchdog(Watchdog&&) = delete;
    Watchdog& operator=(Watchdog&&) = delete;

    // Arms the watchdog for one check.
    void arm(std::stop_source source, SteadyClock::time_point deadline) {
        {
            const std::scoped_lock lock(m_mutex);
            m_source = std::move(source);
            m_deadline = deadline;
        }
        m_changed.notify_all();
    }

    void disarm() {
        {
            const std::scoped_lock lock(m_mutex);
            m_source.reset();
            m_deadline.reset();
        }
        m_changed.notify_all();
    }

private:
    void loop(const std::stop_token& stop) {
        std::unique_lock lock(m_mutex);
        while (!stop.stop_requested()) {
            if (!m_deadline) {
                m_changed.wait(lock, stop, [this] { return m_deadline.has_value(); });
                continue;
            }
            const SteadyClock::time_point deadline = *m_deadline;
            if (m_changed.wait_until(lock, stop, deadline,
                                     [this, deadline] { return m_deadline != deadline; })) {
                continue; // re-armed or disarmed
            }
            if (stop.stop_requested()) {
                break;
            }
            if (m_source) {
                m_source->request_stop();
            }
            m_deadline.reset();
        }
    }

    std::mutex m_mutex;
    std::condition_variable_any m_changed;
    std::optional<std::stop_source> m_source;          // guarded by m_mutex
    std::optional<SteadyClock::time_point> m_deadline; // guarded by m_mutex
    std::jthread m_thread; // declared last: started after the members it uses
};

const CheckResult* findResult(const std::vector<CheckResult>& results, const std::string& id) {
    const auto it = std::ranges::find(results, id, &CheckResult::id);
    return it == results.end() ? nullptr : &*it;
}

} // namespace

std::string_view checkStatusSymbol(CheckStatus status) noexcept {
    switch (status) {
    case CheckStatus::Pending:
        return "\u25CB";
    case CheckStatus::Running:
        return "\u2026";
    case CheckStatus::Ok:
        return "\u2713";
    case CheckStatus::Warning:
        return "!";
    case CheckStatus::Failed:
        return "\u2717";
    case CheckStatus::Skipped:
        return "\u2013";
    }
    return "?";
}

std::string formatCheckLines(std::span<const CheckResult> results, std::size_t titleWidth) {
    if (titleWidth == 0) {
        for (const CheckResult& result : results) {
            titleWidth = std::max(
                titleWidth, static_cast<std::size_t>(QString::fromStdString(result.title).size()));
        }
    }
    std::string text;
    for (const CheckResult& result : results) {
        const qsizetype length = QString::fromStdString(result.title).size();
        const std::size_t pad = std::cmp_greater(titleWidth, length)
                                    ? titleWidth - static_cast<std::size_t>(length)
                                    : 0;
        text += std::format("{} {}{}  {}\n", checkStatusSymbol(result.status), result.title,
                            std::string(pad, ' '), result.result);
    }
    return text;
}

std::string checkTitle(const char* titleSourceText) {
    return QCoreApplication::translate("hio::diag", titleSourceText).toStdString();
}

CheckResult okResult(const Check& check, std::string result) {
    return CheckResult{.id = check.id,
                       .status = CheckStatus::Ok,
                       .title = checkTitle(check.titleSourceText),
                       .result = std::move(result),
                       .error = std::nullopt};
}

CheckResult skippedResult(const Check& check, std::string result) {
    return CheckResult{.id = check.id,
                       .status = CheckStatus::Skipped,
                       .title = checkTitle(check.titleSourceText),
                       .result = std::move(result),
                       .error = std::nullopt};
}

CheckResult problemResult(const Check& check, CheckStatus status, Error error, std::string result) {
    if (result.empty()) {
        result = error.message;
    }
    return CheckResult{.id = check.id,
                       .status = status,
                       .title = checkTitle(check.titleSourceText),
                       .result = std::move(result),
                       .error = std::move(error)};
}

std::vector<CheckResult> CheckRunner::run(std::span<const Check> checks,
                                          const std::stop_token& stop, const Callback& callback) {
    const auto notify = [&callback](const CheckResult& result) {
        if (callback) {
            callback(result);
        }
    };
    std::vector<CheckResult> results;
    results.reserve(std::min(checks.size(), maxChecks));
    Watchdog watchdog;
    for (std::size_t index = 0; index < checks.size(); ++index) {
        const Check& check = checks[index];
        if (index >= maxChecks) {
            CheckResult skipped = problemResult(
                check, CheckStatus::Skipped,
                makeError(ErrorCode::InternalUnexpected, "more than 16 checks in one run"));
            notify(skipped);
            results.push_back(std::move(skipped));
            continue;
        }
        if (stop.stop_requested()) {
            CheckResult skipped = skippedResult(
                check, QCoreApplication::translate("hio::diag", "Cancelled").toStdString());
            notify(skipped);
            results.push_back(std::move(skipped));
            continue;
        }
        const bool dependenciesPassed =
            std::ranges::all_of(check.dependsOn, [&results](const std::string& id) {
                const CheckResult* dependency = findResult(results, id);
                return dependency != nullptr && isPassed(dependency->status);
            });
        if (!dependenciesPassed || !check.run) {
            CheckResult skipped = skippedResult(
                check,
                QCoreApplication::translate("hio::diag", "Skipped: an earlier check did not pass")
                    .toStdString());
            notify(skipped);
            results.push_back(std::move(skipped));
            continue;
        }
        CheckResult running{.id = check.id,
                            .status = CheckStatus::Running,
                            .title = checkTitle(check.titleSourceText),
                            .result = {},
                            .error = std::nullopt};
        notify(running);

        const auto timeout = std::min(check.timeout, maxCheckTimeout);
        std::stop_source checkStop;
        const std::stop_callback fromRun(stop, [&checkStop] { checkStop.request_stop(); });
        const SteadyClock::time_point started = SteadyClock::now();
        watchdog.arm(checkStop, started + timeout);
        CheckResult result = check.run(checkStop.get_token());
        watchdog.disarm();
        const auto elapsed = SteadyClock::now() - started;
        result.id = check.id;
        if (result.title.empty()) {
            result.title = checkTitle(check.titleSourceText);
        }
        if (elapsed >= timeout && !stop.stop_requested() && result.status != CheckStatus::Failed) {
            Error error = makeError(
                ErrorCode::DiagCheckTimeout,
                std::format("{}: {} ms > {} ms", check.id,
                            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                            timeout.count()));
            error.message =
                QCoreApplication::translate("hio::diag", "The check \"%1\" did not finish in time.")
                    .arg(QString::fromStdString(result.title))
                    .toStdString();
            result = problemResult(check, CheckStatus::Warning, std::move(error));
        } else if (stop.stop_requested() && result.status != CheckStatus::Ok &&
                   result.status != CheckStatus::Warning && result.status != CheckStatus::Failed) {
            result = skippedResult(
                check, QCoreApplication::translate("hio::diag", "Cancelled").toStdString());
        }
        if (result.status == CheckStatus::Pending || result.status == CheckStatus::Running) {
            result = problemResult(
                check, CheckStatus::Failed,
                makeError(ErrorCode::InternalUnexpected, "the check returned no result"));
        }
        qCInfo(log::lcDiag).noquote()
            << "Check" << QString::fromStdString(check.id)
            << QString::fromUtf8(checkStatusId(result.status).data(),
                                 static_cast<qsizetype>(checkStatusId(result.status).size()))
            << QString::fromStdString(result.result);
        notify(result);
        results.push_back(std::move(result));
    }
    return results;
}

} // namespace hio::diag
