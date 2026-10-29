#pragma once

// ToolsContext - what the MCP device tools share (plan/features/0013 C1-C3, ADR-0150): the
// device provider, the firmware catalog for `flash catalog_id`, options, the resource
// notifications of the server and the scheduler that spreads the regex scans of wait_for_state
// over owner-thread turns (ADR-0097: no owner-thread task may stall the GUI).
//
// Typical use (CLI `hwio mcp`, GUI plan 0014):
//   ToolsContext tools(provider, &catalog);
//   hwio::mcp::Registry registry;
//   if (auto ok = hwio::mcp::tools::registerTools(registry, tools); !ok) { ... }
//   hwio::mcp::McpServer server(std::move(registry), broker, sink, options);
//   tools.attachServer(server);   // resources/updated, resources/list_changed (ADR-0107)
// The context must outlive the server (the tool handlers keep references to it).
//
// Thread safety: owner thread only.

#include "hwio/device/console_line.h"
#include "hwio/mcp/tools/device_provider.h"

#include <QList>
#include <QObject>
#include <QPointer>

#include <filesystem>
#include <memory>
#include <string>

namespace hwio::firmware {
class FirmwareCatalog;
} // namespace hwio::firmware

namespace hwio::mcp {
class McpServer;
} // namespace hwio::mcp

namespace hwio::mcp::tools {

namespace detail {
class ScanScheduler;
struct ResourceWatch;
} // namespace detail

struct ToolsOptions {
    // read_memory `to_file` with a bare name writes into this directory (ADR-0100); empty =
    // QStandardPaths::CacheLocation + "/mcp" (created on first use).
    std::filesystem::path memoryDumpDirectory;
};

class ToolsContext final : public QObject {
    Q_OBJECT

public:
    // `provider` and `catalog` must outlive the context. `catalog` may be nullptr: `flash` with a
    // catalog_id then fails with usage.invalid_argument.
    ToolsContext(IDeviceProvider& provider, hwio::firmware::FirmwareCatalog* catalog,
                 ToolsOptions options = {}, QObject* parent = nullptr);
    ~ToolsContext() override;
    ToolsContext(const ToolsContext&) = delete;
    ToolsContext& operator=(const ToolsContext&) = delete;
    ToolsContext(ToolsContext&&) = delete;
    ToolsContext& operator=(ToolsContext&&) = delete;

    [[nodiscard]] IDeviceProvider& provider() const noexcept { return *m_provider; }
    [[nodiscard]] hwio::firmware::FirmwareCatalog* catalog() const noexcept { return m_catalog; }
    [[nodiscard]] const ToolsOptions& options() const noexcept { return m_options; }
    // ToolsOptions::memoryDumpDirectory or its default.
    [[nodiscard]] std::filesystem::path memoryDumpDirectory() const;

    // Forwards device changes to `server` (ADR-0107): resources/list_changed when a device opens
    // or closes, resources/updated for hwio://devices and hwio://device/{id}/status on connection
    // changes, for hwio://device/{id}/log and /terminal on new lines (the server throttles to one
    // per second per URI). One server at a time; a second call replaces the first.
    void attachServer(hwio::mcp::McpServer& server);
    void detachServer();

    // Internal (tool handlers).
    [[nodiscard]] detail::ScanScheduler& scanScheduler() const noexcept { return *m_scheduler; }

private:
    void devicesChanged();
    void rewatch();
    void watch(const std::string& id, const DeviceRef& ref);
    void linesArrived(const std::string& id, const QList<hwio::device::ConsoleLine>& lines);

    IDeviceProvider* m_provider;                // non-owning, never null
    hwio::firmware::FirmwareCatalog* m_catalog; // non-owning, may be null
    ToolsOptions m_options;
    std::unique_ptr<detail::ScanScheduler> m_scheduler;
    QPointer<hwio::mcp::McpServer> m_server;
    std::unique_ptr<detail::ResourceWatch> m_watch;
};

} // namespace hwio::mcp::tools
