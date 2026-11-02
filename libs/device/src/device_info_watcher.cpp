#include "hwio/device/device_info_watcher.h"

#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_info.h"
#include "hwio/device/firmware_identity.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/device/shell_query.h"

#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QtLogging>

#include <cstdint>
#include <optional>

namespace hwio::device {

DeviceInfoWatcher::DeviceInfoWatcher(ConsoleSession& session, ShellQueryQueue& queries,
                                     QObject* parent)
    : QObject(parent), m_session(&session), m_queries(&queries) {
    connect(&session, &ConsoleSession::connectionChanged, this, &DeviceInfoWatcher::onConnection);
    if (session.connection().state == ConnectionState::Connected) {
        query();
    }
}

void DeviceInfoWatcher::refresh() { query(); }

void DeviceInfoWatcher::onConnection(const ConnectionEvent& event) {
    switch (event.state) {
    case ConnectionState::Connected:
        query(); // also after a flash, when the console re-attaches (0021)
        break;
    case ConnectionState::Disconnected:
    case ConnectionState::Stopped:
        clear(); // review P3: answers of the earlier attach are ignored (attach check)
        break;
    case ConnectionState::Connecting:
    case ConnectionState::ProbeOpen:
        break;
    }
}

void DeviceInfoWatcher::query() {
    if (!m_session || m_session->connection().state != ConnectionState::Connected) {
        return;
    }
    if (m_session->connection().transport == Transport::JLink) {
        const TargetProfile* target =
            findTarget(m_session->profile(), m_session->consoleTargetId());
        if (target == nullptr || target->rttTerminal.empty()) {
            return; // no console (lte): nothing to ask
        }
    }
    const std::uint64_t attach = m_session->connection().attach;
    const QPointer<DeviceInfoWatcher> self(this);
    m_queries->run("info show", [self, attach](const std::optional<ShellQueryResult>& result) {
        if (!self || !self->m_session) {
            return;
        }
        ConsoleSession& session = *self->m_session;
        // Review P3/P7: only an answer of this attach of this session counts.
        if (session.connection().state != ConnectionState::Connected ||
            session.connection().attach != attach) {
            return;
        }
        std::optional<FirmwareIdentity> identity;
        if (result) {
            self->m_info = parseInfoShow(result->lines);
            identity = identityFromInfo(self->m_info);
            qCInfo(log::lcDevice).noquote()
                << "Firmware in the device:" << toQString(self->m_info.fwName())
                << toQString(self->m_info.fwVersion());
        }
        // A refused or aborted query keeps what is known; the session hears "no identity" (D10).
        if (const Status status = session.setFirmwareIdentity(identity, attach); !status) {
            qCWarning(log::lcDevice).noquote() << "Firmware identity not passed to the session:"
                                               << toQString(status.error().detail);
        }
        if (result) {
            self->infoChanged(); // signal
            self->answered();    // signal
        }
    });
}

void DeviceInfoWatcher::clear() {
    if (m_info.fields.empty()) {
        return;
    }
    m_info = {};
    infoChanged(); // signal
}

} // namespace hwio::device
