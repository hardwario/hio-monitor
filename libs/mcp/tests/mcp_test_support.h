#pragma once

// Reusable test doubles for hio_mcp seams (plan/features/0013 A4): IConsentBroker, IActivitySink,
// IReplyChannel, a manually driven ToolCall and an event-loop helper. Header-only, for the tests
// of libs/mcp and libs/mcp_tools (add libs/mcp/tests to the include path).
//
// Tests pump the Qt event loop with runUntil(); production code never runs nested loops
// (ADR-0097).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/mcp/activity.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/json_rpc.h"
#include "hio/mcp/reply_channel.h"
#include "hio/mcp/tool_call.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QJsonObject>
#include <QThread>

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::mcp::test {

// Processes events until `done()` is true or `timeout` passed; returns done().
inline bool runUntil(const std::function<bool()>& done,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds{5000}) {
    const QDeadlineTimer deadline(timeout);
    while (!done()) {
        if (deadline.hasExpired()) {
            return done();
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return true;
}

// Processes events for `duration`.
inline void runFor(std::chrono::milliseconds duration) {
    const QDeadlineTimer deadline(duration);
    while (!deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
}

// IConsentBroker double. Mode Allow/Deny answers at once (Deny with mcp.tool_not_allowed);
// Manual keeps requests pending until decide() is called.
class FakeConsentBroker final : public IConsentBroker {
public:
    enum class Mode : std::uint8_t { Allow, Deny, Manual };

    struct Pending {
        ConsentRequest request;
        std::function<void(ConsentDecision)> done;
        bool withdrawn = false;
    };

    Mode mode = Mode::Allow;
    std::vector<ConsentRequest> requests;   // every request, in order
    std::vector<std::string> gone;          // clientGone() keys
    std::map<std::size_t, Pending> pending; // Manual mode, by request index

    std::function<void()> request(ConsentRequest request,
                                  std::function<void(ConsentDecision)> done) override {
        const std::size_t index = requests.size();
        requests.push_back(request);
        switch (mode) {
        case Mode::Allow:
            done({});
            return {};
        case Mode::Deny:
            done(std::unexpected(makeError(ErrorCode::McpToolNotAllowed)));
            return {};
        case Mode::Manual:
            pending.emplace(index, Pending{.request = std::move(request),
                                           .done = std::move(done),
                                           .withdrawn = false});
            return [this, index] {
                if (auto it = pending.find(index); it != pending.end()) {
                    it->second.withdrawn = true;
                }
            };
        }
        return {};
    }

    void clientGone(std::string_view clientKey) override { gone.emplace_back(clientKey); }

    // Manual mode: answers request `index` (no-op when withdrawn or unknown).
    void decide(std::size_t index, ConsentDecision decision) {
        auto it = pending.find(index);
        if (it == pending.end() || it->second.withdrawn) {
            return;
        }
        auto done = std::move(it->second.done);
        pending.erase(it);
        done(std::move(decision));
    }

    [[nodiscard]] bool withdrawn(std::size_t index) const {
        auto it = pending.find(index);
        return it != pending.end() && it->second.withdrawn;
    }
};

class RecordingActivitySink final : public IActivitySink {
public:
    std::vector<ActivityEntry> entries;
    void record(ActivityEntry entry) override { entries.push_back(std::move(entry)); }
};

// IReplyChannel double: records every message. `persistent` behaves like stdio (complete() does
// not close it). All state is public for the tests.
class RecordingReplyChannel final : public IReplyChannel {
public:
    explicit RecordingReplyChannel(bool streamable = true, bool keepOpen = false)
        : streamable(streamable), persistent(keepOpen) {}

    bool streamable;
    bool persistent;
    std::vector<QJsonObject> notifications;
    std::vector<QJsonObject> messages; // notifications and completions, in order
    QJsonObject response;              // the last complete() message
    ReplyMeta meta;                    // of the last complete()
    std::size_t completions = 0;
    bool streamStarted = false;
    bool finished = false;
    bool closed = false;
    std::function<void(CloseReason)> closedHandler;

    [[nodiscard]] bool canStream() const override { return streamable; }
    void startStream() override {
        if (streamable) {
            streamStarted = true;
        }
    }
    void notify(const QJsonObject& notification) override {
        if (!isOpen() || !streamable) {
            return;
        }
        notifications.push_back(notification);
        messages.push_back(notification);
    }
    void complete(const QJsonObject& message, const ReplyMeta& replyMeta) override {
        if (!isOpen()) {
            return;
        }
        ++completions;
        response = message;
        meta = replyMeta;
        if (!message.isEmpty()) {
            messages.push_back(message);
        }
        if (!persistent) {
            finished = true;
        }
    }
    [[nodiscard]] bool isOpen() const override { return !finished && !closed; }
    void setClosedHandler(std::function<void(CloseReason)> handler) override {
        closedHandler = std::move(handler);
    }

    // Simulates the peer going away.
    void close(CloseReason reason = CloseReason::PeerClosed) {
        if (closed) {
            return;
        }
        closed = true;
        if (auto handler = std::exchange(closedHandler, {})) {
            handler(reason);
        }
    }

    [[nodiscard]] bool done() const { return completions > 0; }
};

// A ToolCall driven by the test: finish(), reportProgress(), requestConsent() and setDeviceId()
// are public. onCancel() counts and finishes with `cancelled` unless finishOnCancel is false.
class ManualToolCall final : public ToolCall {
public:
    using ToolCall::finish;
    using ToolCall::reportProgress;
    using ToolCall::requestConsent;
    using ToolCall::setDeviceId;

    int cancelCount = 0;
    bool finishOnCancel = true;

protected:
    void onCancel() override {
        ++cancelCount;
        if (finishOnCancel) {
            ToolCall::onCancel();
        }
    }
};

} // namespace hio::mcp::test
