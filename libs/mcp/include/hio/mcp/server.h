#pragma once

// McpServer - the transport-neutral MCP server (plan/features/0013 "Protocol core", ADR-0095,
// ADR-0097, ADR-0105, ADR-0107).
//
// One JSON-RPC message in (handle()), zero or more messages out through the IReplyChannel. Both
// dialects are served at the same time:
//   - Legacy (2025-11-25): `initialize` opens a session (HTTP: Mcp-Session-Id, at most
//     Limits::maxLegacySessions, idle expiry Limits::legacySessionIdle, DELETE ends it, GET opens
//     its notification stream; stdio: one session per connection). ping,
//     resources/subscribe|unsubscribe, notifications/cancelled.
//   - Modern (2026-07-28): every request carries _meta io.modelcontextprotocol/protocolVersion
//     (+ clientCapabilities, clientInfo); server/discover, subscriptions/listen; every result has
//     resultType and _meta serverInfo, cacheable results ttlMs + cacheScope (protocol.h);
//     cancellation = closing the HTTP stream or notifications/cancelled on stdio.
// Era detection per message: initialize -> legacy; _meta protocolVersion -> modern; a valid legacy
// session (HTTP header / stdio connection) -> legacy; otherwise an error.
//
// Tool calls: arguments are validated against the input schema (-32602 with error.data.pointer),
// at most Limits::maxInFlightCalls run at once (more -> tool result mcp.too_many_requests), each
// ends with exactly one response (tool errors are tool results with isError) and one
// ActivityEntry. A call the client cancelled gets no response.
//
// Resource changes (called by hio_mcp_tools): notifyResourceUpdated(uri) sends
// notifications/resources/updated to the subscribers of `uri`, at most once per
// Limits::resourceUpdateInterval per URI (later calls are coalesced into one trailing
// notification); notifyResourceListChanged() sends notifications/resources/list_changed (device
// opened or closed).
//
// Thread safety: owner thread only. No nested event loops (ADR-0097).

#include "hio/mcp/activity.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/offload.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/reply_channel.h"
#include "hio/mcp/request_context.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <any>
#include <cstddef>
#include <memory>
#include <string_view>

namespace hio::mcp {

struct ServerOptions {
    Limits limits;
    QString name;         // empty -> protocol::serverName ("hio-monitor")
    QString version;      // empty -> hio::buildInfo().version
    QString instructions; // optional "instructions" of initialize / server/discover
    std::any toolContext; // ToolEnv::context()
};

class McpServer : public QObject {
    Q_OBJECT

public:
    McpServer(Registry registry, IConsentBroker& consent, IActivitySink& activity,
              ServerOptions options, QObject* parent = nullptr);
    ~McpServer() override;
    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;
    McpServer(McpServer&&) = delete;
    McpServer& operator=(McpServer&&) = delete;

    // Handles one message (a stdio line or an HTTP POST body). Every message ends with exactly one
    // reply->complete() (a response, or Accepted for notifications), except
    // subscriptions/listen, whose stream stays open until the client closes it or stop().
    void handle(const QByteArray& message, std::shared_ptr<IReplyChannel> reply,
                const TransportInfo& transport);

    // HTTP GET /mcp: the legacy session's notification stream (replaces an older one).
    // BadRequest without a session id, NotFound for an unknown/expired session,
    // ServiceUnavailable when Limits::maxListenStreams streams are open. The channel is kept
    // until it closes or the session ends.
    [[nodiscard]] ReplyStatus openSessionStream(const TransportInfo& transport,
                                                std::shared_ptr<IReplyChannel> stream);
    // HTTP DELETE /mcp: ends a legacy session (Ok), NotFound / BadRequest as above.
    [[nodiscard]] ReplyStatus endSession(const TransportInfo& transport);

    // Cancels every call (they still answer, with `cancelled`), stops the offloaded jobs
    // (Offloader::stopAll(): joins their threads), ends subscriptions (modern: graceful listen
    // result) and sessions (IConsentBroker::clientGone). Later messages are answered with -32603.
    // callsDrained() follows when the last call has finished.
    void stop();

    void notifyResourceUpdated(const QString& uri);
    void notifyResourceListChanged();

    [[nodiscard]] bool isStopped() const noexcept;
    [[nodiscard]] std::size_t activeCalls() const noexcept;
    [[nodiscard]] std::size_t legacySessionCount() const noexcept;
    [[nodiscard]] std::size_t subscriptionCount() const noexcept;
    [[nodiscard]] const Registry& registry() const noexcept;
    [[nodiscard]] const Limits& limits() const noexcept;
    // The worker threads of blocking handler work (ADR-0162; Limits::maxOffloadRunning /
    // maxOffloadQueued). Also available to tool handlers as ToolEnv::offloader().
    [[nodiscard]] Offloader& offloader() noexcept;

signals:
    // After stop(): no call is running any more (emitted at once when none was).
    void callsDrained();
    // A legacy session started or ended, or a modern client was seen first / forgotten.
    void clientsChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hio::mcp
