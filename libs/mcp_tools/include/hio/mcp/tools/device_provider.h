#pragma once

// IDeviceProvider - how the MCP tools reach devices (plan/features/0013 "Device and consent seams",
// ADR-0098, ADR-0105).
//
// The tools never own sessions. The CLI's CliDeviceProvider owns the sessions it opened (at most
// SessionRegistry::maxSessions); the GUI's provider (plan 0014) maps devices to its tabs. Device
// ids are the session keys as strings: "jlink:<SN>" or "ble:<BleDeviceId>" (ADR-0098).
//
// DeviceRef holds QPointers: a GUI tab can close at any time, so every tool handler checks them on
// each step and ends with mcp.device_not_open when they are gone. DeviceRef::jlink is null for BLE
// sessions (the J-Link tools then return mcp.capability_missing).
//
// Change notifications (not in the plan's sketch, added for resources, ADR-0107): the provider
// emits DeviceProviderNotifier::devicesChanged() after a device was opened or closed (also when
// the user opened or closed a GUI tab). ToolsContext turns it into resources/list_changed and
// (re)connects to the sessions' linesReceived / connectionChanged for resources/updated.
//
// Thread safety: owner thread only (ADR-0097).

#include "hio/core/result.h"
#include "hio/device/console_session.h"
#include "hio/device/device_session.h"
#include "hio/device/probe_lock.h"
#include "hio/device/session_events.h"
#include "hio/mcp/request_context.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hio::mcp::tools {

struct DeviceEntry {
    std::string id;          // "jlink:<SN>" | "ble:<id>" (ADR-0098; SessionKey ProbeKey/BleKey)
    std::string kind;        // "jlink" | "ble"
    std::string displayName; // "J-Link 801000001", "CHESTER-ABC123"
    std::string profileId;   // "chester"; empty when unknown (a probe that is not open)
    enum class State : std::uint8_t {
        Open,
        Available,
        InUseByOtherProcess
    } state = State::Available;
    // ProbeStatus::usedByOtherProcess / BleUse::otherProcess
    std::optional<hio::device::ProbeLockOwner> owner;
    std::optional<hio::device::ConnectionEvent> connection; // open devices only
    bool openedByMcp = false;
};

struct DeviceRef {
    QPointer<hio::device::ConsoleSession> console;
    QPointer<hio::device::DeviceSession> jlink; // null for BLE sessions

    // The console session still exists.
    [[nodiscard]] bool alive() const { return !console.isNull(); }
};

// What `connect` asks the provider to open (not sketched by the plan; fields follow the connect
// tool's arguments, ADR-0098).
struct OpenRequest {
    std::string id;                    // "jlink:<SN>" | "ble:<id>"
    std::string profileId;             // empty = the provider's default ("chester")
    std::string targetId;              // empty = the profile's first target ("app")
    std::optional<int> speedKhz;       // J-Link only; nullopt = the provider's default
    std::optional<bool> autoReconnect; // nullopt = the provider's default
    hio::mcp::RequestContext context;  // the client (GUI consent / tab title, activity)
};

// The provider's change signal (a separate QObject so that a provider can be a QObject of its own,
// e.g. a GUI model, without multiple QObject inheritance).
class DeviceProviderNotifier : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

signals:
    // A device was opened or closed (by MCP or, in the GUI, by the user).
    void devicesChanged();
};

class IDeviceProvider {
public:
    IDeviceProvider() = default;
    IDeviceProvider(const IDeviceProvider&) = delete;
    IDeviceProvider& operator=(const IDeviceProvider&) = delete;
    IDeviceProvider(IDeviceProvider&&) = delete;
    IDeviceProvider& operator=(IDeviceProvider&&) = delete;
    virtual ~IDeviceProvider() = default;

    // Cheap: the open sessions plus the cached probe / BLE scan results. Never enumerates
    // (ADR-0162): before the first refresh() has finished the cache is empty.
    [[nodiscard]] virtual std::vector<DeviceEntry> list() = 0;
    // One probe enumeration, asynchronous (ADR-0162: the blocking enumeration runs on a worker
    // thread; the owner thread is never blocked). `done` is called exactly once on the owner
    // thread with the outcome, after the cache that list() returns was updated; it may be called
    // before refresh() returns (e.g. an error found at once). A refresh requested while one is
    // running joins it (its `done` is called with the same outcome). When the running
    // enumeration is dropped (the provider's offloader stopped or destroyed, server shutdown),
    // `done` is called with `cancelled` from a later event-loop pass (review F3). The probe
    // enumeration itself cannot be interrupted; it is bounded (about 2 s, review F2).
    virtual void refresh(std::function<void(hio::Status)> done) = 0;
    // Review V1: true while the first probe enumeration (the one the server starts at start-up)
    // runs. list_devices without `refresh` then waits for it by calling refresh(), which joins
    // the running enumeration instead of starting a second one; afterwards it lists the cache at
    // once. A provider without a start-up enumeration keeps the default (false).
    [[nodiscard]] virtual bool firstScanPending() const { return false; }
    // Opens the session, or returns the open one. Async because the GUI asks for consent first.
    // `done` is called exactly once (possibly before open() returns). The returned session may
    // still be Connecting (DeviceSession::open is non-blocking, ADR-0053).
    virtual void open(const OpenRequest& request,
                      std::function<void(hio::Result<DeviceRef>)> done) = 0;
    // Errors: mcp.device_not_open (unknown id), mcp.tool_not_allowed (GUI: a tab the user opened).
    [[nodiscard]] virtual hio::Status close(std::string_view id,
                                            const hio::mcp::RequestContext& context) = 0;
    // The open session, or nullopt.
    [[nodiscard]] virtual std::optional<DeviceRef> find(std::string_view id) = 0;
    // ADR-0105: an AI operation runs on the device ("AI: Flashing…" + a Cancel button in the GUI);
    // `cancel` is called (owner thread) when the user cancels it. endBusy() follows every
    // beginBusy().
    virtual void beginBusy(std::string_view id, QString text, std::function<void()> cancel) = 0;
    virtual void endBusy(std::string_view id) = 0;
    // ADR-0105: a console marker line in the device's ConsoleLog (ConsoleLog::appendMarker); the
    // text is already translated.
    virtual void marker(std::string_view id, QString text) = 0;
    // ADR-0105: "AI is running a command" in the console input while active.
    virtual void setAiCommandHint(std::string_view id, bool active) = 0;
    // The change notifier (owned by the provider, lives as long as it).
    [[nodiscard]] virtual DeviceProviderNotifier& notifier() = 0;
};

} // namespace hio::mcp::tools
