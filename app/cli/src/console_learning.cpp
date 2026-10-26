#include "console_learning.h"

#include "hio/device/command_tree_cache.h"
#include "hio/device/console_session.h"
#include "hio/device/device_info.h"
#include "hio/device/session_events.h"
#include "hio/device/shell_query.h"

#include <QObject>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hio::cli {

ConsoleLearning::ConsoleLearning(device::ConsoleSession& session, std::string_view profile,
                                 std::string_view target, bool readOnConnect,
                                 const std::filesystem::path& cacheDirectory)
    : m_profileId(profile), m_targetId(target), m_readOnConnect(readOnConnect),
      m_cache(cacheDirectory) {
    m_queries.setSession(&session);
    QObject::connect(&session, &device::ConsoleSession::connectionChanged, &m_context,
                     [this](const device::ConnectionEvent& event) { onConnection(event); });
    if (session.connection().state == device::ConnectionState::Connected) {
        queryInfo();
    }
}

ConsoleLearning::~ConsoleLearning() {
    ++m_attach;
    m_queries.setSession(nullptr); // detached first: no query on the released session (P4)
}

void ConsoleLearning::onConnection(const device::ConnectionEvent& event) {
    if (event.state == device::ConnectionState::Connected) {
        queryInfo();
        return;
    }
    ++m_attach;        // answers of the earlier attach are ignored
    m_queries.clear(); // the running query has ended already (it leaves with Connected)
    m_learner.reset(); // another firmware may come back
}

void ConsoleLearning::queryInfo() {
    const unsigned attach = m_attach;
    m_queries.run("info show",
                  [this, attach](const std::optional<device::ShellQueryResult>& result) {
                      // Review P7: only a real answer of this attach counts.
                      if (attach != m_attach || !result) {
                          return;
                      }
                      const device::DeviceInfo info = device::parseInfoShow(result->lines);
                      std::optional<device::FirmwareKey> key;
                      if (info.hasFirmware()) {
                          key = device::FirmwareKey{.profileId = m_profileId,
                                                    .targetId = m_targetId,
                                                    .fwName = std::string(info.fwName()),
                                                    .fwVersion = std::string(info.fwVersion())};
                      }
                      m_learner.start(std::move(key), m_readOnConnect);
                  });
}

} // namespace hio::cli
