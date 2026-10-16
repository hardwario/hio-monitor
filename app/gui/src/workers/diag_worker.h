#pragma once

// DiagWorker - runs the libs/diag checks and the diagnostic report off the GUI thread
// (plan/features/0007 § Data flow and threading, ADR-0068, ADR-0075).
//
// One owned std::jthread for the application (owned by AppViewModel). Requests go into a bounded
// queue (maxQueued = 4): a new request replaces a pending one of the same kind, otherwise a full
// queue drops the oldest pending request (its generation never gets a result). Every request has
// a generation (counts from 1); results carry it, so a caller drops stale ones. cancel() stops the
// running request's stop token (the CheckRunner and the report writer honour it).
//
// Results are handed over in a mutex-protected, bounded list (maxResults; when full the oldest
// progress events are dropped, never final results) and the GUI thread is woken with one
// coalesced queued call by name (the ProbeScanner / DeviceSession pattern, ADR-0031); the signals
// are emitted there. The destructor stops and joins the thread (a running check finishes its
// current vendor call first).
//
// Thread safety: the public functions are called on the GUI thread (the owner).

#include "hio/core/result.h"
#include "hio/diag/check.h"
#include "hio/diag/environment.h"
#include "hio/diag/report.h"

#include <QJsonArray>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace hio::gui {

class DiagWorker : public QObject {
    Q_OBJECT

public:
    static constexpr std::size_t maxQueued = 4;
    static constexpr std::size_t maxResults = 64;

    struct ChecksRequest {
        diag::EnvironmentContext context;
        // empty = all environment checks; else these and (transitively) the checks they depend
        // on, whose results are reported too (in the run order of environmentChecks()).
        std::vector<std::string> ids;
    };
    struct CollectRequest {
        diag::EnvironmentContext context; // its `facts` is set by the worker
        QJsonArray tabs;                  // the open tabs (GUI thread data)
        QString language;
    };
    struct SaveRequest {
        std::shared_ptr<const diag::ReportInput> input;
        std::filesystem::path target;
    };

    explicit DiagWorker(QObject* parent = nullptr);
    ~DiagWorker() override; // stops and joins the worker
    DiagWorker(const DiagWorker&) = delete;
    DiagWorker& operator=(const DiagWorker&) = delete;
    DiagWorker(DiagWorker&&) = delete;
    DiagWorker& operator=(DiagWorker&&) = delete;

    // Each returns the request's generation.
    std::uint64_t runChecks(ChecksRequest request);
    std::uint64_t collectReport(CollectRequest request);
    std::uint64_t saveReport(SaveRequest request);
    // Cancels the request (pending: removed, running: its stop token is stopped).
    void cancel(std::uint64_t generation);

signals:
    void checkProgress(quint64 generation, const hio::diag::CheckResult& result);
    void checksFinished(quint64 generation, const std::vector<hio::diag::CheckResult>& results);
    void reportCollected(quint64 generation,
                         const std::shared_ptr<const hio::diag::ReportInput>& input,
                         const QString& summary);
    void reportSaved(quint64 generation, const hio::Result<std::size_t>& bytes);

private:
    enum class Kind : std::uint8_t { Checks, Collect, Save };
    struct Request {
        std::uint64_t generation = 0;
        std::variant<ChecksRequest, CollectRequest, SaveRequest> payload;
        [[nodiscard]] Kind kind() const { return static_cast<Kind>(payload.index()); }
    };
    struct Progress {
        std::uint64_t generation = 0;
        diag::CheckResult result;
    };
    struct ChecksDone {
        std::uint64_t generation = 0;
        std::vector<diag::CheckResult> results;
    };
    struct Collected {
        std::uint64_t generation = 0;
        std::shared_ptr<const diag::ReportInput> input;
        QString summary;
    };
    struct Saved {
        std::uint64_t generation = 0;
        Result<std::size_t> bytes;
    };
    using Outcome = std::variant<Progress, ChecksDone, Collected, Saved>;

    // GUI thread: invoked (queued, by name) by the worker.
    Q_INVOKABLE void deliver();

    std::uint64_t enqueue(std::variant<ChecksRequest, CollectRequest, SaveRequest> payload);
    void run(const std::stop_token& stop);
    void process(Request& request, const std::stop_token& stop);
    void post(Outcome outcome);

    std::mutex m_mutex; // guards the members below
    std::condition_variable_any m_wake;
    std::deque<Request> m_queue;   // bounded: maxQueued
    std::deque<Outcome> m_results; // bounded: maxResults
    std::uint64_t m_nextGeneration = 1;
    std::uint64_t m_running = 0; // generation of the running request (0 = none)
    std::optional<std::stop_source> m_runningStop;

    std::atomic<bool> m_deliverPending{false};
    std::jthread m_worker; // last member: started last, joined first
};

} // namespace hio::gui
