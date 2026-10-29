#include "resources.h"

#include "hwio/core/result.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/request_context.h"
#include "hwio/mcp/tools/device_provider.h"
#include "hwio/mcp/tools/tools_context.h"
#include "tool_support.h"

#include <QChar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::mcp::tools::detail {

namespace {

QString compact(const QJsonObject& json) {
    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

// One text line per console line: device output as it is, commands as "> <command>", markers as
// they are.
QString linesText(const std::vector<hwio::device::ConsoleLine>& lines) {
    QString text;
    for (const hwio::device::ConsoleLine& line : lines) {
        if (!text.isEmpty()) {
            text += QLatin1Char('\n');
        }
        if (line.dir == hwio::device::Direction::In) {
            text += QStringLiteral("> ");
        }
        text += qs(line.text);
    }
    return text;
}

hwio::mcp::ResourceReader linesReader(ToolsContext& context, hwio::device::Channel channel) {
    return [&context, channel](const hwio::mcp::RequestContext&, const QString& uri,
                               const std::map<QString, QString>& variables) {
        const auto it = variables.find(QStringLiteral("id"));
        if (it == variables.end()) {
            return hwio::mcp::ResourceReadResult::notFound();
        }
        const auto ref = context.provider().find(it->second.toStdString());
        if (!ref || !ref->alive()) {
            return hwio::mcp::ResourceReadResult::notFound();
        }
        const auto mask = channel == hwio::device::Channel::Shell ? hwio::device::ChannelMask::Shell
                                                                  : hwio::device::ChannelMask::Log;
        return hwio::mcp::ResourceReadResult::text(
            uri, QStringLiteral("text/plain"),
            linesText(ref->console->log().last(resourceLines, mask)));
    };
}

} // namespace

QString devicesUri() { return QStringLiteral("hwio://devices"); }

QString deviceUri(std::string_view id, const QString& part) {
    return QStringLiteral("hwio://device/%1/%2").arg(qs(id), part);
}

hwio::Status registerResources(hwio::mcp::Registry& registry, ToolsContext& context) {
    if (auto added = registry.addResource(hwio::mcp::ResourceSpec{
            .uri = "hwio://devices",
            .name = "devices",
            .title = "Devices",
            .description = "The devices this server knows (same shape as list_devices).",
            .mimeType = "application/json",
            .read =
                [&context](const hwio::mcp::RequestContext&, const QString& uri,
                           const std::map<QString, QString>&) {
                    return hwio::mcp::ResourceReadResult::text(
                        uri, QStringLiteral("application/json"),
                        compact(devicesJson(context.provider())));
                }});
        !added) {
        return added;
    }
    if (auto added = registry.addResourceTemplate(hwio::mcp::ResourceTemplateSpec{
            .uriTemplate = "hwio://device/{id}/status",
            .name = "device-status",
            .title = "Device status",
            .description = "Connection state and console buffer counters of a connected device "
                           "(same shape as one entry of the status tool).",
            .mimeType = "application/json",
            .read =
                [&context](const hwio::mcp::RequestContext&, const QString& uri,
                           const std::map<QString, QString>& variables) {
                    const auto it = variables.find(QStringLiteral("id"));
                    if (it == variables.end()) {
                        return hwio::mcp::ResourceReadResult::notFound();
                    }
                    const std::string id = it->second.toStdString();
                    const auto ref = context.provider().find(id);
                    if (!ref || !ref->alive()) {
                        return hwio::mcp::ResourceReadResult::notFound();
                    }
                    return hwio::mcp::ResourceReadResult::text(uri,
                                                               QStringLiteral("application/json"),
                                                               compact(deviceStatusJson(id, *ref)));
                }});
        !added) {
        return added;
    }
    if (auto added = registry.addResourceTemplate(hwio::mcp::ResourceTemplateSpec{
            .uriTemplate = "hwio://device/{id}/log",
            .name = "device-log",
            .title = "Device log",
            .description = "The last 1000 device log lines of a connected device (text).",
            .mimeType = "text/plain",
            .read = linesReader(context, hwio::device::Channel::Log)});
        !added) {
        return added;
    }
    if (auto added = registry.addResourceTemplate(hwio::mcp::ResourceTemplateSpec{
            .uriTemplate = "hwio://device/{id}/terminal",
            .name = "device-terminal",
            .title = "Device terminal",
            .description = "The last 1000 shell lines of a connected device (text; commands as "
                           "\"> command\").",
            .mimeType = "text/plain",
            .read = linesReader(context, hwio::device::Channel::Shell)});
        !added) {
        return added;
    }
    registry.setResourceLister([&context](const hwio::mcp::RequestContext&) {
        std::vector<hwio::mcp::ResourceInfo> resources;
        for (const DeviceEntry& entry : context.provider().list()) {
            if (entry.state != DeviceEntry::State::Open) {
                continue;
            }
            const QString name = qs(entry.displayName.empty() ? entry.id : entry.displayName);
            resources.push_back(
                hwio::mcp::ResourceInfo{.uri = deviceUri(entry.id, QStringLiteral("status")),
                                        .name = name + QStringLiteral(" status"),
                                        .title = {},
                                        .description = {},
                                        .mimeType = QStringLiteral("application/json")});
            resources.push_back(
                hwio::mcp::ResourceInfo{.uri = deviceUri(entry.id, QStringLiteral("log")),
                                        .name = name + QStringLiteral(" log"),
                                        .title = {},
                                        .description = {},
                                        .mimeType = QStringLiteral("text/plain")});
            resources.push_back(
                hwio::mcp::ResourceInfo{.uri = deviceUri(entry.id, QStringLiteral("terminal")),
                                        .name = name + QStringLiteral(" terminal"),
                                        .title = {},
                                        .description = {},
                                        .mimeType = QStringLiteral("text/plain")});
        }
        return resources;
    });
    return {};
}

} // namespace hwio::mcp::tools::detail
