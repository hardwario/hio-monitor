#include "hio/mcp/server.h"

#include "dialect_legacy.h"
#include "dialect_modern.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/version.h"
#include "hio/mcp/activity.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/json_rpc.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/offload.h"
#include "hio/mcp/protocol.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/reply_channel.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/schema.h"
#include "hio/mcp/tool_call.h"

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QtCore/qcborcommon.h>
#include <QtCore/qtmetamacros.h>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::mcp {

namespace {

using detail::DialectError;

QString latin1(std::string_view text) {
    return QLatin1StringView(text.data(), static_cast<qsizetype>(text.size())).toString();
}

bool is(const QString& method, std::string_view name) {
    return method == QLatin1StringView(name.data(), static_cast<qsizetype>(name.size()));
}

RpcError rpcError(int code, const QString& message, const QJsonValue& data = {}) {
    return RpcError{
        .code = code, .message = message, .data = data.isNull() ? QJsonValue::Undefined : data};
}

QJsonObject pointerData(const QString& pointer) {
    QJsonObject data;
    data.insert(QStringLiteral("pointer"), pointer);
    return data;
}

std::string newSessionId() {
    // 128 random bits as 32 lowercase hex characters (visible ASCII, Streamable HTTP rule).
    QByteArray bytes(16, '\0');
    for (char& byte : bytes) {
        byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return bytes.toHex().toStdString();
}

QString shortened(const QString& text, qsizetype maxChars) {
    if (text.size() <= maxChars) {
        return text;
    }
    return text.left(maxChars) + QChar(0x2026);
}

QJsonValue shortenStrings(const QJsonValue& value) {
    constexpr qsizetype maxStringChars = 64;
    if (value.isString()) {
        return shortened(value.toString(), maxStringChars);
    }
    if (value.isObject()) {
        QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            it.value() = shortenStrings(it.value());
        }
        return object;
    }
    if (value.isArray()) {
        QJsonArray array = value.toArray();
        for (qsizetype i = 0; i < array.size(); ++i) {
            array.replace(i, shortenStrings(array.at(i)));
        }
        return array;
    }
    return value;
}

std::string resultCode(const hio::Result<ToolResult>& result) {
    if (!result) {
        return std::string(idOf(result.error().code));
    }
    if (result->isError) {
        const QString code = result->structured.value(QStringLiteral("error"))
                                 .toObject()
                                 .value(QStringLiteral("code"))
                                 .toString();
        return code.isEmpty() ? std::string("error") : code.toStdString();
    }
    return "ok";
}

} // namespace

struct McpServer::Impl {
    struct Session {
        std::string id;
        std::string protocolVersion;
        ClientInfo client;
        TransportKind kind = TransportKind::Http;
        std::uint64_t connectionId = 0;
        QElapsedTimer lastActivity;
        std::set<QString> subscriptions;
        std::shared_ptr<IReplyChannel> stream; // HTTP: GET stream; stdio: the transport channel
    };

    struct Listen {
        QJsonValue id;
        std::shared_ptr<IReplyChannel> channel;
        std::uint64_t connectionId = 0;
        bool resourcesListChanged = false;
        std::set<QString> uris;
    };

    struct Call {
        QJsonValue id;
        std::shared_ptr<IReplyChannel> reply;
        RequestContext context;
        const ToolSpec* spec = nullptr;
        std::unique_ptr<ToolCall> call;
        QJsonObject arguments;
        QDateTime started;
        QElapsedTimer elapsed;
        std::uint64_t connectionId = 0;
        bool suppressReply = false;
        // Progress throttling (ADR-0107)
        QElapsedTimer progressClock;
        double lastProgress = -std::numeric_limits<double>::infinity();
        std::optional<QJsonObject> pendingProgress;
        std::unique_ptr<QTimer> progressTimer;
    };

    struct UpdateState {
        QElapsedTimer last;
        bool sent = false;
        std::unique_ptr<QTimer> timer; // trailing notification; active = pending
    };

    struct ModernClient {
        QElapsedTimer lastSeen;
    };

    // One incoming message with its reply path.
    struct Incoming {
        Message message;
        std::shared_ptr<IReplyChannel> reply;
        TransportInfo transport;
    };

    McpServer& q;
    Registry registry;
    IConsentBroker& consent;
    IActivitySink& activity;
    Limits limits;
    detail::ServerIdentity identity;
    Offloader offloader;
    ToolEnv env;
    bool stopped = false;
    bool destroying = false;

    std::map<std::string, Session> sessions;
    std::map<std::uint64_t, std::string> stdioSessions; // connection id -> session id
    std::map<std::string, ModernClient> modernClients;
    std::map<std::uint64_t, Listen> listens;
    std::map<std::uint64_t, Call> calls;
    std::map<QString, UpdateState> updates;
    std::vector<std::unique_ptr<ToolCall>> graveyard;
    bool purgeScheduled = false;
    std::uint64_t nextKey = 1;
    QTimer sweepTimer;

    Impl(McpServer& server, Registry reg, IConsentBroker& broker, IActivitySink& sink,
         ServerOptions options)
        : q(server), registry(std::move(reg)), consent(broker), activity(sink),
          limits(options.limits),
          identity{.name = options.name.isEmpty() ? latin1(protocol::serverName) : options.name,
                   .version = options.version.isEmpty() ? latin1(hio::buildInfo().version)
                                                        : options.version,
                   .instructions = options.instructions},
          offloader(limits.maxOffloadRunning, limits.maxOffloadQueued),
          env(broker, limits, offloader, std::move(options.toolContext)) {}

    // ---- replies -----------------------------------------------------------------------------

    static void replyError(const Incoming& in, const RpcError& error, ReplyStatus status) {
        if (in.message.kind == MessageKind::Notification) {
            // Notifications are never answered; HTTP still needs a status (body without id).
            if (in.transport.kind == TransportKind::Http) {
                in.reply->complete(errorResponse(QJsonValue::Undefined, error),
                                   ReplyMeta{.status = status, .sessionId = std::nullopt});
            } else {
                in.reply->complete(QJsonObject(),
                                   ReplyMeta{.status = ReplyStatus::Accepted, .sessionId = {}});
            }
            return;
        }
        in.reply->complete(errorResponse(in.message.id, error),
                           ReplyMeta{.status = status, .sessionId = std::nullopt});
    }

    static void replyError(const Incoming& in, const DialectError& error) {
        replyError(in, error.error, error.status);
    }

    void replyResult(const Incoming& in, const RequestContext& context, QJsonObject result,
                     std::optional<std::int64_t> ttlMs = std::nullopt) const {
        in.reply->complete(
            resultResponse(in.message.id, decorate(std::move(result), context, ttlMs)),
            ReplyMeta{.status = ReplyStatus::Ok, .sessionId = std::nullopt});
    }

    static void accept(const Incoming& in) {
        in.reply->complete(QJsonObject(),
                           ReplyMeta{.status = ReplyStatus::Accepted, .sessionId = {}});
    }

    [[nodiscard]] QJsonObject decorate(QJsonObject result, const RequestContext& context,
                                       std::optional<std::int64_t> ttlMs) const {
        if (context.dialect == Dialect::Modern) {
            return detail::decorateModernResult(std::move(result), identity, ttlMs);
        }
        return result;
    }

    // ---- entry -------------------------------------------------------------------------------

    void handle(const QByteArray& bytes, std::shared_ptr<IReplyChannel> reply,
                const TransportInfo& transport) {
        if (!reply) {
            return;
        }
        auto parsed = parseMessage(bytes);
        if (!parsed) {
            // Also for a broken notification (stdio: an error line); see json_rpc.h (review R7).
            reply->complete(errorResponse(parsed.error().id, parsed.error().error),
                            ReplyMeta{.status = ReplyStatus::BadRequest, .sessionId = {}});
            return;
        }
        Incoming in{
            .message = std::move(*parsed), .reply = std::move(reply), .transport = transport};
        if (in.message.kind == MessageKind::Response) {
            accept(in);
            return;
        }
        if (stopped) {
            replyError(in,
                       rpcError(protocol::errc::internalError,
                                QStringLiteral("The server is shutting down")),
                       ReplyStatus::ServiceUnavailable);
            return;
        }
        sweep();

        const QString& method = in.message.method;
        const std::optional<QString> version = detail::modernVersion(in.message.params);
        const bool http = transport.kind == TransportKind::Http;

        if (is(method, protocol::method::initialize)) {
            handleInitialize(in);
            return;
        }
        if (http && transport.sessionId) {
            if (version) {
                replyError(in,
                           rpcError(protocol::errc::invalidRequest,
                                    QStringLiteral("A 2026-07-28 request must not carry "
                                                   "Mcp-Session-Id")),
                           ReplyStatus::BadRequest);
                return;
            }
            handleLegacy(in);
            return;
        }
        if (version || is(method, protocol::method::serverDiscover) ||
            is(method, protocol::method::subscriptionsListen)) {
            handleModern(in, version);
            return;
        }
        if (!http && stdioSessions.contains(transport.connectionId)) {
            handleLegacy(in);
            return;
        }
        if (http && !(transport.protocolVersionHeader &&
                      *transport.protocolVersionHeader == protocol::modernRevision)) {
            replyError(in,
                       rpcError(protocol::errc::invalidRequest,
                                QStringLiteral("Missing Mcp-Session-Id (send initialize first, or "
                                               "use 2026-07-28 with _meta)")),
                       ReplyStatus::BadRequest);
            return;
        }
        handleModern(in, version);
    }

    // ---- legacy ------------------------------------------------------------------------------

    void handleInitialize(const Incoming& in) {
        if (in.message.kind == MessageKind::Notification) {
            accept(in);
            return;
        }
        const bool http = in.transport.kind == TransportKind::Http;
        if (http && in.transport.sessionId) {
            replyError(in,
                       rpcError(protocol::errc::invalidRequest,
                                QStringLiteral("initialize must not carry Mcp-Session-Id")),
                       ReplyStatus::BadRequest);
            return;
        }
        const QJsonValue requested = in.message.params.value(QStringLiteral("protocolVersion"));
        if (!requested.isString()) {
            replyError(in,
                       rpcError(protocol::errc::invalidParams,
                                QStringLiteral("Invalid params: /protocolVersion is required"),
                                pointerData(QStringLiteral("/protocolVersion"))),
                       ReplyStatus::BadRequest);
            return;
        }
        if (!http) {
            if (auto it = stdioSessions.find(in.transport.connectionId);
                it != stdioSessions.end()) {
                endSessionById(std::string(it->second), false);
            }
        }
        if (sessions.size() >= limits.maxLegacySessions) {
            qCWarning(hio::log::lcMcp) << "MCP: legacy session limit reached; initialize refused";
            replyError(
                in,
                rpcError(
                    protocol::errc::internalError,
                    QStringLiteral("Too many sessions (at most %1)").arg(limits.maxLegacySessions)),
                ReplyStatus::ServiceUnavailable);
            return;
        }
        Session session;
        session.id = newSessionId();
        // An unknown legacy version is answered with 2025-11-25, never with an error (ADR-0159).
        session.protocolVersion = detail::negotiateLegacyVersion(requested.toString());
        session.client = detail::legacyClientInfo(in.message.params);
        session.kind = in.transport.kind;
        session.connectionId = in.transport.connectionId;
        session.lastActivity.start();
        if (!http) {
            session.stream = in.reply; // the persistent stdio channel
            stdioSessions[in.transport.connectionId] = session.id;
        }
        const std::string id = session.id;
        const QJsonObject result = detail::initializeResult(session.protocolVersion, identity);
        qCInfo(hio::log::lcMcp).noquote()
            << "MCP: legacy session started for" << QString::fromStdString(session.client.name)
            << "protocol" << QString::fromStdString(session.protocolVersion);
        sessions.emplace(id, std::move(session));
        in.reply->complete(
            resultResponse(in.message.id, result),
            ReplyMeta{.status = ReplyStatus::Ok,
                      .sessionId = http ? std::optional<std::string>(id) : std::nullopt});
        emit q.clientsChanged();
    }

    [[nodiscard]] Session* findSession(const TransportInfo& transport) {
        if (transport.kind == TransportKind::Http) {
            if (!transport.sessionId) {
                return nullptr;
            }
            auto it = sessions.find(*transport.sessionId);
            return it == sessions.end() ? nullptr : &it->second;
        }
        auto link = stdioSessions.find(transport.connectionId);
        if (link == stdioSessions.end()) {
            return nullptr;
        }
        auto it = sessions.find(link->second);
        return it == sessions.end() ? nullptr : &it->second;
    }

    void handleLegacy(const Incoming& in) {
        Session* session = findSession(in.transport);
        if (session == nullptr) {
            replyError(in,
                       rpcError(protocol::errc::invalidRequest,
                                QStringLiteral("Unknown or expired session")),
                       ReplyStatus::NotFound);
            return;
        }
        session->lastActivity.restart();
        if (in.transport.kind == TransportKind::Http && in.transport.protocolVersionHeader &&
            *in.transport.protocolVersionHeader != session->protocolVersion) {
            replyError(in,
                       rpcError(protocol::errc::invalidRequest,
                                QStringLiteral("MCP-Protocol-Version does not match the "
                                               "negotiated version")),
                       ReplyStatus::BadRequest);
            return;
        }
        RequestContext context{.dialect = Dialect::Legacy,
                               .protocolVersion = session->protocolVersion,
                               .client = session->client,
                               .clientKey = session->id,
                               .tool = {},
                               .requestId = in.message.id,
                               .progressToken = progressToken(in.message.params)};
        const QString& method = in.message.method;
        if (in.message.kind == MessageKind::Notification) {
            if (is(method, protocol::method::cancelled)) {
                cancelRequest(in.message.params, Dialect::Legacy, session->id, 0);
            }
            accept(in);
            return;
        }
        if (is(method, protocol::method::ping)) {
            replyResult(in, context, QJsonObject());
        } else if (is(method, protocol::method::resourcesSubscribe)) {
            subscribe(in, *session, context);
        } else if (is(method, protocol::method::resourcesUnsubscribe)) {
            unsubscribe(in, *session, context);
        } else if (!dispatchShared(in, context)) {
            replyError(in,
                       rpcError(protocol::errc::methodNotFound,
                                QStringLiteral("Method not found: %1").arg(method)),
                       ReplyStatus::Ok);
        }
    }

    void subscribe(const Incoming& in, Session& session, const RequestContext& context) const {
        const QJsonValue uri = in.message.params.value(QStringLiteral("uri"));
        if (!uri.isString()) {
            replyError(in,
                       rpcError(protocol::errc::invalidParams,
                                QStringLiteral("Invalid params: /uri is required"),
                                pointerData(QStringLiteral("/uri"))),
                       ReplyStatus::Ok);
            return;
        }
        if (!registry.matchResource(uri.toString())) {
            replyError(in, resourceNotFound(Dialect::Legacy, uri.toString()), ReplyStatus::Ok);
            return;
        }
        if (!session.subscriptions.contains(uri.toString())) {
            if (subscriptionCount() >= limits.maxSubscriptions) {
                replyError(in,
                           rpcError(protocol::errc::internalError,
                                    QStringLiteral("Too many subscriptions (at most %1)")
                                        .arg(limits.maxSubscriptions)),
                           ReplyStatus::Ok);
                return;
            }
            session.subscriptions.insert(uri.toString());
        }
        replyResult(in, context, QJsonObject());
    }

    void unsubscribe(const Incoming& in, Session& session, const RequestContext& context) {
        const QJsonValue uri = in.message.params.value(QStringLiteral("uri"));
        if (!uri.isString()) {
            replyError(in,
                       rpcError(protocol::errc::invalidParams,
                                QStringLiteral("Invalid params: /uri is required"),
                                pointerData(QStringLiteral("/uri"))),
                       ReplyStatus::Ok);
            return;
        }
        session.subscriptions.erase(uri.toString());
        dropUnusedUpdates();
        replyResult(in, context, QJsonObject());
    }

    // ---- modern ------------------------------------------------------------------------------

    void handleModern(const Incoming& in, const std::optional<QString>& version) {
        if (!version) {
            replyError(in, detail::missingMeta(protocol::meta::protocolVersion));
            return;
        }
        if (auto mismatch = detail::checkModernHeaders(in.message, in.transport, *version)) {
            replyError(in, *mismatch);
            return;
        }
        if (*version != latin1(protocol::modernRevision)) {
            replyError(in, detail::unsupportedVersion(*version));
            return;
        }
        const QJsonValue capabilities = in.message.params.value(latin1(protocol::meta::key))
                                            .toObject()
                                            .value(latin1(protocol::meta::clientCapabilities));
        if (!capabilities.isObject()) {
            replyError(in, detail::missingMeta(protocol::meta::clientCapabilities));
            return;
        }
        RequestContext context{.dialect = Dialect::Modern,
                               .protocolVersion = version->toStdString(),
                               .client = detail::modernClientInfo(in.message.params),
                               .clientKey = {},
                               .tool = {},
                               .requestId = in.message.id,
                               .progressToken = progressToken(in.message.params)};
        context.clientKey =
            context.client.name.empty() ? std::string("unknown") : context.client.name;
        touchModernClient(context.clientKey);

        const QString& method = in.message.method;
        if (in.message.kind == MessageKind::Notification) {
            if (is(method, protocol::method::cancelled)) {
                cancelRequest(in.message.params, Dialect::Modern, {}, in.transport.connectionId);
            }
            accept(in);
            return;
        }
        if (is(method, protocol::method::serverDiscover)) {
            QJsonArray supported;
            for (const std::string_view revision : protocol::supportedRevisions) {
                supported.append(latin1(revision));
            }
            QJsonObject result;
            result.insert(QStringLiteral("supportedVersions"), supported);
            result.insert(QStringLiteral("capabilities"), detail::serverCapabilities());
            if (!identity.instructions.isEmpty()) {
                result.insert(QStringLiteral("instructions"), identity.instructions);
            }
            replyResult(in, context, result, protocol::cache::discoverTtlMs);
        } else if (is(method, protocol::method::subscriptionsListen)) {
            listen(in, context);
        } else if (!dispatchShared(in, context)) {
            // Modern: -32601 with HTTP 404 (2026-07-28 Streamable HTTP; accepted in the review of
            // plan 0013). Legacy answers it with 200.
            replyError(in,
                       rpcError(protocol::errc::methodNotFound,
                                QStringLiteral("Method not found: %1").arg(method)),
                       ReplyStatus::NotFound);
        }
    }

    void touchModernClient(const std::string& key) {
        auto it = modernClients.find(key);
        if (it != modernClients.end()) {
            it->second.lastSeen.restart();
            return;
        }
        if (modernClients.size() >= limits.maxModernClients) {
            auto oldest = std::ranges::max_element(modernClients, {}, [](const auto& entry) {
                return entry.second.lastSeen.elapsed();
            });
            const std::string gone = oldest->first;
            modernClients.erase(oldest);
            consent.clientGone(gone);
        }
        ModernClient client;
        client.lastSeen.start();
        modernClients.emplace(key, client);
        emit q.clientsChanged();
    }

    void listen(const Incoming& in, const RequestContext& context) {
        if (idInFlight(in, context)) {
            replyError(in, duplicateId(), ReplyStatus::Ok);
            return;
        }
        if (!in.reply->canStream()) {
            replyError(in,
                       rpcError(protocol::errc::invalidRequest,
                                QStringLiteral("subscriptions/listen needs a stream "
                                               "(Accept: text/event-stream)")),
                       ReplyStatus::BadRequest);
            return;
        }
        if (streamCount() >= limits.maxListenStreams) {
            replyError(in,
                       rpcError(protocol::errc::internalError,
                                QStringLiteral("Too many subscription streams (at most %1)")
                                    .arg(limits.maxListenStreams)),
                       ReplyStatus::ServiceUnavailable);
            return;
        }
        const QJsonValue filterValue = in.message.params.value(QStringLiteral("notifications"));
        if (!filterValue.isObject()) {
            replyError(in,
                       rpcError(protocol::errc::invalidParams,
                                QStringLiteral("Invalid params: /notifications is required"),
                                pointerData(QStringLiteral("/notifications"))),
                       ReplyStatus::Ok);
            return;
        }
        const QJsonObject filter = filterValue.toObject();
        Listen entry;
        entry.id = in.message.id;
        entry.channel = in.reply;
        entry.connectionId = in.transport.connectionId;
        entry.resourcesListChanged =
            filter.value(QStringLiteral("resourcesListChanged")).toBool(false);
        QJsonArray honoredUris;
        std::size_t available =
            limits.maxSubscriptions - std::min(limits.maxSubscriptions, subscriptionCount());
        for (const auto& uri : filter.value(QStringLiteral("resourceSubscriptions")).toArray()) {
            if (!uri.isString() || entry.uris.contains(uri.toString()) || available == 0 ||
                !registry.matchResource(uri.toString())) {
                continue;
            }
            entry.uris.insert(uri.toString());
            honoredUris.append(uri);
            --available;
        }
        // Tools and prompts never change at run time (no listChanged capability): not honored.
        QJsonObject acknowledged;
        if (entry.resourcesListChanged) {
            acknowledged.insert(QStringLiteral("resourcesListChanged"), true);
        }
        if (!honoredUris.isEmpty()) {
            acknowledged.insert(QStringLiteral("resourceSubscriptions"), honoredUris);
        }
        const std::uint64_t key = nextKey++;
        // Registered and watched before the stream starts: a close during the SSE header write
        // (slow client) must end the listen (review R8).
        listens.emplace(key, std::move(entry));
        watchChannel(in.reply);
        in.reply->startStream();
        if (!listens.contains(key)) {
            return;
        }
        QJsonObject params;
        params.insert(QStringLiteral("notifications"), acknowledged);
        in.reply->notify(
            listenNotification(in.message.id, protocol::method::subscriptionsAcknowledged, params));
        (void)context;
    }

    [[nodiscard]] static QJsonObject listenNotification(const QJsonValue& subscriptionId,
                                                        std::string_view method,
                                                        QJsonObject params) {
        QJsonObject meta;
        meta.insert(latin1(protocol::meta::subscriptionId), subscriptionId);
        params.insert(latin1(protocol::meta::key), meta);
        return notificationMessage(latin1(method), params);
    }

    // ---- shared methods ----------------------------------------------------------------------

    // Methods of both dialects; false when `method` is not one of them.
    bool dispatchShared(const Incoming& in, RequestContext& context) {
        const QString& method = in.message.method;
        if (is(method, protocol::method::toolsList)) {
            QJsonArray tools;
            for (const ToolSpec& spec : registry.tools()) {
                tools.append(toolJson(spec));
            }
            QJsonObject result;
            result.insert(QStringLiteral("tools"), tools);
            replyResult(in, context, result, protocol::cache::toolsListTtlMs);
        } else if (is(method, protocol::method::toolsCall)) {
            callTool(in, context);
        } else if (is(method, protocol::method::resourcesList)) {
            listResources(in, context);
        } else if (is(method, protocol::method::resourcesTemplatesList)) {
            QJsonArray templates;
            for (const ResourceTemplateSpec& spec : registry.resourceTemplates()) {
                QJsonObject item;
                item.insert(QStringLiteral("uriTemplate"), latin1(spec.uriTemplate));
                insertDescriptive(item, spec.name, spec.title, spec.description, spec.mimeType);
                templates.append(item);
            }
            QJsonObject result;
            result.insert(QStringLiteral("resourceTemplates"), templates);
            replyResult(in, context, result, protocol::cache::resourceTemplatesListTtlMs);
        } else if (is(method, protocol::method::resourcesRead)) {
            readResource(in, context);
        } else if (is(method, protocol::method::promptsList)) {
            listPrompts(in, context);
        } else if (is(method, protocol::method::promptsGet)) {
            getPrompt(in, context);
        } else {
            return false;
        }
        return true;
    }

    static void insertDescriptive(QJsonObject& item, std::string_view name, std::string_view title,
                                  std::string_view description, std::string_view mimeType) {
        item.insert(QStringLiteral("name"),
                    QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())));
        if (!title.empty()) {
            item.insert(QStringLiteral("title"),
                        QString::fromUtf8(title.data(), static_cast<qsizetype>(title.size())));
        }
        if (!description.empty()) {
            item.insert(
                QStringLiteral("description"),
                QString::fromUtf8(description.data(), static_cast<qsizetype>(description.size())));
        }
        if (!mimeType.empty()) {
            item.insert(QStringLiteral("mimeType"), latin1(mimeType));
        }
    }

    static QJsonObject toolJson(const ToolSpec& spec) {
        QJsonObject tool;
        tool.insert(QStringLiteral("name"), latin1(spec.name));
        if (!spec.title.empty()) {
            tool.insert(
                QStringLiteral("title"),
                QString::fromUtf8(spec.title.data(), static_cast<qsizetype>(spec.title.size())));
        }
        tool.insert(QStringLiteral("description"),
                    QString::fromUtf8(spec.description.data(),
                                      static_cast<qsizetype>(spec.description.size())));
        tool.insert(QStringLiteral("inputSchema"), spec.inputSchema);
        if (!spec.outputSchema.isEmpty()) {
            tool.insert(QStringLiteral("outputSchema"), spec.outputSchema);
        }
        QJsonObject annotations;
        annotations.insert(QStringLiteral("readOnlyHint"), spec.annotations.readOnlyHint);
        annotations.insert(QStringLiteral("destructiveHint"), spec.annotations.destructiveHint);
        annotations.insert(QStringLiteral("idempotentHint"), spec.annotations.idempotentHint);
        annotations.insert(QStringLiteral("openWorldHint"), spec.annotations.openWorldHint);
        tool.insert(QStringLiteral("annotations"), annotations);
        return tool;
    }

    void listResources(const Incoming& in, const RequestContext& context) const {
        QJsonArray resources;
        for (const ResourceSpec& spec : registry.resources()) {
            QJsonObject item;
            item.insert(QStringLiteral("uri"), latin1(spec.uri));
            insertDescriptive(item, spec.name, spec.title, spec.description, spec.mimeType);
            resources.append(item);
        }
        if (registry.resourceLister()) {
            for (const ResourceInfo& info : registry.resourceLister()(context)) {
                QJsonObject item;
                item.insert(QStringLiteral("uri"), info.uri);
                item.insert(QStringLiteral("name"), info.name);
                if (!info.title.isEmpty()) {
                    item.insert(QStringLiteral("title"), info.title);
                }
                if (!info.description.isEmpty()) {
                    item.insert(QStringLiteral("description"), info.description);
                }
                if (!info.mimeType.isEmpty()) {
                    item.insert(QStringLiteral("mimeType"), info.mimeType);
                }
                resources.append(item);
            }
        }
        QJsonObject result;
        result.insert(QStringLiteral("resources"), resources);
        replyResult(in, context, result, protocol::cache::resourcesListTtlMs);
    }

    [[nodiscard]] static RpcError resourceNotFound(Dialect dialect, const QString& uri) {
        QJsonObject data;
        data.insert(QStringLiteral("uri"), uri);
        return rpcError(dialect == Dialect::Legacy ? protocol::errc::legacyResourceNotFound
                                                   : protocol::errc::invalidParams,
                        QStringLiteral("Resource not found"), data);
    }

    void readResource(const Incoming& in, const RequestContext& context) const {
        const QJsonValue uriValue = in.message.params.value(QStringLiteral("uri"));
        if (!uriValue.isString()) {
            replyError(in,
                       rpcError(protocol::errc::invalidParams,
                                QStringLiteral("Invalid params: /uri is required"),
                                pointerData(QStringLiteral("/uri"))),
                       ReplyStatus::Ok);
            return;
        }
        const QString uri = uriValue.toString();
        const auto match = registry.matchResource(uri);
        if (!match) {
            replyError(in, resourceNotFound(context.dialect, uri), ReplyStatus::Ok);
            return;
        }
        const ResourceReadResult read = (*match->reader)(context, uri, match->variables);
        switch (read.status) {
        case ResourceReadResult::Status::NotFound:
            replyError(in, resourceNotFound(context.dialect, uri), ReplyStatus::Ok);
            return;
        case ResourceReadResult::Status::Failed: {
            QJsonObject data;
            const Error error = read.error.value_or(makeError(ErrorCode::InternalUnexpected));
            data.insert(QStringLiteral("error"), hio::toJson(error));
            replyError(in, rpcError(protocol::errc::internalError, toQString(error.message), data),
                       ReplyStatus::Ok);
            return;
        }
        case ResourceReadResult::Status::Ok:
            break;
        }
        if (read.contents.empty()) {
            replyError(in, resourceNotFound(context.dialect, uri), ReplyStatus::Ok);
            return;
        }
        QJsonArray contents;
        for (const ResourceContent& content : read.contents) {
            QJsonObject item;
            item.insert(QStringLiteral("uri"), content.uri);
            if (!content.mimeType.isEmpty()) {
                item.insert(QStringLiteral("mimeType"), content.mimeType);
            }
            item.insert(QStringLiteral("text"), content.text);
            contents.append(item);
        }
        QJsonObject result;
        result.insert(QStringLiteral("contents"), contents);
        replyResult(in, context, result, protocol::cache::resourcesReadTtlMs);
    }

    void listPrompts(const Incoming& in, const RequestContext& context) const {
        QJsonArray prompts;
        for (const PromptSpec& spec : registry.prompts()) {
            QJsonObject item;
            insertDescriptive(item, spec.name, spec.title, spec.description, {});
            QJsonArray arguments;
            for (const PromptArgument& argument : spec.arguments) {
                QJsonObject arg;
                insertDescriptive(arg, argument.name, {}, argument.description, {});
                arg.insert(QStringLiteral("required"), argument.required);
                arguments.append(arg);
            }
            item.insert(QStringLiteral("arguments"), arguments);
            prompts.append(item);
        }
        QJsonObject result;
        result.insert(QStringLiteral("prompts"), prompts);
        replyResult(in, context, result, protocol::cache::promptsListTtlMs);
    }

    void getPrompt(const Incoming& in, const RequestContext& context) const {
        const auto invalid = [&in](const QString& pointer, const QString& what) {
            replyError(in,
                       rpcError(protocol::errc::invalidParams,
                                QStringLiteral("Invalid params: %1 %2").arg(pointer, what),
                                pointerData(pointer)),
                       ReplyStatus::Ok);
        };
        const QJsonValue name = in.message.params.value(QStringLiteral("name"));
        if (!name.isString()) {
            invalid(QStringLiteral("/name"), QStringLiteral("is required"));
            return;
        }
        const PromptSpec* spec = registry.findPrompt(name.toString().toStdString());
        if (spec == nullptr) {
            invalid(QStringLiteral("/name"), QStringLiteral("names an unknown prompt"));
            return;
        }
        const QJsonValue argumentsValue = in.message.params.value(QStringLiteral("arguments"));
        if (!argumentsValue.isUndefined() && !argumentsValue.isObject()) {
            invalid(QStringLiteral("/arguments"), QStringLiteral("must be an object"));
            return;
        }
        const QJsonObject argumentsObject = argumentsValue.toObject();
        std::map<QString, QString> arguments;
        for (auto it = argumentsObject.constBegin(); it != argumentsObject.constEnd(); ++it) {
            const bool known = std::ranges::any_of(spec->arguments, [&it](const PromptArgument& a) {
                return latin1(a.name) == it.key();
            });
            if (!known) {
                invalid(QStringLiteral("/arguments/") + it.key(), QStringLiteral("is not allowed"));
                return;
            }
            if (!it.value().isString()) {
                invalid(QStringLiteral("/arguments/") + it.key(),
                        QStringLiteral("must be a string"));
                return;
            }
            arguments.emplace(it.key(), it.value().toString());
        }
        for (const PromptArgument& argument : spec->arguments) {
            if (argument.required && !arguments.contains(latin1(argument.name))) {
                invalid(QStringLiteral("/arguments/") + latin1(argument.name),
                        QStringLiteral("is required"));
                return;
            }
        }
        const auto prompt = spec->get(context, arguments);
        if (!prompt) {
            QJsonObject data;
            data.insert(QStringLiteral("error"), hio::toJson(prompt.error()));
            replyError(
                in,
                rpcError(protocol::errc::internalError, toQString(prompt.error().message), data),
                ReplyStatus::Ok);
            return;
        }
        QJsonArray messages;
        for (const PromptMessage& message : prompt->messages) {
            QJsonObject content;
            content.insert(QStringLiteral("type"), QStringLiteral("text"));
            content.insert(QStringLiteral("text"), message.text);
            QJsonObject item;
            item.insert(QStringLiteral("role"), message.role);
            item.insert(QStringLiteral("content"), content);
            messages.append(item);
        }
        QJsonObject result;
        if (!prompt->description.isEmpty()) {
            result.insert(QStringLiteral("description"), prompt->description);
        }
        result.insert(QStringLiteral("messages"), messages);
        replyResult(in, context, result);
    }

    // ---- tool calls --------------------------------------------------------------------------

    [[nodiscard]] static std::optional<QJsonValue> progressToken(const QJsonObject& params) {
        QJsonValue token = params.value(latin1(protocol::meta::key))
                               .toObject()
                               .value(latin1(protocol::meta::progressToken));
        if (isValidRequestId(token)) {
            return token;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string argumentSummary(const QJsonObject& arguments) const {
        const QString text = QString::fromUtf8(
            QJsonDocument(shortenStrings(arguments).toObject()).toJson(QJsonDocument::Compact));
        return shortened(text, static_cast<qsizetype>(limits.activityArgumentChars)).toStdString();
    }

    void recordActivity(const RequestContext& context, const std::string& tool,
                        const QJsonObject& arguments, std::string deviceId, std::string result,
                        const QDateTime& started, std::chrono::milliseconds duration, bool marker) {
        if (deviceId.empty()) {
            deviceId = arguments.value(QStringLiteral("device")).toString().toStdString();
        }
        activity.record(ActivityEntry{.time = started,
                                      .dialect = context.dialect,
                                      .client = context.client.name,
                                      .clientKey = context.clientKey,
                                      .tool = tool,
                                      .deviceId = std::move(deviceId),
                                      .arguments = argumentSummary(arguments),
                                      .result = std::move(result),
                                      .duration = duration,
                                      .marker = marker});
    }

    void callTool(const Incoming& in, RequestContext& context) {
        const QDateTime started = QDateTime::currentDateTimeUtc();
        const QJsonValue nameValue = in.message.params.value(QStringLiteral("name"));
        const QJsonValue argumentsValue = in.message.params.value(QStringLiteral("arguments"));
        const QJsonObject rawArguments = argumentsValue.toObject();
        const std::string toolName = nameValue.toString().toStdString();
        const auto rejectParams = [&](const QString& pointer, const QString& message) {
            replyError(in, rpcError(protocol::errc::invalidParams, message, pointerData(pointer)),
                       ReplyStatus::Ok);
            recordActivity(context, toolName, rawArguments, {}, "rpc.invalid_params", started,
                           std::chrono::milliseconds{0}, false);
        };
        if (idInFlight(in, context)) {
            replyError(in, duplicateId(), ReplyStatus::Ok);
            recordActivity(context, toolName, rawArguments, {}, "rpc.invalid_request", started,
                           std::chrono::milliseconds{0}, false);
            return;
        }
        if (!nameValue.isString()) {
            rejectParams(QStringLiteral("/name"),
                         QStringLiteral("Invalid params: /name is required"));
            return;
        }
        const ToolSpec* spec = registry.findTool(toolName);
        if (spec == nullptr) {
            rejectParams(QStringLiteral("/name"),
                         QStringLiteral("Unknown tool: %1").arg(nameValue.toString()));
            return;
        }
        if (!argumentsValue.isUndefined() && !argumentsValue.isObject()) {
            rejectParams(QStringLiteral("/arguments"),
                         QStringLiteral("Invalid params: /arguments must be an object"));
            return;
        }
        auto validated = validateArguments(spec->inputSchema, rawArguments);
        if (!validated) {
            rejectParams(validated.error().pointer,
                         QStringLiteral("Invalid params: %1 %2")
                             .arg(validated.error().pointer, validated.error().message));
            return;
        }
        if (spec->validate) {
            if (const auto invalid = spec->validate(*validated, limits)) {
                rejectParams(invalid->pointer, QStringLiteral("Invalid params: %1 %2")
                                                   .arg(invalid->pointer, invalid->message));
                return;
            }
        }
        context.tool = toolName;

        if (calls.size() >= limits.maxInFlightCalls) {
            const Error error = makeError(ErrorCode::McpTooManyRequests);
            replyResult(in, context, toolResultJson(toolErrorResult(error)));
            recordActivity(context, toolName, *validated, {}, std::string(idOf(error.code)),
                           started, std::chrono::milliseconds{0}, spec->marker);
            return;
        }

        std::unique_ptr<ToolCall> call = spec->start(context, *validated, env);
        if (!call) {
            call = ToolCall::completed(std::unexpected(
                makeError(ErrorCode::InternalUnexpected, "tool start returned no call")));
        }
        const std::uint64_t key = nextKey++;
        Call entry;
        entry.id = in.message.id;
        entry.reply = in.reply;
        entry.context = context;
        entry.spec = spec;
        entry.call = std::move(call);
        entry.arguments = std::move(*validated);
        entry.started = started;
        entry.elapsed.start();
        entry.connectionId = in.transport.connectionId;
        ToolCall* raw = entry.call.get();
        const bool progressEnabled = context.progressToken.has_value();
        calls.emplace(key, std::move(entry));

        // Watched before the stream starts (see listen(), review R8). A close during the header
        // write may finish the call at once; `raw` stays valid in the graveyard.
        watchChannel(in.reply);
        if (progressEnabled) {
            in.reply->startStream();
        }
        if (raw->isFinished()) {
            onCallFinished(key);
            return;
        }
        QObject::connect(raw, &ToolCall::finished, &q,
                         [this, key](const hio::Result<ToolResult>&) { onCallFinished(key); });
        if (progressEnabled) {
            QObject::connect(
                raw, &ToolCall::progress, &q,
                [this, key](double done, std::optional<double> total, const QString& message) {
                    onProgress(key, done, total, message);
                });
        }
    }

    void onCallFinished(std::uint64_t key) {
        if (destroying) {
            return;
        }
        auto it = calls.find(key);
        if (it == calls.end()) {
            return;
        }
        Call entry = std::move(it->second);
        calls.erase(it);
        if (entry.progressTimer) {
            entry.progressTimer->stop();
        }
        hio::Result<ToolResult> result = entry.call->result().value_or(
            std::unexpected(makeError(ErrorCode::InternalUnexpected)));

        // Safety net (consent.h): a successful call must have been granted its static categories.
        const ConsentCategories required =
            entry.spec->consent & ~ConsentCategories(ConsentCategory::Connect);
        if (result && !result->isError && (required & ~entry.call->grantedConsent()) != 0) {
            qCCritical(hio::log::lcMcp).noquote()
                << "MCP: tool" << latin1(entry.spec->name)
                << "succeeded without the consent its spec requires; result withheld";
            result = std::unexpected(
                makeError(ErrorCode::InternalUnexpected, "consent was not requested"));
        }
        const ToolResult toolResultValue = result ? *result : toolErrorResult(result.error());
        if (!entry.suppressReply && entry.reply->isOpen()) {
            entry.reply->complete(resultResponse(entry.id, decorate(toolResultJson(toolResultValue),
                                                                    entry.context, std::nullopt)),
                                  ReplyMeta{.status = ReplyStatus::Ok, .sessionId = std::nullopt});
        }
        recordActivity(entry.context, entry.context.tool, entry.arguments, entry.call->deviceId(),
                       resultCode(result), entry.started,
                       std::chrono::milliseconds{entry.elapsed.elapsed()}, entry.spec->marker);

        graveyard.push_back(std::move(entry.call));
        if (!purgeScheduled) {
            purgeScheduled = true;
            QTimer::singleShot(0, &q, [this] {
                purgeScheduled = false;
                graveyard.clear();
            });
        }
        if (stopped && calls.empty()) {
            emit q.callsDrained();
        }
    }

    void onProgress(std::uint64_t key, double done, std::optional<double> total,
                    const QString& message) {
        auto it = calls.find(key);
        if (it == calls.end() || it->second.suppressReply || !it->second.context.progressToken) {
            return;
        }
        Call& entry = it->second;
        const std::optional<QJsonValue>& token = entry.context.progressToken;
        if (!token || !(done > entry.lastProgress)) {
            return; // progress must increase
        }
        QJsonObject params;
        params.insert(latin1(protocol::meta::progressToken), *token);
        params.insert(QStringLiteral("progress"), done);
        if (total) {
            params.insert(QStringLiteral("total"), *total);
        }
        if (!message.isEmpty()) {
            params.insert(QStringLiteral("message"), message);
        }
        entry.lastProgress = done;
        const auto interval = limits.progressInterval;
        if (!entry.progressClock.isValid() || entry.progressClock.elapsed() >= interval.count()) {
            entry.progressClock.start();
            entry.pendingProgress.reset();
            // A local reference: a failing write can close the channel and end the call.
            const std::shared_ptr<IReplyChannel> reply = entry.reply;
            reply->notify(notificationMessage(latin1(protocol::method::progress), params));
            return;
        }
        entry.pendingProgress = params;
        if (!entry.progressTimer) {
            entry.progressTimer = std::make_unique<QTimer>();
            entry.progressTimer->setSingleShot(true);
            QObject::connect(entry.progressTimer.get(), &QTimer::timeout, &q,
                             [this, key] { flushProgress(key); });
        }
        if (!entry.progressTimer->isActive()) {
            entry.progressTimer->start(std::chrono::milliseconds{
                std::max<qint64>(0, interval.count() - entry.progressClock.elapsed())});
        }
    }

    void flushProgress(std::uint64_t key) {
        auto it = calls.find(key);
        if (it == calls.end() || it->second.suppressReply) {
            return;
        }
        Call& entry = it->second;
        const std::optional<QJsonObject> pending =
            std::exchange(entry.pendingProgress, std::nullopt);
        if (!pending) {
            return;
        }
        entry.progressClock.start();
        const std::shared_ptr<IReplyChannel> reply = entry.reply; // see onProgress()
        reply->notify(notificationMessage(latin1(protocol::method::progress), *pending));
    }

    // Whether `entry` is in the scope of a request (legacy: the session; modern: the connection).
    // The same scope is used for notifications/cancelled and for duplicate ids.
    [[nodiscard]] static bool sameScope(const Call& entry, Dialect dialect,
                                        const std::string& sessionId, std::uint64_t connectionId) {
        return dialect == Dialect::Legacy
                   ? entry.context.dialect == Dialect::Legacy &&
                         entry.context.clientKey == sessionId
                   : entry.context.dialect == Dialect::Modern && entry.connectionId == connectionId;
    }

    // Review R5: a request whose id is still in flight in the same scope (a running tool call the
    // client has not cancelled, or a modern listen stream) is refused with -32600. Only tool
    // calls and listen streams outlive handle(); every other request is answered at once.
    [[nodiscard]] bool idInFlight(const Incoming& in, const RequestContext& context) const {
        const QJsonValue& id = in.message.id;
        const std::string& session = context.clientKey;
        const std::uint64_t connection = in.transport.connectionId;
        const bool callInFlight = std::ranges::any_of(calls, [&](const auto& item) {
            return !item.second.suppressReply && item.second.id == id &&
                   sameScope(item.second, context.dialect, session, connection);
        });
        return callInFlight ||
               (context.dialect == Dialect::Modern &&
                std::ranges::any_of(listens, [&](const auto& item) {
                    return item.second.connectionId == connection && item.second.id == id;
                }));
    }

    [[nodiscard]] static RpcError duplicateId() {
        return rpcError(protocol::errc::invalidRequest,
                        QStringLiteral("Invalid Request: duplicate id (a request with this id is "
                                       "still in progress)"));
    }

    // notifications/cancelled {requestId}: legacy within the session, modern within the
    // connection (stdio; over HTTP the connection, too - a modern HTTP client cancels by closing
    // the stream, and its notification arrives on another connection, so it matches nothing;
    // accepted in the review of plan 0013).
    void cancelRequest(const QJsonObject& params, Dialect dialect, const std::string& sessionId,
                       std::uint64_t connectionId) {
        const QJsonValue requestId = params.value(QStringLiteral("requestId"));
        if (!isValidRequestId(requestId)) {
            return;
        }
        for (auto& [key, entry] : calls) {
            if (sameScope(entry, dialect, sessionId, connectionId) && entry.id == requestId) {
                entry.suppressReply = true;
                ToolCall* call = entry.call.get();
                call->cancel(); // may finish (and erase the entry) synchronously
                return;
            }
        }
        // A cancelled subscriptions/listen (stdio): end it without a response.
        for (auto it = listens.begin(); it != listens.end(); ++it) {
            if (dialect == Dialect::Modern && it->second.connectionId == connectionId &&
                it->second.id == requestId) {
                listens.erase(it);
                dropUnusedUpdates();
                return;
            }
        }
    }

    // ---- channels, sessions, subscriptions --------------------------------------------------

    void watchChannel(const std::shared_ptr<IReplyChannel>& channel) {
        const QPointer<McpServer> server(&q);
        const IReplyChannel* identityPtr = channel.get();
        channel->setClosedHandler([server, identityPtr](CloseReason reason) {
            if (!server.isNull()) {
                server->m_impl->channelClosed(identityPtr, reason);
            }
        });
    }

    void channelClosed(const IReplyChannel* channel, CloseReason reason) {
        std::vector<ToolCall*> toCancel;
        for (auto& [key, entry] : calls) {
            if (entry.reply.get() != channel) {
                continue;
            }
            // Modern: closing the stream is the cancellation. Legacy: a disconnect is no
            // cancellation (2025-11-25), except a slow client that we dropped ourselves.
            if (entry.context.dialect == Dialect::Modern || reason == CloseReason::SlowClient ||
                reason == CloseReason::Shutdown) {
                entry.suppressReply = true;
                toCancel.push_back(entry.call.get());
            }
        }
        for (ToolCall* call : toCancel) {
            call->cancel();
        }
        std::erase_if(listens, [channel](const auto& entry) {
            return entry.second.channel.get() == channel;
        });
        for (auto& [id, session] : sessions) {
            if (session.stream.get() == channel && session.kind == TransportKind::Http) {
                session.stream.reset();
                session.lastActivity.restart();
            }
        }
        dropUnusedUpdates();
    }

    [[nodiscard]] std::size_t streamCount() const {
        std::size_t count = listens.size();
        for (const auto& [id, session] : sessions) {
            if (session.kind == TransportKind::Http && session.stream) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::size_t subscriptionCount() const {
        std::size_t count = 0;
        for (const auto& [id, session] : sessions) {
            count += session.subscriptions.size();
        }
        for (const auto& [key, entry] : listens) {
            count += entry.uris.size();
        }
        return count;
    }

    [[nodiscard]] bool hasSubscribers(const QString& uri) const {
        return std::ranges::any_of(
                   sessions,
                   [&uri](const auto& s) { return s.second.subscriptions.contains(uri); }) ||
               std::ranges::any_of(listens,
                                   [&uri](const auto& l) { return l.second.uris.contains(uri); });
    }

    void dropUnusedUpdates() {
        std::erase_if(updates, [this](const auto& entry) { return !hasSubscribers(entry.first); });
    }

    ReplyStatus openSessionStream(const TransportInfo& transport,
                                  std::shared_ptr<IReplyChannel> stream) {
        if (!transport.sessionId) {
            return ReplyStatus::BadRequest;
        }
        auto it = sessions.find(*transport.sessionId);
        if (stopped || it == sessions.end()) {
            return ReplyStatus::NotFound;
        }
        Session& session = it->second;
        session.lastActivity.restart();
        std::shared_ptr<IReplyChannel> previous = std::move(session.stream);
        session.stream.reset();
        if (!previous && streamCount() >= limits.maxListenStreams) {
            return ReplyStatus::ServiceUnavailable;
        }
        if (previous) {
            previous->complete(QJsonObject(), ReplyMeta{});
        }
        // Attached and watched before the stream starts (see listen(), review R8). The local
        // reference keeps the channel alive when a close resets session.stream.
        session.stream = std::move(stream);
        const std::shared_ptr<IReplyChannel> attached = session.stream;
        watchChannel(attached);
        attached->startStream();
        return ReplyStatus::Ok;
    }

    ReplyStatus endSession(const TransportInfo& transport) {
        if (!transport.sessionId) {
            return ReplyStatus::BadRequest;
        }
        if (!sessions.contains(*transport.sessionId)) {
            return ReplyStatus::NotFound;
        }
        endSessionById(*transport.sessionId, true);
        return ReplyStatus::Ok;
    }

    // `cancelCalls`: cancel the session's running calls without a response (DELETE, expiry,
    // re-initialize); stop() has cancelled them already and lets them answer.
    void endSessionById(const std::string& id, bool notify, bool cancelCalls = true) {
        auto it = sessions.find(id);
        if (it == sessions.end()) {
            return;
        }
        Session session = std::move(it->second);
        sessions.erase(it);
        if (session.kind == TransportKind::Stdio) {
            stdioSessions.erase(session.connectionId);
        }
        std::vector<ToolCall*> toCancel;
        for (auto& [key, entry] : calls) {
            if (cancelCalls && entry.context.dialect == Dialect::Legacy &&
                entry.context.clientKey == id) {
                entry.suppressReply = true;
                toCancel.push_back(entry.call.get());
            }
        }
        for (ToolCall* call : toCancel) {
            call->cancel();
        }
        if (session.stream && session.kind == TransportKind::Http) {
            session.stream->complete(QJsonObject(), ReplyMeta{});
        }
        dropUnusedUpdates();
        consent.clientGone(id);
        qCInfo(hio::log::lcMcp).noquote()
            << "MCP: legacy session ended for" << QString::fromStdString(session.client.name);
        if (notify) {
            emit q.clientsChanged();
        }
    }

    void sweep() {
        std::vector<std::string> expired;
        for (const auto& [id, session] : sessions) {
            const bool busy = (session.kind == TransportKind::Http && session.stream) ||
                              std::ranges::any_of(calls, [&id](const auto& c) {
                                  return c.second.context.dialect == Dialect::Legacy &&
                                         c.second.context.clientKey == id;
                              });
            if (session.kind == TransportKind::Http && !busy &&
                session.lastActivity.elapsed() >= limits.legacySessionIdle.count()) {
                expired.push_back(id);
            }
        }
        for (const std::string& id : expired) {
            qCInfo(hio::log::lcMcp) << "MCP: legacy session expired after the idle timeout";
            endSessionById(id, true);
        }
        std::vector<std::string> idle;
        for (const auto& [key, client] : modernClients) {
            if (client.lastSeen.elapsed() >= limits.modernClientIdle.count()) {
                idle.push_back(key);
            }
        }
        for (const std::string& key : idle) {
            modernClients.erase(key);
            consent.clientGone(key);
        }
        if (!idle.empty()) {
            emit q.clientsChanged();
        }
    }

    void notifyResourceUpdated(const QString& uri) {
        if (stopped || !hasSubscribers(uri)) {
            return;
        }
        UpdateState& state = updates[uri];
        const qint64 interval = limits.resourceUpdateInterval.count();
        if (!state.sent || state.last.elapsed() >= interval) {
            deliverUpdate(uri);
            return;
        }
        if (!state.timer) {
            state.timer = std::make_unique<QTimer>();
            state.timer->setSingleShot(true);
            QObject::connect(state.timer.get(), &QTimer::timeout, &q,
                             [this, uri] { deliverUpdate(uri); });
        }
        if (!state.timer->isActive()) {
            state.timer->start(
                std::chrono::milliseconds{std::max<qint64>(0, interval - state.last.elapsed())});
        }
    }

    void deliverUpdate(const QString& uri) {
        auto it = updates.find(uri);
        if (it == updates.end()) {
            return;
        }
        it->second.sent = true;
        it->second.last.start();
        if (it->second.timer) {
            it->second.timer->stop();
        }
        QJsonObject params;
        params.insert(QStringLiteral("uri"), uri);
        std::vector<Delivery> deliveries;
        for (const auto& [id, session] : sessions) {
            if (session.stream && session.subscriptions.contains(uri)) {
                deliveries.push_back(Delivery{
                    .channel = session.stream,
                    .message =
                        notificationMessage(latin1(protocol::method::resourcesUpdated), params)});
            }
        }
        for (const auto& [key, entry] : listens) {
            if (entry.uris.contains(uri)) {
                deliveries.push_back(Delivery{
                    .channel = entry.channel,
                    .message =
                        listenNotification(entry.id, protocol::method::resourcesUpdated, params)});
            }
        }
        deliver(deliveries);
    }

    // A notification for one channel; collected first because a failing write can close a channel
    // and change `sessions` / `listens` (re-entrancy through IReplyChannel::setClosedHandler).
    struct Delivery {
        std::shared_ptr<IReplyChannel> channel;
        QJsonObject message;
    };

    static void deliver(const std::vector<Delivery>& deliveries) {
        for (const Delivery& delivery : deliveries) {
            if (delivery.channel->isOpen()) {
                delivery.channel->notify(delivery.message);
            }
        }
    }

    void notifyResourceListChanged() {
        if (stopped) {
            return;
        }
        std::vector<Delivery> deliveries;
        for (const auto& [id, session] : sessions) {
            if (session.stream) {
                deliveries.push_back(
                    Delivery{.channel = session.stream,
                             .message = notificationMessage(
                                 latin1(protocol::method::resourcesListChanged), QJsonObject())});
            }
        }
        for (const auto& [key, entry] : listens) {
            if (entry.resourcesListChanged) {
                deliveries.push_back(
                    Delivery{.channel = entry.channel,
                             .message = listenNotification(
                                 entry.id, protocol::method::resourcesListChanged, QJsonObject())});
            }
        }
        deliver(deliveries);
    }

    void stop() {
        if (stopped) {
            return;
        }
        stopped = true;
        sweepTimer.stop();
        std::vector<ToolCall*> toCancel;
        toCancel.reserve(calls.size());
        for (auto& [key, entry] : calls) {
            toCancel.push_back(entry.call.get());
        }
        for (ToolCall* call : toCancel) {
            call->cancel();
        }
        // Offloaded jobs (ADR-0162): a stop is requested on every running job, queued ones are
        // dropped, the threads are joined. A cancelled call never gets its offloaded result.
        offloader.stopAll();
        // Graceful end of every listen stream (2026-07-28 "Graceful Closure"): a final result
        // with the subscription id (accepted in the review of plan 0013, Q2).
        auto ending = std::move(listens);
        listens.clear();
        for (auto& [key, entry] : ending) {
            QJsonObject meta;
            meta.insert(latin1(protocol::meta::subscriptionId), entry.id);
            QJsonObject result;
            result.insert(latin1(protocol::meta::key), meta);
            RequestContext modern;
            modern.dialect = Dialect::Modern;
            entry.channel->complete(
                resultResponse(entry.id, decorate(result, modern, std::nullopt)), ReplyMeta{});
        }
        std::vector<std::string> ids;
        ids.reserve(sessions.size());
        for (const auto& [id, session] : sessions) {
            ids.push_back(id);
        }
        for (const std::string& id : ids) {
            endSessionById(id, false, false);
        }
        for (const auto& [key, client] : modernClients) {
            consent.clientGone(key);
        }
        modernClients.clear();
        updates.clear();
        emit q.clientsChanged();
        if (calls.empty()) {
            emit q.callsDrained();
        }
    }
};

McpServer::McpServer(Registry registry, IConsentBroker& consent, IActivitySink& activity,
                     ServerOptions options, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this, std::move(registry), consent, activity,
                                                     std::move(options))) {
    const auto shortest =
        std::min(m_impl->limits.legacySessionIdle, m_impl->limits.modernClientIdle);
    const auto interval =
        std::clamp(shortest / 4, std::chrono::milliseconds{10}, std::chrono::milliseconds{60'000});
    m_impl->sweepTimer.setInterval(interval);
    connect(&m_impl->sweepTimer, &QTimer::timeout, this, [this] { m_impl->sweep(); });
    m_impl->sweepTimer.start();
}

McpServer::~McpServer() {
    m_impl->destroying = true;
    m_impl->sweepTimer.stop();
    for (auto& [key, entry] : m_impl->calls) {
        disconnect(entry.call.get(), nullptr, this, nullptr);
        entry.call->cancel();
    }
    for (const auto& [id, session] : m_impl->sessions) {
        m_impl->consent.clientGone(id);
    }
    for (const auto& [key, client] : m_impl->modernClients) {
        m_impl->consent.clientGone(key);
    }
}

void McpServer::handle(const QByteArray& message, std::shared_ptr<IReplyChannel> reply,
                       const TransportInfo& transport) {
    m_impl->handle(message, std::move(reply), transport);
}

ReplyStatus McpServer::openSessionStream(const TransportInfo& transport,
                                         std::shared_ptr<IReplyChannel> stream) {
    return m_impl->openSessionStream(transport, std::move(stream));
}

ReplyStatus McpServer::endSession(const TransportInfo& transport) {
    return m_impl->endSession(transport);
}

void McpServer::stop() { m_impl->stop(); }

void McpServer::notifyResourceUpdated(const QString& uri) { m_impl->notifyResourceUpdated(uri); }

void McpServer::notifyResourceListChanged() { m_impl->notifyResourceListChanged(); }

bool McpServer::isStopped() const noexcept { return m_impl->stopped; }

std::size_t McpServer::activeCalls() const noexcept { return m_impl->calls.size(); }

std::size_t McpServer::legacySessionCount() const noexcept { return m_impl->sessions.size(); }

std::size_t McpServer::subscriptionCount() const noexcept { return m_impl->subscriptionCount(); }

const Registry& McpServer::registry() const noexcept { return m_impl->registry; }

const Limits& McpServer::limits() const noexcept { return m_impl->limits; }

Offloader& McpServer::offloader() noexcept { return m_impl->offloader; }

} // namespace hio::mcp
