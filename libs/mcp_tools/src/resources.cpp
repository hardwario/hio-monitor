#include "resources.h"

#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/tools/device_provider.h"
#include "hio/mcp/tools/tools_context.h"
#include "tool_support.h"

#include <QChar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hio::mcp::tools::detail {

namespace {

QString compact(const QJsonObject& json) {
    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

// One text line per console line: device output as it is, commands as "> <command>", markers as
// they are.
QString linesText(const std::vector<hio::device::ConsoleLine>& lines) {
    QString text;
    for (const hio::device::ConsoleLine& line : lines) {
        if (!text.isEmpty()) {
            text += QLatin1Char('\n');
        }
        if (line.dir == hio::device::Direction::In) {
            text += QStringLiteral("> ");
        }
        text += qs(line.text);
    }
    return text;
}

hio::mcp::ResourceReader linesReader(ToolsContext& context, hio::device::Channel channel) {
    return [&context, channel](const hio::mcp::RequestContext&, const QString& uri,
                               const std::map<QString, QString>& variables) {
        const auto it = variables.find(QStringLiteral("id"));
        if (it == variables.end()) {
            return hio::mcp::ResourceReadResult::notFound();
        }
        const auto ref = context.provider().find(it->second.toStdString());
        if (!ref || !ref->alive()) {
            return hio::mcp::ResourceReadResult::notFound();
        }
        const auto mask = channel == hio::device::Channel::Shell ? hio::device::ChannelMask::Shell
                                                                 : hio::device::ChannelMask::Log;
        return hio::mcp::ResourceReadResult::text(
            uri, QStringLiteral("text/plain"),
            linesText(ref->console->log().last(resourceLines, mask)));
    };
}

} // namespace

QString devicesUri() { return QStringLiteral("hio://devices"); }

QString deviceUri(std::string_view id, const QString& part) {
    return QStringLiteral("hio://device/%1/%2").arg(qs(id), part);
}

hio::Status registerResources(hio::mcp::Registry& registry, ToolsContext& context) {
    if (auto added = registry.addResource(hio::mcp::ResourceSpec{
            .uri = "hio://devices",
            .name = "devices",
            .title = "Devices",
            .description = "The devices this server knows (same shape as list_devices).",
            .mimeType = "application/json",
            .read =
                [&context](const hio::mcp::RequestContext&, const QString& uri,
                           const std::map<QString, QString>&) {
                    return hio::mcp::ResourceReadResult::text(
                        uri, QStringLiteral("application/json"),
                        compact(devicesJson(context.provider())));
                }});
        !added) {
        return added;
    }
    if (auto added = registry.addResourceTemplate(hio::mcp::ResourceTemplateSpec{
            .uriTemplate = "hio://device/{id}/status",
            .name = "device-status",
            .title = "Device status",
            .description = "Connection state and console buffer counters of a connected device "
                           "(same shape as one entry of the status tool).",
            .mimeType = "application/json",
            .read =
                [&context](const hio::mcp::RequestContext&, const QString& uri,
                           const std::map<QString, QString>& variables) {
                    const auto it = variables.find(QStringLiteral("id"));
                    if (it == variables.end()) {
                        return hio::mcp::ResourceReadResult::notFound();
                    }
                    const std::string id = it->second.toStdString();
                    const auto ref = context.provider().find(id);
                    if (!ref || !ref->alive()) {
                        return hio::mcp::ResourceReadResult::notFound();
                    }
                    return hio::mcp::ResourceReadResult::text(uri,
                                                              QStringLiteral("application/json"),
                                                              compact(deviceStatusJson(id, *ref)));
                }});
        !added) {
        return added;
    }
    if (auto added = registry.addResourceTemplate(hio::mcp::ResourceTemplateSpec{
            .uriTemplate = "hio://device/{id}/log",
            .name = "device-log",
            .title = "Device log",
            .description = "The last 1000 device log lines of a connected device (text).",
            .mimeType = "text/plain",
            .read = linesReader(context, hio::device::Channel::Log)});
        !added) {
        return added;
    }
    if (auto added = registry.addResourceTemplate(hio::mcp::ResourceTemplateSpec{
            .uriTemplate = "hio://device/{id}/terminal",
            .name = "device-terminal",
            .title = "Device terminal",
            .description = "The last 1000 shell lines of a connected device (text; commands as "
                           "\"> command\").",
            .mimeType = "text/plain",
            .read = linesReader(context, hio::device::Channel::Shell)});
        !added) {
        return added;
    }
    registry.setResourceLister([&context](const hio::mcp::RequestContext&) {
        std::vector<hio::mcp::ResourceInfo> resources;
        for (const DeviceEntry& entry : context.provider().list()) {
            if (entry.state != DeviceEntry::State::Open) {
                continue;
            }
            const QString name = qs(entry.displayName.empty() ? entry.id : entry.displayName);
            resources.push_back(
                hio::mcp::ResourceInfo{.uri = deviceUri(entry.id, QStringLiteral("status")),
                                       .name = name + QStringLiteral(" status"),
                                       .title = {},
                                       .description = {},
                                       .mimeType = QStringLiteral("application/json")});
            resources.push_back(
                hio::mcp::ResourceInfo{.uri = deviceUri(entry.id, QStringLiteral("log")),
                                       .name = name + QStringLiteral(" log"),
                                       .title = {},
                                       .description = {},
                                       .mimeType = QStringLiteral("text/plain")});
            resources.push_back(
                hio::mcp::ResourceInfo{.uri = deviceUri(entry.id, QStringLiteral("terminal")),
                                       .name = name + QStringLiteral(" terminal"),
                                       .title = {},
                                       .description = {},
                                       .mimeType = QStringLiteral("text/plain")});
        }
        return resources;
    });
    return {};
}

} // namespace hio::mcp::tools::detail
