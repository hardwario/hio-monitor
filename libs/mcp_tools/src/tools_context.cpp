#include "hio/mcp/tools/tools_context.h"

#include "hio/device/console_line.h"
#include "hio/device/console_session.h"
#include "hio/device/session_events.h"
#include "hio/mcp/server.h"
#include "hio/mcp/tools/device_provider.h"
#include "resources.h"
#include "scan_scheduler.h"

#include <QList>
#include <QObject>
#include <QStandardPaths>
#include <QString>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace hio::mcp::tools {

namespace detail {

// The session connections for resources/updated; destroying the context object disconnects them.
struct ResourceWatch {
    std::unique_ptr<QObject> connections = std::make_unique<QObject>();
};

} // namespace detail

ToolsContext::ToolsContext(IDeviceProvider& provider, hio::firmware::FirmwareCatalog* catalog,
                           ToolsOptions options, QObject* parent)
    : QObject(parent), m_provider(&provider), m_catalog(catalog), m_options(std::move(options)),
      m_scheduler(std::make_unique<detail::ScanScheduler>()) {
    connect(&provider.notifier(), &DeviceProviderNotifier::devicesChanged, this,
            &ToolsContext::devicesChanged);
}

ToolsContext::~ToolsContext() = default;

std::filesystem::path ToolsContext::memoryDumpDirectory() const {
    if (!m_options.memoryDumpDirectory.empty()) {
        return m_options.memoryDumpDirectory;
    }
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return std::filesystem::path(cache.toStdU16String()) / "mcp";
}

void ToolsContext::attachServer(hio::mcp::McpServer& server) {
    m_server = &server;
    rewatch();
}

void ToolsContext::detachServer() {
    m_server.clear();
    m_watch.reset();
}

void ToolsContext::devicesChanged() {
    if (m_server.isNull()) {
        return;
    }
    m_server->notifyResourceListChanged();
    m_server->notifyResourceUpdated(detail::devicesUri());
    rewatch();
}

void ToolsContext::rewatch() {
    m_watch = std::make_unique<detail::ResourceWatch>();
    if (m_server.isNull()) {
        return;
    }
    for (const DeviceEntry& entry : m_provider->list()) {
        if (entry.state != DeviceEntry::State::Open) {
            continue;
        }
        if (const auto ref = m_provider->find(entry.id); ref && ref->alive()) {
            watch(entry.id, *ref);
        }
    }
}

void ToolsContext::watch(const std::string& id, const DeviceRef& ref) {
    QObject* context = m_watch->connections.get();
    connect(ref.console.data(), &hio::device::ConsoleSession::linesReceived, context,
            [this, id](const QList<hio::device::ConsoleLine>& lines) { linesArrived(id, lines); });
    connect(ref.console.data(), &hio::device::ConsoleSession::connectionChanged, context,
            [this, id](const hio::device::ConnectionEvent&) {
                if (m_server.isNull()) {
                    return;
                }
                m_server->notifyResourceUpdated(detail::deviceUri(id, QStringLiteral("status")));
                m_server->notifyResourceUpdated(detail::devicesUri());
            });
}

void ToolsContext::linesArrived(const std::string& id,
                                const QList<hio::device::ConsoleLine>& lines) {
    if (m_server.isNull()) {
        return;
    }
    const bool shell = std::ranges::any_of(lines, [](const hio::device::ConsoleLine& line) {
        return line.channel == hio::device::Channel::Shell;
    });
    const bool log = std::ranges::any_of(lines, [](const hio::device::ConsoleLine& line) {
        return line.channel == hio::device::Channel::Log;
    });
    if (shell) {
        m_server->notifyResourceUpdated(detail::deviceUri(id, QStringLiteral("terminal")));
    }
    if (log) {
        m_server->notifyResourceUpdated(detail::deviceUri(id, QStringLiteral("log")));
    }
}

} // namespace hio::mcp::tools
