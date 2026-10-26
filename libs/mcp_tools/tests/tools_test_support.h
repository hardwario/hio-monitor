#pragma once

// Test doubles for the MCP device tools (plan/features/0013 A4, C1-C3):
//   FakeConsoleSession - an in-process ConsoleSession driven by the test (lines, prompt,
//                        connection), no threads;
//   MockDeviceProvider - GMock IDeviceProvider;
//   FakeProvider       - an IDeviceProvider over FakeConsoleSessions that records markers, busy
//                        state and command hints;
//   Harness            - ToolsContext + registerTools + McpServer, calls tools over the modern
//                        dialect (stdio transport) and pumps the event loop.
// Production code never runs nested loops (ADR-0097); tests pump with runUntil().

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/console_session.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/mcp/json_rpc.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/server.h"
#include "hio/mcp/tools/device_provider.h"
#include "hio/mcp/tools/register_tools.h"
#include "hio/mcp/tools/tools_context.h"
#include "mcp_test_support.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <gmock/gmock.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::mcp::tools::test {

using hio::mcp::test::runFor;
using hio::mcp::test::runUntil;

// A CHESTER copy whose app target has an RTT shell prompt (ADR-0104; CHESTER's is empty).
inline const hio::device::DeviceProfile& promptProfile() {
    static const std::array<hio::device::TargetProfile, 2> targets = [] {
        const auto* chester = hio::device::ProfileRegistry::find("chester");
        std::array<hio::device::TargetProfile, 2> copy{chester->targets[0], chester->targets[1]};
        copy[0].shellPrompt = "uart:~$ ";
        return copy;
    }();
    static const hio::device::DeviceProfile profile = [] {
        hio::device::DeviceProfile copy = *hio::device::ProfileRegistry::find("chester");
        copy.targets = targets;
        return copy;
    }();
    return profile;
}

inline const hio::device::DeviceProfile& chesterProfile() {
    return *hio::device::ProfileRegistry::find("chester");
}

class FakeConsoleSession final : public hio::device::ConsoleSession {
public:
    explicit FakeConsoleSession(
        const hio::device::DeviceProfile& profile = chesterProfile(),
        std::size_t logCapacity = hio::device::ConsoleLog::defaultCapacityPerChannel,
        hio::device::Transport transport = hio::device::Transport::JLink)
        : m_profile(&profile), m_log(std::make_shared<hio::device::ConsoleLog>(logCapacity)),
          m_transport(transport) {
        m_connection.state = hio::device::ConnectionState::Connected;
        m_connection.transport = transport;
    }

    // The lines sent, in order.
    [[nodiscard]] const std::vector<std::string>& sent() const { return m_sent; }
    [[nodiscard]] int reconnects() const { return m_reconnects; }

    hio::Status sendLine(std::string line) override {
        if (hio::Status held; holdLine(line, held)) {
            return held; // plan 0021: a silent shell query runs
        }
        m_sent.push_back(line);
        lineQueued();
        if (m_echo) { // like the real sessions: the line is echoed (dir In) once written
            emitLines({{hio::device::Channel::Shell, line}}, hio::device::Direction::In,
                      sendingOrigin());
        }
        return {};
    }
    hio::Status reconnect() override {
        ++m_reconnects;
        return {};
    }
    [[nodiscard]] const hio::device::ConsoleLog& log() const override { return *m_log; }
    [[nodiscard]] hio::device::ConsoleLog& mutableLog() { return *m_log; }
    [[nodiscard]] std::shared_ptr<hio::device::ConsoleLog> sharedLog() { return m_log; }
    [[nodiscard]] const hio::device::ConnectionEvent& connection() const override {
        return m_connection;
    }
    [[nodiscard]] hio::device::SessionIdentity identity() const override {
        return {.transport = m_transport, .id = "1", .displayName = "Fake 1"};
    }
    [[nodiscard]] hio::device::Capabilities capabilities() const override { return {}; }
    [[nodiscard]] const hio::device::DeviceProfile& profile() const override { return *m_profile; }
    [[nodiscard]] std::string_view consoleTargetId() const override { return "app"; }
    [[nodiscard]] std::filesystem::path consoleFile() const override { return {}; }

    // Appends lines to the log and emits them as one batch (like the real sessions).
    // A silent shell capture (plan 0021) takes its lines first, like in the real sessions.
    void emitLines(const std::vector<std::pair<hio::device::Channel, std::string>>& lines,
                   hio::device::Direction dir = hio::device::Direction::Out,
                   std::uint64_t origin = 0) {
        QList<hio::device::ConsoleLine> batch;
        for (const auto& [channel, text] : lines) {
            hio::device::ConsoleLine line;
            line.channel = channel;
            line.dir = dir;
            line.time = std::chrono::system_clock::now();
            line.level = channel == hio::device::Channel::Log ? hio::device::LogLevel::Info
                                                              : hio::device::LogLevel::None;
            line.text = text;
            line.origin = origin;
            if (captureLine(line)) {
                continue;
            }
            line.seq = m_log->append(line);
            batch.push_back(line);
        }
        if (!batch.isEmpty()) {
            emit linesReceived(batch);
        }
    }
    void emitLine(hio::device::Channel channel, hio::device::Direction dir,
                  const std::string& text) {
        emitLines({{channel, text}}, dir);
    }
    void shell(const std::string& text) {
        emitLine(hio::device::Channel::Shell, hio::device::Direction::Out, text);
    }
    void logLine(const std::string& text) {
        emitLine(hio::device::Channel::Log, hio::device::Direction::Out, text);
    }
    void prompt() { emit promptSeen(); }
    void setConnection(hio::device::ConnectionState state,
                       std::optional<hio::Error> error = std::nullopt) {
        m_connection.state = state;
        m_connection.error = std::move(error);
        emit connectionChanged(m_connection);
    }

protected:
    void closeSession() override {}

private:
    const hio::device::DeviceProfile* m_profile;
    std::shared_ptr<hio::device::ConsoleLog> m_log;
    hio::device::Transport m_transport;
    hio::device::ConnectionEvent m_connection;
    std::vector<std::string> m_sent;
    int m_reconnects = 0;
    bool m_echo = true;
};

class MockDeviceProvider : public IDeviceProvider {
public:
    MOCK_METHOD(std::vector<DeviceEntry>, list, (), (override));
    MOCK_METHOD(void, refresh, (std::function<void(hio::Status)> done), (override));
    MOCK_METHOD(bool, firstScanPending, (), (const, override));
    MOCK_METHOD(void, open,
                (const OpenRequest& request, std::function<void(hio::Result<DeviceRef>)> done),
                (override));
    MOCK_METHOD(hio::Status, close, (std::string_view id, const hio::mcp::RequestContext& context),
                (override));
    MOCK_METHOD(std::optional<DeviceRef>, find, (std::string_view id), (override));
    MOCK_METHOD(void, beginBusy, (std::string_view id, QString text, std::function<void()> cancel),
                (override));
    MOCK_METHOD(void, endBusy, (std::string_view id), (override));
    MOCK_METHOD(void, marker, (std::string_view id, QString text), (override));
    MOCK_METHOD(void, setAiCommandHint, (std::string_view id, bool active), (override));
    DeviceProviderNotifier& notifier() override { return m_notifier; }

private:
    DeviceProviderNotifier m_notifier;
};

// Records what the tools tell a provider (shared by FakeProvider and the session provider).
struct ProviderRecord {
    std::vector<std::pair<std::string, QString>> markers;
    std::vector<std::pair<std::string, QString>> busy; // beginBusy
    int endBusy = 0;
    std::vector<std::pair<std::string, bool>> hints;
    std::function<void()> busyCancel; // the last beginBusy's cancel functor

    [[nodiscard]] std::vector<QString> markerTexts() const {
        std::vector<QString> texts;
        texts.reserve(markers.size());
        for (const auto& [id, text] : markers) {
            texts.push_back(text);
        }
        return texts;
    }
};

// IDeviceProvider over FakeConsoleSessions (no J-Link: DeviceRef::jlink stays null).
class FakeProvider final : public IDeviceProvider {
public:
    [[nodiscard]] ProviderRecord& record() { return m_record; }

    FakeConsoleSession&
    add(const std::string& id, const hio::device::DeviceProfile& profile = chesterProfile(),
        std::size_t logCapacity = hio::device::ConsoleLog::defaultCapacityPerChannel) {
        auto session = std::make_unique<FakeConsoleSession>(profile, logCapacity);
        FakeConsoleSession& ref = *session;
        m_sessions[id] = std::move(session);
        emit m_notifier.devicesChanged();
        return ref;
    }
    void remove(const std::string& id) {
        m_sessions.erase(id);
        emit m_notifier.devicesChanged();
    }

    std::vector<DeviceEntry> list() override {
        std::vector<DeviceEntry> entries;
        entries.reserve(m_sessions.size());
        for (const auto& [id, session] : m_sessions) {
            entries.push_back(DeviceEntry{.id = id,
                                          .kind = "jlink",
                                          .displayName = id,
                                          .profileId = "chester",
                                          .state = DeviceEntry::State::Open,
                                          .owner = std::nullopt,
                                          .connection = session->connection(),
                                          .openedByMcp = true});
        }
        return entries;
    }
    void refresh(std::function<void(hio::Status)> done) override { done(hio::Status{}); }
    void open(const OpenRequest& /*request*/,
              std::function<void(hio::Result<DeviceRef>)> done) override {
        done(std::unexpected(makeError(ErrorCode::UsageInvalidArgument)));
    }
    hio::Status close(std::string_view id, const hio::mcp::RequestContext& /*context*/) override {
        if (m_sessions.erase(std::string(id)) == 0) {
            return std::unexpected(makeError(ErrorCode::McpDeviceNotOpen, std::string(id)));
        }
        emit m_notifier.devicesChanged();
        return {};
    }
    std::optional<DeviceRef> find(std::string_view id) override {
        const auto it = m_sessions.find(std::string(id));
        if (it == m_sessions.end()) {
            return std::nullopt;
        }
        return DeviceRef{.console = it->second.get(), .jlink = nullptr};
    }
    void beginBusy(std::string_view id, QString text, std::function<void()> cancel) override {
        m_record.busy.emplace_back(id, std::move(text));
        m_record.busyCancel = std::move(cancel);
    }
    void endBusy(std::string_view /*id*/) override { ++m_record.endBusy; }
    void marker(std::string_view id, QString text) override {
        if (auto it = m_sessions.find(std::string(id)); it != m_sessions.end()) {
            it->second->mutableLog().appendMarker(hio::device::Channel::Shell, text.toStdString());
        }
        m_record.markers.emplace_back(id, std::move(text));
    }
    void setAiCommandHint(std::string_view id, bool active) override {
        m_record.hints.emplace_back(id, active);
    }
    DeviceProviderNotifier& notifier() override { return m_notifier; }

private:
    std::map<std::string, std::unique_ptr<FakeConsoleSession>> m_sessions;
    DeviceProviderNotifier m_notifier;
    ProviderRecord m_record;
};

// ---- server harness -----------------------------------------------------------------------------

inline QJsonObject modernMeta(const QJsonObject& extra = {}) {
    QJsonObject meta{
        {QStringLiteral("io.modelcontextprotocol/protocolVersion"), QStringLiteral("2026-07-28")},
        {QStringLiteral("io.modelcontextprotocol/clientCapabilities"), QJsonObject()},
        {QStringLiteral("io.modelcontextprotocol/clientInfo"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("tester")},
                     {QStringLiteral("version"), QStringLiteral("1")}}}};
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        meta.insert(it.key(), it.value());
    }
    return meta;
}

inline QByteArray request(const QJsonValue& id, const QString& method, QJsonObject params = {},
                          const QJsonObject& metaExtra = {}) {
    params.insert(QStringLiteral("_meta"), modernMeta(metaExtra));
    QJsonObject message{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                        {QStringLiteral("method"), method},
                        {QStringLiteral("params"), params}};
    if (!id.isUndefined()) {
        message.insert(QStringLiteral("id"), id);
    }
    return hio::mcp::serialize(message);
}

// One tools/call in flight.
struct PendingCall {
    std::shared_ptr<hio::mcp::test::RecordingReplyChannel> reply;

    [[nodiscard]] bool done() const { return reply->done(); }
    [[nodiscard]] const QJsonObject& response() const { return reply->response; }
    [[nodiscard]] QJsonObject result() const {
        return reply->response.value(QStringLiteral("result")).toObject();
    }
    [[nodiscard]] QJsonObject structured() const {
        return result().value(QStringLiteral("structuredContent")).toObject();
    }
    [[nodiscard]] bool isError() const {
        return result().value(QStringLiteral("isError")).toBool();
    }
    [[nodiscard]] QJsonObject error() const {
        return structured().value(QStringLiteral("error")).toObject();
    }
    [[nodiscard]] std::string errorCode() const {
        return error().value(QStringLiteral("code")).toString().toStdString();
    }
    [[nodiscard]] std::string errorDetail() const {
        return error().value(QStringLiteral("detail")).toString().toStdString();
    }
    // JSON-RPC error number (0 when the response is a result).
    [[nodiscard]] int rpcError() const {
        return reply->response.value(QStringLiteral("error"))
            .toObject()
            .value(QStringLiteral("code"))
            .toInt();
    }
    [[nodiscard]] QString rpcPointer() const {
        return reply->response.value(QStringLiteral("error"))
            .toObject()
            .value(QStringLiteral("data"))
            .toObject()
            .value(QStringLiteral("pointer"))
            .toString();
    }
    [[nodiscard]] std::size_t progressCount() const { return reply->notifications.size(); }
};

class Harness {
public:
    explicit Harness(IDeviceProvider& provider, const hio::mcp::Limits& limits = {},
                     hio::firmware::FirmwareCatalog* catalog = nullptr, ToolsOptions options = {})
        : tools(std::make_unique<ToolsContext>(provider, catalog, std::move(options))) {
        hio::mcp::Registry registry;
        registered = registerTools(registry, *tools);
        hio::mcp::ServerOptions serverOptions;
        serverOptions.limits = limits;
        serverOptions.version = QStringLiteral("test");
        server = std::make_unique<hio::mcp::McpServer>(std::move(registry), consent, activity,
                                                       std::move(serverOptions));
        tools->attachServer(*server);
    }
    ~Harness() {
        server.reset();
        tools.reset();
    }
    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;
    Harness(Harness&&) = delete;
    Harness& operator=(Harness&&) = delete;

    hio::mcp::test::FakeConsentBroker consent;
    hio::mcp::test::RecordingActivitySink activity;
    std::unique_ptr<ToolsContext> tools;
    std::unique_ptr<hio::mcp::McpServer> server;
    hio::Status registered;

    // Sends tools/call (with a progress token when `progress`) and returns at once.
    PendingCall start(const QString& tool, const QJsonObject& arguments = {},
                      bool progress = false) {
        PendingCall call{std::make_shared<hio::mcp::test::RecordingReplyChannel>(true, false)};
        QJsonObject meta;
        if (progress) {
            meta.insert(QStringLiteral("progressToken"), nextId);
        }
        server->handle(request(nextId++, QStringLiteral("tools/call"),
                               QJsonObject{{QStringLiteral("name"), tool},
                                           {QStringLiteral("arguments"), arguments}},
                               meta),
                       call.reply, transport());
        return call;
    }

    // Calls a tool and waits for its response.
    PendingCall call(const QString& tool, const QJsonObject& arguments = {},
                     std::chrono::milliseconds timeout = std::chrono::seconds(20),
                     bool progress = false) {
        PendingCall pending = start(tool, arguments, progress);
        (void)runUntil([&pending] { return pending.done(); }, timeout);
        return pending;
    }

    // Any request; the response.
    QJsonObject rpc(const QString& method, const QJsonObject& params = {}) {
        auto reply = std::make_shared<hio::mcp::test::RecordingReplyChannel>(true, false);
        server->handle(request(nextId++, method, params), reply, transport());
        (void)runUntil([&reply] { return reply->done(); });
        return reply->response;
    }

    // Cancels request `id` (notifications/cancelled).
    void cancel(int id) const {
        auto reply = std::make_shared<hio::mcp::test::RecordingReplyChannel>(true, false);
        server->handle(request(QJsonValue(QJsonValue::Undefined),
                               QStringLiteral("notifications/cancelled"),
                               QJsonObject{{QStringLiteral("requestId"), id}}),
                       reply, transport());
    }

    [[nodiscard]] int lastId() const { return nextId - 1; }

    int nextId = 1; // the next JSON-RPC request id

private:
    static hio::mcp::TransportInfo transport() {
        hio::mcp::TransportInfo info;
        info.kind = hio::mcp::TransportKind::Stdio;
        info.connectionId = 7;
        return info;
    }
};

inline QJsonObject args(std::initializer_list<std::pair<const char*, QJsonValue>> values) {
    QJsonObject json;
    for (const auto& [name, value] : values) {
        json.insert(QLatin1StringView(name), value);
    }
    return json;
}

} // namespace hio::mcp::tools::test
