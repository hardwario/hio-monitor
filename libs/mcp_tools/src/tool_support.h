#pragma once

// Shared parts of the MCP device tools (plan/features/0013 C1-C3): JSON helpers, the resolution of
// the `device` / `target` arguments (ADR-0098), console markers (ADR-0105), and the ToolCall bases
// DeviceCall (QPointer checks) and OperationCall (one DeviceSession operation at a time: consent,
// busy check, markers, beginBusy/endBusy, progress, stall timeout, cancellation).
//
// Thread safety: owner thread only.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/console_session.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/schema.h"
#include "hio/mcp/tool_call.h"
#include "hio/mcp/tools/device_provider.h"
#include "hio/mcp/tools/tools_context.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::mcp::tools::detail {

// ---- text and JSON ------------------------------------------------------------------------------

[[nodiscard]] QString qs(std::string_view utf8);
// ISO 8601 UTC with milliseconds ("2026-10-20T10:00:00.123Z").
[[nodiscard]] QString isoTime(std::chrono::system_clock::time_point time);
[[nodiscard]] double seconds(std::chrono::milliseconds duration);
[[nodiscard]] QJsonValue seqJson(std::uint64_t seq);
// {state, error (hio::toJson or null), target_mv (or null)}
[[nodiscard]] QJsonObject connectionJson(const hio::device::ConnectionEvent& event);
// {id, kind, display_name, profile, state, owner {app, pid} | null, connection | null,
//  opened_by_mcp}
[[nodiscard]] QJsonObject deviceEntryJson(const DeviceEntry& entry);
// status: {id, connection, terminal_total, terminal_buffered, log_total, log_buffered,
// buffer_size, busy_operation (operationKindId or null), paused_by_operation}
[[nodiscard]] QJsonObject deviceStatusJson(const std::string& id, const DeviceRef& ref);
[[nodiscard]] QJsonObject deviceStatusSchema();
// {devices: [deviceEntryJson]}
[[nodiscard]] QJsonObject devicesJson(IDeviceProvider& provider);
[[nodiscard]] QJsonObject deviceEntrySchema();
// read_terminal: {seq, dir, text, time}; read_log: {seq, level, text, time}.
[[nodiscard]] QJsonObject terminalLineJson(const hio::device::ConsoleLine& line);
[[nodiscard]] QJsonObject logLineJson(const hio::device::ConsoleLine& line);
// Classic hexdump lines "0x%08X: xx xx ... |ascii|" (16 bytes per line, rttt _hexdump).
[[nodiscard]] QJsonArray hexdump(std::uint32_t address, std::span<const std::byte> bytes);
[[nodiscard]] QString hexString(std::span<const std::byte> bytes);
// Hex digits (whitespace ignored) to bytes; nullopt for odd counts or other characters.
[[nodiscard]] std::optional<std::vector<std::byte>> decodeHex(const QString& text);

// ---- JSON Schema helpers (output schemas) -------------------------------------------------------

[[nodiscard]] QJsonObject typed(const char* type, const char* description = nullptr);
// {"type": [type, "null"]}
[[nodiscard]] QJsonObject nullable(const char* type);
[[nodiscard]] QJsonObject arrayOf(const QJsonObject& items);
[[nodiscard]] QJsonObject objectOf(std::initializer_list<std::pair<const char*, QJsonObject>> props,
                                   const QStringList& required = {});
[[nodiscard]] QJsonObject connectionSchema();
// Adds `"enum": values` to property `name` of an object schema built with Schema (numbers).
[[nodiscard]] QJsonObject withNumberEnum(QJsonObject schema, const QString& name,
                                         const QJsonArray& values);

// ---- argument helpers ---------------------------------------------------------------------------

// The common optional `device` argument (ADR-0098).
[[nodiscard]] hio::mcp::Schema deviceArgument();
// The common optional `target` argument.
[[nodiscard]] hio::mcp::Schema targetArgument();
[[nodiscard]] std::optional<hio::mcp::ValidationError> invalid(const QString& name,
                                                               const QString& message);

struct ResolvedDevice {
    std::string id;
    DeviceRef ref;
};

// The `device` argument, or the only open device (mcp.device_ambiguous with the open ids in
// detail when several are open; mcp.device_not_open when none is or the id is not open).
[[nodiscard]] hio::Result<ResolvedDevice> resolveDevice(IDeviceProvider& provider,
                                                        const QJsonObject& arguments);
// resolveDevice() + a J-Link session (mcp.capability_missing for BLE).
[[nodiscard]] hio::Result<ResolvedDevice> resolveJLinkDevice(IDeviceProvider& provider,
                                                             const QJsonObject& arguments);
// The `target` argument (default: the session's consoleTargetId()); usage.invalid_argument for a
// target the profile does not have.
[[nodiscard]] hio::Result<const hio::device::TargetProfile*>
resolveTarget(const hio::device::ConsoleSession& session, const QJsonObject& arguments);

[[nodiscard]] hio::Error deviceNotOpen(const std::string& id);
[[nodiscard]] hio::Error capabilityMissing(const std::string& id);
[[nodiscard]] hio::Error invalidArgument(const QString& message, std::string detail);
// internal.busy with the running operation's kind (operationKindId) in detail.
[[nodiscard]] hio::Error busyError(hio::device::OperationKind kind);
// The running operation pauses RTT (it is not a light operation, ADR-0157).
[[nodiscard]] bool pausedByOperation(const DeviceRef& ref);

// An absolute path to a regular file of at most `maxBytes` (ADR-0100). Errors:
// usage.invalid_argument (relative, not a regular file, too large), io.file_not_found.
[[nodiscard]] hio::Result<std::filesystem::path> inputFile(const QString& path,
                                                           std::uint64_t maxBytes);
[[nodiscard]] hio::Result<std::vector<std::byte>> readInputFile(const std::filesystem::path& path,
                                                                std::uint64_t maxBytes);

// ---- markers (ADR-0105) -------------------------------------------------------------------------

// "— AI (<client>): <action> —"
[[nodiscard]] QString markerStart(const hio::mcp::RequestContext& context, const QString& action);
// "— AI: <tool> done in <s> s —"
[[nodiscard]] QString markerDone(std::string_view tool, std::chrono::milliseconds duration);
// "— AI: <tool> failed: <message> —"
[[nodiscard]] QString markerFailed(std::string_view tool, const hio::Error& error);
// "— AI: <tool> cancelled —"
[[nodiscard]] QString markerCancelled(std::string_view tool);
// The busy text of beginBusy ("AI: Flashing…").
[[nodiscard]] QString busyText(std::string_view tool);

// ---- registration -------------------------------------------------------------------------------

[[nodiscard]] hio::Status addTool(hio::mcp::Registry& registry, hio::mcp::ToolSpec spec);

// A finished call with `result` (synchronous tools).
[[nodiscard]] std::unique_ptr<hio::mcp::ToolCall> done(hio::Result<QJsonObject> result);
[[nodiscard]] std::unique_ptr<hio::mcp::ToolCall> failed(hio::Error error);

// ---- calls --------------------------------------------------------------------------------------

// A call on one device: finishes with mcp.device_not_open as soon as the console session is
// destroyed (QPointer + destroyed()).
class DeviceCall : public hio::mcp::ToolCall {
    Q_OBJECT

public:
    DeviceCall(ToolsContext& tools, hio::mcp::RequestContext context, const hio::mcp::ToolEnv& env,
               ResolvedDevice device);

protected:
    [[nodiscard]] ToolsContext& tools() const noexcept { return *m_tools; }
    [[nodiscard]] IDeviceProvider& provider() const noexcept { return m_tools->provider(); }
    [[nodiscard]] const hio::mcp::RequestContext& context() const noexcept { return m_context; }
    [[nodiscard]] hio::mcp::IConsentBroker& consentBroker() const noexcept { return *m_consent; }
    [[nodiscard]] const hio::mcp::ToolLimits& limits() const noexcept { return m_limits; }
    [[nodiscard]] const std::string& id() const noexcept { return m_id; }
    [[nodiscard]] const DeviceRef& ref() const noexcept { return m_ref; }
    [[nodiscard]] bool alive() const { return m_ref.alive(); }
    // finish() unless already finished.
    void complete(hio::Result<QJsonObject> result);
    void completeError(hio::Error error);
    // Called before the call finishes because the device went away (stop timers etc.).
    virtual void deviceGone() {}

private:
    ToolsContext* m_tools;               // non-owning, outlives the call
    hio::mcp::IConsentBroker* m_consent; // non-owning, the server's broker
    hio::mcp::RequestContext m_context;
    hio::mcp::ToolLimits m_limits;
    std::string m_id;
    DeviceRef m_ref;
};

struct OperationSettings {
    std::string_view tool;
    bool marker = true;    // the start marker (ADR-0105)
    bool busy = true;      // beginBusy / endBusy and the end marker ("done in", "failed")
    bool cursors = false;  // log_cursor_end / terminal_cursor_end in the result (after flashing)
    bool progress = false; // forward operationProgress as MCP progress
};

using OperationStart =
    std::function<hio::Result<hio::device::OperationId>(hio::device::DeviceSession&)>;

// Runs DeviceSession operations one after the other on a J-Link device.
class OperationCall : public DeviceCall {
    Q_OBJECT

public:
    OperationCall(ToolsContext& tools, const hio::mcp::RequestContext& context,
                  const hio::mcp::ToolEnv& env, ResolvedDevice device, OperationSettings settings);
    ~OperationCall() override;
    OperationCall(const OperationCall&) = delete;
    OperationCall& operator=(const OperationCall&) = delete;
    OperationCall(OperationCall&&) = delete;
    OperationCall& operator=(OperationCall&&) = delete;

protected:
    // Asks consent for `categories` (nothing when empty), then calls `next`; a denial finishes.
    void withConsent(hio::mcp::ConsentCategories categories, QJsonObject details,
                     std::function<void()> next);
    // The busy check (internal.busy with the kind) before the first operation, the start marker
    // (`action`) and beginBusy. False when the call finished.
    [[nodiscard]] bool begin(const QString& action);
    // Starts one operation; `then` gets its successful result, errors finish the call (with the
    // end marker). Restarts the stall timer.
    void run(const OperationStart& start,
             std::function<void(const hio::device::OperationResult&)> then);
    // Success: end marker ("done in", busy operations only), cursors when configured, endBusy,
    // finish.
    void succeed(QJsonObject structured, std::chrono::milliseconds duration);
    // Failure: end marker ("failed" / "cancelled"; busy operations only) when begun, endBusy,
    // finish.
    void failWith(hio::Error error);
    // Extra cancellation work before an operation runs (e.g. a catalog download); default none.
    virtual void cancelPending() {}
    [[nodiscard]] hio::device::DeviceSession* session() const { return ref().jlink.data(); }
    [[nodiscard]] std::chrono::milliseconds sinceBegin() const;

    void onCancel() override;
    void deviceGone() override;

private:
    void onProgress(hio::device::OperationId id, const hio::device::OperationProgress& progress);
    void onFinished(hio::device::OperationId id, const hio::device::OperationResult& result);
    void stalled();
    void endBusy();

    OperationSettings m_settings;
    std::optional<hio::device::OperationId> m_operation;
    std::function<void(const hio::device::OperationResult&)> m_then;
    QTimer m_stall;
    std::chrono::steady_clock::time_point m_begun;
    bool m_begunMarker = false;
    bool m_busy = false;
    bool m_cancelling = false;
};

// ---- one-operation tools ------------------------------------------------------------------------

// One DeviceSession operation with consent, markers, busy state and a JSON result.
struct SimpleOperation {
    OperationSettings settings;
    hio::mcp::ConsentCategories consent; // asked before the operation (nothing when empty)
    QJsonObject details;                 // ConsentRequest::details
    QString action;                      // the start marker's action ("reset app")
    OperationStart start;
    std::function<hio::Result<QJsonObject>(const hio::device::OperationResult&)> result;
};

class SimpleOperationCall final : public OperationCall {
    Q_OBJECT

public:
    SimpleOperationCall(ToolsContext& tools, const hio::mcp::RequestContext& context,
                        const hio::mcp::ToolEnv& env, ResolvedDevice device, SimpleOperation op);

private:
    SimpleOperation m_op;
};

// Builds the operation from the validated arguments, the J-Link device and its target.
using OperationBuilder = std::function<hio::Result<SimpleOperation>(
    const QJsonObject& args, const ResolvedDevice& device, const hio::device::TargetProfile& target,
    const hio::mcp::ToolLimits& limits)>;

// ToolSpec::start for a SimpleOperation tool: resolves the J-Link device and the target
// (`fixedTarget` instead of the `target` argument when not empty), then builds and runs it.
[[nodiscard]] hio::mcp::ToolStart operationStart(ToolsContext& context, OperationBuilder builder,
                                                 std::string_view fixedTarget = {});

// Settings of a plain read (no marker, no busy state, ADR-0105).
[[nodiscard]] constexpr OperationSettings readSettings(std::string_view tool) {
    return OperationSettings{
        .tool = tool, .marker = false, .busy = false, .cursors = false, .progress = false};
}

} // namespace hio::mcp::tools::detail
