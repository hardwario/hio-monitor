#include "workers/diag_worker.h"

#include "gui_log.h"
#include "hio/core/crash_handler.h"
#include "hio/core/error.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"
#include "hio/diag/check.h"
#include "hio/diag/environment.h"
#include "hio/diag/report.h"

#include <QDateTime>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QString>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace hio::gui {

namespace {

template <class... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

// Keeps the checks in `ids` and, transitively, the checks they depend on (Check::dependsOn), in
// the run order of `list` (review V3: a subset without its dependencies would only be Skipped).
void keepWithDependencies(std::vector<diag::Check>& list, const std::vector<std::string>& ids) {
    std::vector<std::string> wanted(ids);
    // Bounded: each pass adds at least one id of `list`, so at most list.size() passes.
    for (std::size_t pass = 0; pass <= list.size(); ++pass) {
        const std::size_t before = wanted.size();
        for (const diag::Check& check : list) {
            if (std::ranges::find(wanted, check.id) == wanted.end()) {
                continue;
            }
            for (const std::string& dependency : check.dependsOn) {
                if (std::ranges::find(wanted, dependency) == wanted.end()) {
                    wanted.push_back(dependency);
                }
            }
        }
        if (wanted.size() == before) {
            break;
        }
    }
    std::erase_if(list, [&wanted](const diag::Check& check) {
        return std::ranges::find(wanted, check.id) == wanted.end();
    });
}

} // namespace

DiagWorker::DiagWorker(QObject* parent)
    : QObject(parent), m_worker([this](const std::stop_token& stop) { run(stop); }) {}

DiagWorker::~DiagWorker() {
    m_worker.request_stop();
    {
        const std::scoped_lock lock(m_mutex);
        if (m_runningStop) {
            m_runningStop->request_stop();
        }
    }
    m_wake.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

std::uint64_t DiagWorker::runChecks(ChecksRequest request) { return enqueue(std::move(request)); }

std::uint64_t DiagWorker::collectReport(CollectRequest request) {
    return enqueue(std::move(request));
}

std::uint64_t DiagWorker::saveReport(SaveRequest request) { return enqueue(std::move(request)); }

std::uint64_t
DiagWorker::enqueue(std::variant<ChecksRequest, CollectRequest, SaveRequest> payload) {
    std::uint64_t generation = 0;
    {
        const std::scoped_lock lock(m_mutex);
        generation = m_nextGeneration++;
        Request request{.generation = generation, .payload = std::move(payload)};
        // The newest request replaces a pending one of the same kind.
        const auto same = std::ranges::find_if(
            m_queue, [&request](const Request& queued) { return queued.kind() == request.kind(); });
        if (same != m_queue.end()) {
            *same = std::move(request);
        } else {
            if (m_queue.size() >= maxQueued) {
                qCWarning(lcGui) << "Diagnostics queue full: request" << m_queue.front().generation
                                 << "dropped";
                m_queue.pop_front();
            }
            m_queue.push_back(std::move(request));
        }
    }
    m_wake.notify_all();
    return generation;
}

void DiagWorker::cancel(std::uint64_t generation) {
    const std::scoped_lock lock(m_mutex);
    std::erase_if(
        m_queue, [generation](const Request& request) { return request.generation == generation; });
    if (m_running == generation && m_runningStop) {
        m_runningStop->request_stop();
    }
}

void DiagWorker::run(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        Request request;
        std::stop_source requestStop;
        {
            std::unique_lock lock(m_mutex);
            if (!m_wake.wait(lock, stop, [this] { return !m_queue.empty(); })) {
                break;
            }
            request = std::move(m_queue.front());
            m_queue.pop_front();
            m_running = request.generation;
            m_runningStop = requestStop;
        }
        // The request stops with the thread too.
        const std::stop_callback fromThread(stop, [&requestStop] { requestStop.request_stop(); });
        process(request, requestStop.get_token());
        const std::scoped_lock lock(m_mutex);
        m_running = 0;
        m_runningStop.reset();
    }
}

void DiagWorker::process(Request& request, const std::stop_token& stop) {
    const std::uint64_t generation = request.generation;
    const auto progress = [this, generation](const diag::CheckResult& result) {
        post(Progress{.generation = generation, .result = result});
    };
    std::visit(
        Overloaded{
            [&](ChecksRequest& checks) {
                std::vector<diag::Check> list = diag::environmentChecks(checks.context);
                if (!checks.ids.empty()) {
                    keepWithDependencies(list, checks.ids);
                }
                post(ChecksDone{.generation = generation,
                                .results = diag::CheckRunner::run(list, stop, progress)});
            },
            [&](CollectRequest& collect) {
                collect.context.facts = std::make_shared<diag::EnvironmentFacts>();
                auto input = std::make_shared<diag::ReportInput>();
                input->doctor = diag::CheckRunner::run(diag::environmentChecks(collect.context),
                                                       stop, progress);
                input->app = diag::appInfoJson(collect.language.toStdString());
                input->tools = diag::toJson(*collect.context.facts);
                input->probes = input->tools.value(QStringLiteral("probes")).toArray();
                input->tabs = collect.tabs;
                input->logFiles = diag::recentLogFiles(log::defaultLogDirectory());
                input->crashFiles = diag::recentCrashFiles(crash::defaultDirectory());
                input->settingsFile = Settings::defaultFilePath();
                input->createdUtc =
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
                input->redaction = diag::currentRedaction();
                const QString summary =
                    QString::fromStdString(diag::ReportBuilder::summaryText(*input));
                post(Collected{.generation = generation, .input = input, .summary = summary});
            },
            [&](SaveRequest& save) {
                Result<std::size_t> bytes =
                    save.input ? diag::ReportBuilder::writeZip(*save.input, save.target, stop)
                               : Result<std::size_t>(std::unexpected(
                                     makeError(ErrorCode::InternalUnexpected, "no report")));
                post(Saved{.generation = generation, .bytes = std::move(bytes)});
            },
        },
        request.payload);
}

void DiagWorker::post(Outcome outcome) {
    {
        const std::scoped_lock lock(m_mutex);
        if (m_results.size() >= maxResults) {
            // Drop the oldest progress event; final results are never dropped.
            const auto oldProgress = std::ranges::find_if(m_results, [](const Outcome& item) {
                return std::holds_alternative<Progress>(item);
            });
            if (oldProgress != m_results.end()) {
                m_results.erase(oldProgress);
            } else if (std::holds_alternative<Progress>(outcome)) {
                return;
            }
        }
        m_results.push_back(std::move(outcome));
    }
    if (!m_deliverPending.exchange(true)) {
        // By name: Qt creates and consumes the queued call (see DeviceSession::wakeUp).
        QMetaObject::invokeMethod(this, "deliver", Qt::QueuedConnection);
    }
}

void DiagWorker::deliver() {
    m_deliverPending.store(false);
    std::deque<Outcome> results;
    {
        const std::scoped_lock lock(m_mutex);
        results.swap(m_results);
    }
    for (Outcome& outcome : results) {
        std::visit(
            Overloaded{
                [this](Progress& item) { emit checkProgress(item.generation, item.result); },
                [this](ChecksDone& item) { emit checksFinished(item.generation, item.results); },
                [this](Collected& item) {
                    emit reportCollected(item.generation, item.input, item.summary);
                },
                [this](Saved& item) { emit reportSaved(item.generation, item.bytes); },
            },
            outcome);
    }
}

} // namespace hio::gui
