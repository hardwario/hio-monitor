// Discovery tools (plan/features/0013 C1, ADR-0098): list_devices, connect, disconnect, status.

#include "hio/core/error.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/device/console_session.h"
#include "hio/device/device_session.h"
#include "hio/device/session_events.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/schema.h"
#include "hio/mcp/tool_call.h"
#include "hio/mcp/tools/device_provider.h"
#include "hio/mcp/tools/tools_context.h"
#include "tool_support.h"
#include "tools/tools.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringLiteral>
#include <QTimer>

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace hio::mcp::tools::detail {

namespace {

using hio::mcp::Required;
using hio::mcp::Schema;

constexpr const char* listDevicesDescription =
    "List the devices this server can use: connected (open) devices and the J-Link probes found "
    "by the last enumeration, with their id (\"jlink:<serial>\" or \"ble:<id>\"), state (open, "
    "available, in_use_by_other_process with the owner) and connection state. Right after the "
    "server starts, the first call waits for the first enumeration. Pass refresh=true to "
    "enumerate the probes again (takes up to 2 s). Use an id with connect.";

constexpr const char* connectDescription =
    "Connect a device by its id from list_devices (\"jlink:<serial>\" or \"ble:<id>\") and start "
    "its console (RTT over J-Link, or the Bluetooth shell). Returns after the first connection "
    "attempt (usually 1-5 s, at most about 35 s; longer at a low J-Link speed). When the device is "
    "already connected it returns at once with already_open=true. With auto_reconnect the session "
    "keeps retrying after a failed attempt; check `connection` and use wait_for_connection. The "
    "user may have to allow the connection in HARDWARIO Monitor first.";

constexpr const char* disconnectDescription =
    "Disconnect a device that was connected with connect and release its probe. In HARDWARIO "
    "Monitor only devices the AI connected can be disconnected.";

constexpr const char* statusDescription =
    "Connection state and console buffer counters of one device, or of every connected device "
    "when `device` is left out. connection.state: connected (working), connecting (an attach is "
    "in progress), disconnected (the link failed; call reconnect, or wait_for_connection when the "
    "device is expected back after a reset or flash), stopped (stopped by stop, release_probe or "
    "a fatal error; see connection.error; call start, reacquire_probe or reconnect). "
    "busy_operation names a running operation (flash, erase, ...); while it runs the console is "
    "paused (paused_by_operation). *_total count all lines ever received, *_buffered the lines "
    "still stored (buffer_size per channel).";

// list_devices refresh=true: waits for the asynchronous probe enumeration (ADR-0162), then lists.
// Review V1: without refresh, while the provider's first (start-up) enumeration runs, it waits for
// that one (refresh() joins it) and then lists the cache even when the enumeration failed, like a
// plain list_devices (`reportErrors` false).
class RefreshDevicesCall final : public hio::mcp::ToolCall {
public:
    RefreshDevicesCall(IDeviceProvider& provider, bool reportErrors) : m_provider(&provider) {
        const QPointer<RefreshDevicesCall> self(this);
        provider.refresh([self, reportErrors](const hio::Status& refreshed) {
            if (self.isNull() || self->isFinished()) {
                return; // cancelled meanwhile
            }
            if (!refreshed && reportErrors) {
                self->finish(std::unexpected(refreshed.error()));
                return;
            }
            self->finish(hio::mcp::toolResult(devicesJson(*self->m_provider)));
        });
    }

private:
    IDeviceProvider* m_provider; // non-owning, outlives the call (ToolsContext)
};

// connect: open (after consent) and wait for the outcome of the first attempt (waitForFirstAttempt
// semantics on connectionChanged, ADR-0097: no nested loop).
class ConnectCall final : public hio::mcp::ToolCall {
public:
    ConnectCall(ToolsContext& tools, const hio::mcp::RequestContext& context,
                hio::mcp::ToolEnv& env, OpenRequest request)
        : m_tools(&tools), m_request(std::move(request)) {
        setDeviceId(m_request.id);
        m_timeout.setSingleShot(true);
        m_timeout.setInterval(hio::device::connectTimeoutForSpeed(m_request.speedKhz));
        connect(&m_timeout, &QTimer::timeout, this,
                [this] { fail(makeError(ErrorCode::Timeout, "no connection attempt finished")); });
        if (auto ref = tools.provider().find(m_request.id); ref && ref->alive()) {
            QJsonObject result;
            result.insert(QStringLiteral("device"), qs(m_request.id));
            result.insert(QStringLiteral("already_open"), true);
            result.insert(QStringLiteral("connection"), connectionJson(ref->console->connection()));
            finish(hio::mcp::toolResult(result));
            return;
        }
        QJsonObject details;
        details.insert(QStringLiteral("device"), qs(m_request.id));
        if (!m_request.profileId.empty()) {
            details.insert(QStringLiteral("profile"), qs(m_request.profileId));
        }
        if (!m_request.targetId.empty()) {
            details.insert(QStringLiteral("target"), qs(m_request.targetId));
        }
        requestConsent(env.consentBroker(),
                       hio::mcp::makeConsentRequest(context, hio::mcp::ConsentCategory::Connect,
                                                    m_request.id, details),
                       [this](const hio::mcp::ConsentDecision& decision) {
                           if (!decision) {
                               finish(std::unexpected(decision.error()));
                               return;
                           }
                           open();
                       });
    }

protected:
    void onCancel() override {
        m_timeout.stop();
        const bool opened = m_opened && m_ref.alive();
        ToolCall::onCancel();
        if (opened) {
            closeLater();
        }
    }

private:
    void open() {
        const QPointer<ConnectCall> self(this);
        m_tools->provider().open(m_request, [self](hio::Result<DeviceRef> ref) {
            if (self.isNull() || self->isFinished()) {
                return;
            }
            if (!ref) {
                self->finish(std::unexpected(std::move(ref).error()));
                return;
            }
            self->opened(*ref);
        });
    }

    void opened(const DeviceRef& ref) {
        m_opened = true;
        m_ref = ref;
        if (!m_ref.alive()) {
            finish(std::unexpected(deviceNotOpen(m_request.id)));
            return;
        }
        connect(m_ref.console.data(), &QObject::destroyed, this,
                [this] { fail(deviceNotOpen(m_request.id)); });
        connect(m_ref.console.data(), &hio::device::ConsoleSession::connectionChanged, this,
                [this](const hio::device::ConnectionEvent& event) { evaluate(event); });
        m_timeout.start();
        evaluate(m_ref.console->connection());
    }

    void evaluate(const hio::device::ConnectionEvent& event) {
        if (isFinished()) {
            return;
        }
        switch (event.state) {
        case hio::device::ConnectionState::Connecting:
        case hio::device::ConnectionState::ProbeOpen:
            return;
        case hio::device::ConnectionState::Connected:
        case hio::device::ConnectionState::Disconnected: {
            // The first attempt is over; a Disconnected session keeps retrying (auto-reconnect).
            m_timeout.stop();
            QJsonObject result;
            result.insert(QStringLiteral("device"), qs(m_request.id));
            result.insert(QStringLiteral("already_open"), false);
            result.insert(QStringLiteral("connection"), connectionJson(event));
            finish(hio::mcp::toolResult(result));
            return;
        }
        case hio::device::ConnectionState::Stopped:
            fail(event.error ? *event.error : makeError(ErrorCode::Cancelled));
            return;
        }
    }

    // Closes the session this call opened and finishes with `error`.
    void fail(hio::Error error) {
        if (isFinished()) {
            return;
        }
        m_timeout.stop();
        const bool opened = m_opened && m_ref.alive();
        finish(std::unexpected(std::move(error)));
        if (opened) {
            closeLater();
        }
    }

    // Closes the session this call opened, from the event loop: fail() can run inside the
    // session's own connectionChanged signal, where destroying the session is not allowed.
    void closeLater() {
        ToolsContext* tools = m_tools;
        QTimer::singleShot(0, tools, [tools, id = m_request.id, context = m_request.context] {
            if (const hio::Status closed = tools->provider().close(id, context); !closed) {
                qCWarning(hio::log::lcMcp).noquote()
                    << "MCP connect: closing" << qs(id) << "failed:" << qs(closed.error().message);
            }
        });
    }

    ToolsContext* m_tools; // non-owning
    OpenRequest m_request;
    DeviceRef m_ref;
    QTimer m_timeout;
    bool m_opened = false;
};

hio::Status addListDevices(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "list_devices",
            .title = "List devices",
            .description = listDevicesDescription,
            .inputSchema =
                Schema::object()
                    .property(
                        QStringLiteral("refresh"),
                        Schema::boolean(QStringLiteral("Enumerate the J-Link probes again first."))
                            .defaultValue(false))
                    .json(),
            .outputSchema =
                objectOf({{"devices", arrayOf(deviceEntrySchema())}}, {QStringLiteral("devices")}),
            .annotations = readOnly,
            .consent = {},
            .marker = false,
            .start = [&context](const hio::mcp::RequestContext&, const QJsonObject& args,
                                hio::mcp::ToolEnv&) -> std::unique_ptr<hio::mcp::ToolCall> {
                if (args.value(QStringLiteral("refresh")).toBool()) {
                    return std::make_unique<RefreshDevicesCall>(context.provider(), true);
                }
                if (context.provider().firstScanPending()) { // review V1
                    return std::make_unique<RefreshDevicesCall>(context.provider(), false);
                }
                return done(devicesJson(context.provider()));
            },
            .validate = {}});
}

hio::Status addConnect(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "connect",
            .title = "Connect a device",
            .description = connectDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"),
                              Schema::string(QStringLiteral("Device id from list_devices, e.g. "
                                                            "\"jlink:801000001\"."))
                                  .minLength(1)
                                  .maxLength(160)
                                  .pattern(QStringLiteral("^(jlink|ble):\\S+$")),
                              Required::Yes)
                    .property(
                        QStringLiteral("profile"),
                        Schema::string(QStringLiteral("Device profile (default: \"chester\")."))
                            .minLength(1)
                            .maxLength(32))
                    .property(
                        QStringLiteral("target"),
                        Schema::string(QStringLiteral("J-Link: the MCU whose RTT console to use "
                                                      "(CHESTER: \"app\"; default: the profile's "
                                                      "first target)."))
                            .minLength(1)
                            .maxLength(32))
                    .property(
                        QStringLiteral("speed_khz"),
                        Schema::integer(QStringLiteral("J-Link speed in kHz (default: automatic or "
                                                       "the server's setting)."))
                            .minimum(hio::device::minSpeedKhz)
                            .maximum(hio::device::maxSpeedKhz))
                    .property(QStringLiteral("auto_reconnect"),
                              Schema::boolean(QStringLiteral(
                                  "Keep reconnecting after the link fails (default: the "
                                  "server's setting).")))
                    .json(),
            .outputSchema = objectOf({{"device", typed("string")},
                                      {"already_open", typed("boolean")},
                                      {"connection", connectionSchema()}},
                                     {QStringLiteral("device"), QStringLiteral("already_open"),
                                      QStringLiteral("connection")}),
            .annotations = hio::mcp::ToolAnnotations{.readOnlyHint = false,
                                                     .destructiveHint = false,
                                                     .idempotentHint = true,
                                                     .openWorldHint = false},
            .consent = hio::mcp::ConsentCategory::Connect,
            .marker = false,
            .start = [&context](const hio::mcp::RequestContext& rc, const QJsonObject& args,
                                hio::mcp::ToolEnv& env) -> std::unique_ptr<hio::mcp::ToolCall> {
                OpenRequest request;
                request.id = args.value(QStringLiteral("device")).toString().toStdString();
                request.profileId = args.value(QStringLiteral("profile")).toString().toStdString();
                request.targetId = args.value(QStringLiteral("target")).toString().toStdString();
                if (args.contains(QStringLiteral("speed_khz"))) {
                    request.speedKhz = args.value(QStringLiteral("speed_khz")).toInt();
                }
                if (args.contains(QStringLiteral("auto_reconnect"))) {
                    request.autoReconnect = args.value(QStringLiteral("auto_reconnect")).toBool();
                }
                request.context = rc;
                return std::make_unique<ConnectCall>(context, rc, env, std::move(request));
            },
            .validate = {}});
}

hio::Status addDisconnect(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "disconnect",
            .title = "Disconnect a device",
            .description = disconnectDescription,
            .inputSchema =
                Schema::object().property(QStringLiteral("device"), deviceArgument()).json(),
            .outputSchema = objectOf({{"device", typed("string")}, {"closed", typed("boolean")}},
                                     {QStringLiteral("device"), QStringLiteral("closed")}),
            .annotations = hio::mcp::ToolAnnotations{.readOnlyHint = false,
                                                     .destructiveHint = false,
                                                     .idempotentHint = true,
                                                     .openWorldHint = false},
            .consent = {},
            .marker = false,
            .start =
                [&context](const hio::mcp::RequestContext& rc, const QJsonObject& args,
                           hio::mcp::ToolEnv&) {
                    auto device = resolveDevice(context.provider(), args);
                    if (!device) {
                        return failed(std::move(device).error());
                    }
                    if (auto closed = context.provider().close(device->id, rc); !closed) {
                        return failed(std::move(closed).error());
                    }
                    QJsonObject result;
                    result.insert(QStringLiteral("device"), qs(device->id));
                    result.insert(QStringLiteral("closed"), true);
                    return done(result);
                },
            .validate = {}});
}

hio::Status addStatus(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "status",
            .title = "Device status",
            .description = statusDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"),
                              Schema::string(QStringLiteral("Device id; left out = every connected "
                                                            "device."))
                                  .minLength(1)
                                  .maxLength(160))
                    .json(),
            .outputSchema =
                objectOf({{"devices", arrayOf(deviceStatusSchema())}}, {QStringLiteral("devices")}),
            .annotations = readOnly,
            .consent = {},
            .marker = false,
            .start =
                [&context](const hio::mcp::RequestContext&, const QJsonObject& args,
                           hio::mcp::ToolEnv&) {
                    QJsonArray devices;
                    if (args.contains(QStringLiteral("device"))) {
                        auto device = resolveDevice(context.provider(), args);
                        if (!device) {
                            return failed(std::move(device).error());
                        }
                        devices.append(deviceStatusJson(device->id, device->ref));
                    } else {
                        for (const DeviceEntry& entry : context.provider().list()) {
                            if (entry.state != DeviceEntry::State::Open) {
                                continue;
                            }
                            if (const auto ref = context.provider().find(entry.id);
                                ref && ref->alive()) {
                                devices.append(deviceStatusJson(entry.id, *ref));
                            }
                        }
                    }
                    QJsonObject result;
                    result.insert(QStringLiteral("devices"), devices);
                    return done(result);
                },
            .validate = {}});
}

} // namespace

hio::Status registerDiscoveryTools(hio::mcp::Registry& registry, ToolsContext& context) {
    for (const auto& add : {&addListDevices, &addConnect, &addDisconnect, &addStatus}) {
        if (auto added = add(registry, context); !added) {
            return added;
        }
    }
    return {};
}

} // namespace hio::mcp::tools::detail
