// Session control tools (plan/features/0013 C1, ADR-0099; rttt reconnect / stop / start /
// jlink_close / jlink_open): reconnect, stop, start, release_probe, reacquire_probe. They queue a
// command for the session and return at once; wait_for_connection waits for the outcome.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_session.h"
#include "hio/device/device_session.h"
#include "hio/device/session_events.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/schema.h"
#include "hio/mcp/tool_call.h"
#include "hio/mcp/tools/device_provider.h"
#include "hio/mcp/tools/tools_context.h"
#include "tool_support.h"
#include "tools/tools.h"

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace hio::mcp::tools::detail {

namespace {

using hio::mcp::Schema;

constexpr const char* reconnectDescription =
    "Restart the console connection without resetting the device: re-attach RTT (J-Link) or "
    "reconnect Bluetooth. Use it when no output arrives, commands fail, or the device was reset "
    "or reflashed outside this server. rtt_address (J-Link only) sets the RTT control block "
    "address, e.g. \"0x20004000\" (the _SEGGER_RTT symbol from the firmware ELF: "
    "`nm zephyr.elf | grep _SEGGER_RTT`), when the automatic search attaches to a stale block; "
    "it is kept for later connections, \"auto\" returns to the search. Returns at once; call "
    "wait_for_connection to wait for the result.";

constexpr const char* stopDescription =
    "Stop reading the device's RTT console; the probe stays held, so memory, registers and "
    "halt/go keep working, and the firmware keeps running. No output arrives while stopped. "
    "Call start to read again, or release_probe to also give the probe up. J-Link only.";

constexpr const char* startDescription =
    "Start reading the RTT console again after stop. Use reconnect instead when the session is "
    "running but stuck. J-Link only.";

constexpr const char* releaseProbeDescription =
    "Release the J-Link probe so that another program (J-Link Commander, nrfjprog, west flash) "
    "can use it (rttt: jlink_close). Stops RTT; the firmware keeps running. HARDWARIO programs "
    "still see the probe as used by this server. Call reacquire_probe to take it back. J-Link "
    "only.";

constexpr const char* reacquireProbeDescription =
    "Take the J-Link probe back after release_probe and start reading RTT again (rttt: "
    "jlink_open). Fails (connection.state stopped, probe.in_use_by_other_process) while another "
    "program still holds the probe. Returns at once; call wait_for_connection to wait. J-Link "
    "only.";

QJsonObject connectionResult(const DeviceRef& ref) {
    QJsonObject result;
    result.insert(QStringLiteral("connection"), connectionJson(ref.console->connection()));
    return result;
}

using SessionAction =
    std::function<hio::Result<QJsonObject>(const ResolvedDevice& device, const QJsonObject& args)>;

// A synchronous control tool: device resolution (J-Link only when `jlinkOnly`), the start marker
// (ADR-0105), then `action`.
hio::mcp::ToolStart controlStart(ToolsContext& context, const char* tool, bool jlinkOnly,
                                 SessionAction action) {
    return [&context, tool, jlinkOnly,
            action = std::move(action)](const hio::mcp::RequestContext& rc, const QJsonObject& args,
                                        hio::mcp::ToolEnv&) -> std::unique_ptr<hio::mcp::ToolCall> {
        auto device = jlinkOnly ? resolveJLinkDevice(context.provider(), args)
                                : resolveDevice(context.provider(), args);
        if (!device) {
            return failed(std::move(device).error());
        }
        context.provider().marker(device->id, markerStart(rc, QString::fromLatin1(tool)));
        auto result = action(*device, args);
        auto call = done(std::move(result));
        return call;
    };
}

QJsonObject deviceOnly() {
    return Schema::object().property(QStringLiteral("device"), deviceArgument()).json();
}

hio::Status addReconnect(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "reconnect",
            .title = "Reconnect the console",
            .description = reconnectDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"), deviceArgument())
                    .property(
                        QStringLiteral("rtt_address"),
                        Schema::string(QStringLiteral("\"auto\" or the RTT control block address "
                                                      "(J-Link only; sticky)."))
                            .pattern(QStringLiteral("^(auto|0x[0-9a-fA-F]{1,8}|[0-9]{1,10})$")))
                    .json(),
            .outputSchema =
                objectOf({{"connection", connectionSchema()}}, {QStringLiteral("connection")}),
            .annotations = control,
            .consent = {},
            .marker = true,
            .start = [&context](const hio::mcp::RequestContext& rc, const QJsonObject& args,
                                hio::mcp::ToolEnv&) -> std::unique_ptr<hio::mcp::ToolCall> {
                auto device = resolveDevice(context.provider(), args);
                if (!device) {
                    return failed(std::move(device).error());
                }
                const QString address = args.value(QStringLiteral("rtt_address")).toString();
                if (!address.isEmpty() && device->ref.jlink.isNull()) {
                    return failed(capabilityMissing(device->id));
                }
                context.provider().marker(device->id, markerStart(rc, QStringLiteral("reconnect")));
                if (!address.isEmpty()) {
                    std::optional<std::uint32_t> value;
                    if (address != QStringLiteral("auto")) {
                        value = parseAddress(address);
                        if (!value) {
                            return failed(invalidArgument(
                                QStringLiteral("The address %1 is not valid.").arg(address),
                                address.toStdString()));
                        }
                    }
                    if (auto set = device->ref.jlink->setRttAddress(value); !set) {
                        return failed(std::move(set).error());
                    }
                }
                if (auto reconnected = device->ref.console->reconnect(); !reconnected) {
                    return failed(std::move(reconnected).error());
                }
                return done(connectionResult(device->ref));
            },
            .validate = {}});
}

hio::Status addStop(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "stop",
            .title = "Stop reading RTT",
            .description = stopDescription,
            .inputSchema = deviceOnly(),
            .outputSchema =
                objectOf({{"was_reading", typed("boolean")}}, {QStringLiteral("was_reading")}),
            .annotations = control,
            .consent = {},
            .marker = true,
            .start = controlStart(
                context, "stop", true,
                [](const ResolvedDevice& device, const QJsonObject&) -> hio::Result<QJsonObject> {
                    const auto state = device.ref.console->connection().state;
                    const bool wasReading = state == hio::device::ConnectionState::Connected ||
                                            state == hio::device::ConnectionState::Connecting;
                    if (auto suspended = device.ref.jlink->suspend(); !suspended) {
                        return std::unexpected(std::move(suspended).error());
                    }
                    QJsonObject result;
                    result.insert(QStringLiteral("was_reading"), wasReading);
                    return result;
                }),
            .validate = {}});
}

hio::Status addStart(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "start",
            .title = "Start reading RTT",
            .description = startDescription,
            .inputSchema = deviceOnly(),
            .outputSchema = objectOf({{"already_reading", typed("boolean")}},
                                     {QStringLiteral("already_reading")}),
            .annotations = control,
            .consent = {},
            .marker = true,
            .start = controlStart(
                context, "start", true,
                [](const ResolvedDevice& device, const QJsonObject&) -> hio::Result<QJsonObject> {
                    const bool reading = device.ref.console->connection().state ==
                                         hio::device::ConnectionState::Connected;
                    if (!reading) {
                        if (auto resumed = device.ref.jlink->resume(); !resumed) {
                            return std::unexpected(std::move(resumed).error());
                        }
                    }
                    QJsonObject result;
                    result.insert(QStringLiteral("already_reading"), reading);
                    return result;
                }),
            .validate = {}});
}

hio::Status addReleaseProbe(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "release_probe",
            .title = "Release the probe",
            .description = releaseProbeDescription,
            .inputSchema = deviceOnly(),
            .outputSchema =
                objectOf({{"released", typed("boolean")}}, {QStringLiteral("released")}),
            .annotations = control,
            .consent = {},
            .marker = true,
            .start = controlStart(
                context, "release_probe", true,
                [](const ResolvedDevice& device, const QJsonObject&) -> hio::Result<QJsonObject> {
                    if (auto released = device.ref.jlink->releaseProbe(); !released) {
                        return std::unexpected(std::move(released).error());
                    }
                    QJsonObject result;
                    result.insert(QStringLiteral("released"), true);
                    return result;
                }),
            .validate = {}});
}

hio::Status addReacquireProbe(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "reacquire_probe",
            .title = "Take the probe back",
            .description = reacquireProbeDescription,
            .inputSchema = deviceOnly(),
            .outputSchema =
                objectOf({{"connection", connectionSchema()}}, {QStringLiteral("connection")}),
            .annotations = control,
            .consent = {},
            .marker = true,
            .start = controlStart(
                context, "reacquire_probe", true,
                [](const ResolvedDevice& device, const QJsonObject&) -> hio::Result<QJsonObject> {
                    if (auto reacquired = device.ref.jlink->reacquireProbe(); !reacquired) {
                        return std::unexpected(std::move(reacquired).error());
                    }
                    return connectionResult(device.ref);
                }),
            .validate = {}});
}

} // namespace

hio::Status registerSessionTools(hio::mcp::Registry& registry, ToolsContext& context) {
    for (const auto& add :
         {&addReconnect, &addStop, &addStart, &addReleaseProbe, &addReacquireProbe}) {
        if (auto added = add(registry, context); !added) {
            return added;
        }
    }
    return {};
}

} // namespace hio::mcp::tools::detail
