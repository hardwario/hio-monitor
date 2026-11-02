#pragma once

// DeviceInfoWatcher - asks a console session's device `info show` on every Connected (plan
// 0028 D7, ADR-0166, ADR-0200; the logic of plan 0021's GUI tab and CLI console, moved here).
//
// On Connected it queues `info show` on the given ShellQueryQueue (silent, ADR-0175). Only an
// answer of the current attach of the session counts (review P3/P7: the session is still
// Connected with the same ConnectionEvent::attach). On an answer it updates info(), emits
// infoChanged() and answered(), and passes the firmware identity to
// ConsoleSession::setFirmwareIdentity() for the console file name; an answer without firmware
// fields, a refused query or a timeout passes nullopt (the session may then use the boot banner,
// D10). When the console leaves Connected (Disconnected, Stopped) info() is cleared
// (infoChanged()); the session keeps its file name. The name and version are logged at info level,
// the other fields (claim token, passkey) never.
//
// Thread safety: owner thread of the session. The session and the queue must outlive the watcher
// (the queue's session is set by its owner).

#include "hwio/device/console_session.h"
#include "hwio/device/device_info.h"
#include "hwio/device/session_events.h"
#include "hwio/device/shell_query.h"

#include <QObject>
#include <QPointer>

namespace hwio::device {

class DeviceInfoWatcher : public QObject {
    Q_OBJECT

public:
    DeviceInfoWatcher(ConsoleSession& session, ShellQueryQueue& queries, QObject* parent = nullptr);
    ~DeviceInfoWatcher() override = default;
    DeviceInfoWatcher(const DeviceInfoWatcher&) = delete;
    DeviceInfoWatcher& operator=(const DeviceInfoWatcher&) = delete;
    DeviceInfoWatcher(DeviceInfoWatcher&&) = delete;
    DeviceInfoWatcher& operator=(DeviceInfoWatcher&&) = delete;

    // Of the current attach; empty otherwise.
    [[nodiscard]] const DeviceInfo& info() const noexcept { return m_info; }
    // Asks again now (0021 "Now in the device" after a flash); a no-op unless Connected.
    void refresh();

signals:
    void infoChanged();
    // An answer of the current attach arrived (also one without firmware fields).
    void answered();

private:
    void onConnection(const ConnectionEvent& event);
    void query();
    void clear();

    QPointer<ConsoleSession> m_session;
    ShellQueryQueue* m_queries; // not owned; outlives this
    DeviceInfo m_info;
};

} // namespace hwio::device
