#pragma once

// ToolCall - one in-flight MCP tool call (plan/features/0013 "Protocol core", ADR-0097).
//
// A handler is a small asynchronous state machine on the owner thread: ToolSpec::start creates a
// ToolCall subclass, which reacts to session signals and QTimers and ends with finish() exactly
// once. It never blocks and never runs a nested event loop.
//
//   class ResetCall final : public hio::mcp::ToolCall {
//   public:
//       ResetCall(DeviceRef device) : m_device(device) {
//           auto id = m_device.jlink->reset(...);            // async operation
//           if (!id) { finish(std::unexpected(id.error())); return; }
//           connect(m_device.jlink, &DeviceSession::operationFinished, this, ...);
//       }
//   protected:
//       void onCancel() override { (void)m_device.jlink->cancel(); }   // finish() follows
//   };
//
// - finish(Result): an error becomes a tool result with isError (toolErrorResult()); later calls
//   are ignored. finish() may be called from the constructor (the server checks isFinished()).
// - reportProgress(): forwarded to the client only when the request carried a progress token;
//   the server throttles to Limits::progressInterval (4/s, latest value wins) and drops values
//   that do not increase.
// - cancel() (server: notifications/cancelled, closed HTTP stream, slow client, stop()): withdraws
//   a pending consent request and calls onCancel(); the default onCancel() finishes with
//   `cancelled`. A handler that overrides it must still finish (usually with `cancelled`).
// - requestConsent(): asks the IConsentBroker (ToolEnv::consentBroker()) asynchronously; `done` is
//   not called when the call was cancelled meanwhile. Granted categories are remembered
//   (grantedConsent()) for the server's safety net (consent.h).
// - setDeviceId(): the device the call works on, for the activity entry (ADR-0105).
//
// Thread safety: owner thread only.

#include "hio/core/result.h"
#include "hio/mcp/consent.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace hio::mcp {

// The MCP CallToolResult of a call: structuredContent + one text block.
struct ToolResult {
    QJsonObject structured;
    QString text;
    bool isError = false;
};

// structuredContent = `structured`, text = its compact JSON.
[[nodiscard]] ToolResult toolResult(const QJsonObject& structured);
// isError, structuredContent {"error": hio::toJson(error)}, text "<code>: <message> — <suggestion>"
// (" — <suggestion>" left out when empty).
[[nodiscard]] ToolResult toolErrorResult(const hio::Error& error);
// {"content":[{"type":"text","text":...}],"structuredContent":...,"isError":...}
[[nodiscard]] QJsonObject toolResultJson(const ToolResult& result);

class ToolCall : public QObject {
    Q_OBJECT

public:
    ToolCall(const ToolCall&) = delete;
    ToolCall& operator=(const ToolCall&) = delete;
    ToolCall(ToolCall&&) = delete;
    ToolCall& operator=(ToolCall&&) = delete;
    ~ToolCall() override;

    // A call that is already finished (synchronous tools such as list_devices).
    [[nodiscard]] static std::unique_ptr<ToolCall> completed(hio::Result<ToolResult> result);

    // Idempotent; no effect after finish().
    void cancel();

    [[nodiscard]] bool isCancelled() const noexcept { return m_cancelled; }
    [[nodiscard]] bool isFinished() const noexcept { return m_result.has_value(); }
    [[nodiscard]] const std::optional<hio::Result<ToolResult>>& result() const noexcept {
        return m_result;
    }
    [[nodiscard]] const std::string& deviceId() const noexcept { return m_deviceId; }
    [[nodiscard]] ConsentCategories grantedConsent() const noexcept { return m_granted; }

signals:
    void progress(double done, std::optional<double> total, const QString& message);
    void finished(const hio::Result<hio::mcp::ToolResult>& result);

protected:
    ToolCall() = default;

    void finish(hio::Result<ToolResult> result);
    void reportProgress(double done, std::optional<double> total = std::nullopt,
                        const QString& message = {});
    virtual void onCancel();
    void setDeviceId(std::string id) { m_deviceId = std::move(id); }
    // At most one request at a time (a second one is answered with internal.unexpected).
    void requestConsent(IConsentBroker& broker, ConsentRequest request,
                        std::function<void(ConsentDecision)> done);
    [[nodiscard]] bool consentPending() const noexcept { return m_consentPending; }

private:
    std::optional<hio::Result<ToolResult>> m_result;
    std::string m_deviceId;
    ConsentCategories m_granted;
    std::function<void()> m_consentCancel;
    std::uint64_t m_consentToken = 0;
    bool m_consentPending = false;
    bool m_cancelled = false;
};

} // namespace hio::mcp
