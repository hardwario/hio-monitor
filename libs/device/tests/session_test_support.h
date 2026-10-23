#pragma once

// Helpers for DeviceSession tests: an event recorder and a wait loop that runs the Qt event loop
// (the facade delivers its signals through queued calls).

#include "hio/core/error.h"
#include "hio/device/console_line.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/session_events.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QList>
#include <QObject>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hio::device::test {

// Runs the event loop until `done` returns true or `timeout` passes.
inline bool waitFor(const std::function<bool()>& done,
                    std::chrono::milliseconds timeout = std::chrono::milliseconds(10000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

// Runs the event loop for `duration` (to see that something does NOT happen).
inline void spin(std::chrono::milliseconds duration) {
    (void)waitFor([] { return false; }, duration);
}

class Recorder {
public:
    explicit Recorder(DeviceSession& session) {
        QObject::connect(&session, &DeviceSession::connectionChanged, &context,
                         [this](const ConnectionEvent& event) { connections.push_back(event); });
        QObject::connect(&session, &DeviceSession::linesReceived, &context,
                         [this](const QList<ConsoleLine>& received) {
                             for (const ConsoleLine& line : received) {
                                 lines.push_back(line);
                             }
                         });
        QObject::connect(&session, &DeviceSession::warning, &context,
                         [this](const Error& error) { warnings.push_back(error); });
        QObject::connect(&session, &DeviceSession::linesDropped, &context,
                         [this](quint64 count) { dropped += count; });
        QObject::connect(
            &session, &DeviceSession::operationProgress, &context,
            [this](OperationId id, const OperationProgress& p) { progress.emplace_back(id, p); });
        QObject::connect(
            &session, &DeviceSession::operationFinished, &context,
            [this](OperationId id, const OperationResult& r) { finished.emplace_back(id, r); });
        // M6 (ADR-0104): prompts in order with the lines (the line count when it was seen).
        QObject::connect(&session, &DeviceSession::promptSeen, &context,
                         [this] { prompts.push_back(lines.size()); });
    }

    // The result of operation `id`, when it finished.
    [[nodiscard]] const OperationResult* result(OperationId id) const {
        for (const auto& [operation, result] : finished) {
            if (operation == id) {
                return &result;
            }
        }
        return nullptr;
    }

    // The distinct phases operation `id` reported, in order.
    [[nodiscard]] std::vector<ProgramPhase> phases(OperationId id) const {
        std::vector<ProgramPhase> result;
        for (const auto& [operation, p] : progress) {
            if (operation == id && (result.empty() || result.back() != p.phase)) {
                result.push_back(p.phase);
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<ConnectionState> state() const {
        if (connections.empty()) {
            return std::nullopt;
        }
        return connections.back().state;
    }

    [[nodiscard]] bool reached(ConnectionState state) const { return this->state() == state; }

    [[nodiscard]] std::vector<std::string> texts(Channel channel, Direction dir) const {
        std::vector<std::string> result;
        for (const ConsoleLine& line : lines) {
            if (line.channel == channel && line.dir == dir) {
                result.push_back(line.text);
            }
        }
        return result;
    }

    [[nodiscard]] int count(ConnectionState state) const {
        int n = 0;
        for (const ConnectionEvent& event : connections) {
            n += event.state == state ? 1 : 0;
        }
        return n;
    }

    std::vector<ConnectionEvent> connections;
    std::vector<ConsoleLine> lines;
    std::vector<Error> warnings;
    std::uint64_t dropped = 0;
    std::vector<std::pair<OperationId, OperationProgress>> progress;
    std::vector<std::pair<OperationId, OperationResult>> finished;
    std::vector<std::size_t> prompts; // lines.size() at each promptSeen

    QObject context; // disconnects the lambdas when the recorder goes away
};

} // namespace hio::device::test
