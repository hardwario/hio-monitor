#pragma once

// Offloader - runs the blocking work of MCP handlers on short-lived worker threads (ADR-0162,
// plan/features/0013 "Threading summary"): the Intel HEX parse of `flash`, the LTE zip
// classification of `flash_lte` and the probe enumeration of CliDeviceProvider::refresh().
//
// One owned std::jthread per job, at most `maxRunning` at once (Limits::maxOffloadRunning); more
// jobs wait in a FIFO of `maxQueued` (Limits::maxOffloadQueued); beyond that submit() fails with
// mcp.too_many_requests. A job gets a std::stop_token and must only use data it owns (captured by
// value); it never touches sessions, ConsoleLog or other owner-thread objects. A job must not
// throw: an escaping exception terminates the process, like any std::thread (ADR-0007: no
// exceptions across thread boundaries).
//
// The job's value is handed back to the owner thread with a queued call; `done` runs there,
// guarded by a QPointer to `receiver`. `done` is never called when `receiver` was destroyed, the
// handle was cancelled (or destroyed), or the offloader was stopped. A cancelled running job
// still occupies its slot until its thread has ended (the thread is joined on the owner thread
// when its result arrives; at that point the job has already returned).
//
// stopAll() and the destructor request a stop on every running job, drop the queued ones (their
// `done` is never called) and join all threads (they block until the jobs have observed the stop
// token and returned). The offloader stays usable after stopAll().
//
// Thread safety: owner thread only (the thread that created the offloader), except the job body.

#include "hio/core/result.h"

#include <QObject>
#include <QPointer>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

namespace hio::mcp {

class Offloader final : public QObject {
    Q_OBJECT

public:
    // Cancels its job when destroyed (RAII). Movable, not copyable. A default-constructed or
    // moved-from handle does nothing.
    class Handle final {
    public:
        Handle() = default;
        ~Handle() { cancel(); }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        Handle(Handle&& other) noexcept
            : m_owner(std::move(other.m_owner)), m_id(std::exchange(other.m_id, 0)) {}
        Handle& operator=(Handle&& other) noexcept {
            if (this != &other) {
                cancel();
                m_owner = std::move(other.m_owner);
                m_id = std::exchange(other.m_id, 0);
            }
            return *this;
        }

        // Stop requested if running (its result is discarded), removed if queued; `done` is never
        // called afterwards. No-op when the job has already delivered its result.
        void cancel() {
            if (m_id != 0 && !m_owner.isNull()) {
                m_owner->cancel(m_id);
            }
            m_id = 0;
        }

    private:
        friend class Offloader;
        Handle(Offloader& owner, std::uint64_t id) : m_owner(&owner), m_id(id) {}

        QPointer<Offloader> m_owner;
        std::uint64_t m_id = 0;
    };

    Offloader(std::size_t maxRunning, std::size_t maxQueued, QObject* parent = nullptr);
    ~Offloader() override;
    Offloader(const Offloader&) = delete;
    Offloader& operator=(const Offloader&) = delete;
    Offloader(Offloader&&) = delete;
    Offloader& operator=(Offloader&&) = delete;

    // Starts `job` at once when fewer than maxRunning jobs run, else queues it. Error:
    // mcp.too_many_requests when maxQueued jobs are already waiting.
    template <class T>
    [[nodiscard]] hio::Result<Handle>
    submit(QObject& receiver, std::function<T(std::stop_token)> job, std::function<void(T)> done) {
        // The value slot is written by the worker before it posts the completion and read on the
        // owner thread after it (the event queue orders both).
        auto slot = std::make_shared<std::optional<T>>();
        Work work;
        work.run = [slot, job = std::move(job)](const std::stop_token& token) {
            slot->emplace(job(token));
        };
        work.deliver = [slot, done = std::move(done)] {
            if (slot->has_value()) {
                done(std::move(**slot));
            }
        };
        auto id = enqueue(receiver, std::move(work));
        if (!id) {
            return std::unexpected(std::move(id).error());
        }
        return Handle(*this, *id);
    }

    void stopAll();

    [[nodiscard]] std::size_t running() const noexcept { return m_running.size(); }
    [[nodiscard]] std::size_t queued() const noexcept { return m_queue.size(); }

private:
    // The type-erased job: `run` on the worker thread, `deliver` on the owner thread.
    struct Work {
        std::function<void(const std::stop_token&)> run;
        std::function<void()> deliver;
        QPointer<QObject> receiver;
        std::uint64_t id = 0;
    };

    struct Running {
        std::function<void()> deliver; // empty once cancelled
        QPointer<QObject> receiver;
        std::jthread thread;
    };

    [[nodiscard]] hio::Result<std::uint64_t> enqueue(QObject& receiver, Work work);
    void cancel(std::uint64_t id);
    void start(Work work);
    void startQueued();
    void onFinished(std::uint64_t id);

    std::size_t m_maxRunning;
    std::size_t m_maxQueued;
    std::uint64_t m_nextId = 1;
    std::map<std::uint64_t, Running> m_running; // at most m_maxRunning entries
    std::deque<Work> m_queue;                   // at most m_maxQueued entries
};

} // namespace hio::mcp
