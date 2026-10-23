#include "hio/mcp/offload.h"

#include "hio/core/error.h"
#include "hio/core/result.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <Qt>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <stop_token>
#include <thread>
#include <utility>

namespace hio::mcp {

Offloader::Offloader(std::size_t maxRunning, std::size_t maxQueued, QObject* parent)
    : QObject(parent), m_maxRunning(maxRunning == 0 ? 1 : maxRunning), m_maxQueued(maxQueued) {}

Offloader::~Offloader() { stopAll(); }

hio::Result<std::uint64_t> Offloader::enqueue(QObject& receiver, Work work) {
    if (m_running.size() >= m_maxRunning && m_queue.size() >= m_maxQueued) {
        return std::unexpected(makeError(ErrorCode::McpTooManyRequests));
    }
    work.receiver = &receiver;
    work.id = m_nextId++;
    const std::uint64_t id = work.id;
    if (m_running.size() < m_maxRunning) {
        start(std::move(work));
    } else {
        m_queue.push_back(std::move(work));
    }
    return id;
}

void Offloader::start(Work work) {
    const std::uint64_t id = work.id;
    Running& entry = m_running[id];
    entry.deliver = std::move(work.deliver);
    entry.receiver = std::move(work.receiver);
    // The worker only posts to `this`: the offloader joins every thread before it is destroyed,
    // and posted events of a destroyed object are discarded by Qt.
    entry.thread =
        std::jthread([this, id, run = std::move(work.run)](const std::stop_token& token) {
            run(token);
            QMetaObject::invokeMethod(this, [this, id] { onFinished(id); }, Qt::QueuedConnection);
        });
}

void Offloader::startQueued() {
    while (m_running.size() < m_maxRunning && !m_queue.empty()) {
        Work work = std::move(m_queue.front());
        m_queue.pop_front();
        if (work.receiver.isNull()) {
            continue; // nobody would receive the result
        }
        start(std::move(work));
    }
}

void Offloader::cancel(std::uint64_t id) {
    if (auto it = m_running.find(id); it != m_running.end()) {
        it->second.deliver = nullptr; // releases the captures of `done` on the owner thread
        it->second.thread.request_stop();
        return; // the slot is freed when the thread has ended (onFinished)
    }
    std::erase_if(m_queue, [id](const Work& work) { return work.id == id; });
}

void Offloader::onFinished(std::uint64_t id) {
    auto it = m_running.find(id);
    if (it == m_running.end()) {
        return; // stopped meanwhile
    }
    Running entry = std::move(it->second);
    m_running.erase(it);
    // The job has returned and only the end of the thread function remains: a short join.
    entry.thread.join();
    startQueued();
    // Last: `done` may submit or cancel jobs.
    if (entry.deliver && !entry.receiver.isNull()) {
        entry.deliver();
    }
}

void Offloader::stopAll() {
    m_queue.clear();
    for (auto& [id, entry] : m_running) {
        entry.deliver = nullptr;
        entry.thread.request_stop();
    }
    auto running = std::move(m_running);
    m_running.clear();
    for (auto& [id, entry] : running) {
        if (entry.thread.joinable()) {
            entry.thread.join();
        }
    }
}

} // namespace hio::mcp
