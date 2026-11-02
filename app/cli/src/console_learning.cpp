#include "console_learning.h"

#include "hwio/device/command_tree_cache.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_info.h"
#include "hwio/device/device_info_watcher.h"
#include "hwio/device/session_events.h"

#include <QObject>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hwio::cli {

ConsoleLearning::ConsoleLearning(device::ConsoleSession& session, std::string_view profile,
                                 std::string_view target, bool readOnConnect,
                                 const std::filesystem::path& cacheDirectory, bool learn)
    : m_profileId(profile), m_targetId(target), m_readOnConnect(readOnConnect), m_learn(learn),
      m_cache(cacheDirectory) {
    m_queries.setSession(&session);
    QObject::connect(&session, &device::ConsoleSession::connectionChanged, &m_context,
                     [this](const device::ConnectionEvent& event) { onConnection(event); });
    // Plan 0028 (D7): queries `info show` now when connected and on every later Connected.
    m_watcher.emplace(session, m_queries);
    QObject::connect(&*m_watcher, &device::DeviceInfoWatcher::answered, &m_context,
                     [this] { onAnswered(); });
}

ConsoleLearning::~ConsoleLearning() {
    m_watcher.reset();
    m_queries.setSession(nullptr); // detached first: no query on the released session (P4)
}

void ConsoleLearning::onConnection(const device::ConnectionEvent& event) {
    if (event.state == device::ConnectionState::Connected) {
        return; // the watcher asks `info show`
    }
    m_queries.clear(); // the running query has ended already (it leaves with Connected)
    m_learner.reset(); // another firmware may come back
}

void ConsoleLearning::onAnswered() {
    if (!m_learn || !m_watcher) {
        return;
    }
    const device::DeviceInfo& info = m_watcher->info();
    std::optional<device::FirmwareKey> key;
    if (info.hasFirmware()) {
        key = device::FirmwareKey{.profileId = m_profileId,
                                  .targetId = m_targetId,
                                  .fwName = std::string(info.fwName()),
                                  .fwVersion = std::string(info.fwVersion())};
    }
    m_learner.start(std::move(key), m_readOnConnect);
}

} // namespace hwio::cli
