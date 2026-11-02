#pragma once

// The device command tree for Tab completion in the interactive CLI console (plan 0021,
// ADR-0170, ADR-0175): `info show` for the firmware key after every attach, then the cached tree
// or `help` (on connect when console/readCommandsOnConnect is on, else at the first Tab);
// subcommands on demand. Forgotten when the console leaves Connected (review P6); answers of an
// earlier attach and refused queries are ignored (review P3, P7). The queries are silent
// (ShellQuery) and run on the owner thread of the session during the console.
//
// Plan 0028 (D7): `info show` is asked by the backend DeviceInfoWatcher, which also passes the
// firmware identity to the session for the console file name. Without learning (a console that
// is not interactive but writes a generated console file) only the watcher runs.
//
// Thread safety: owner thread of the session. The session must outlive this object.

#include "hwio/device/command_tree_cache.h"
#include "hwio/device/command_tree_learner.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_info_watcher.h"
#include "hwio/device/session_events.h"
#include "hwio/device/shell_query.h"

#include <QObject>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace hwio::cli {

class ConsoleLearning {
public:
    // `cacheDirectory`: where learned trees are kept (CommandTreeCache::defaultDirectory()).
    // `learn` = false: only the DeviceInfoWatcher (console file name), no command tree.
    ConsoleLearning(device::ConsoleSession& session, std::string_view profile,
                    std::string_view target, bool readOnConnect,
                    const std::filesystem::path& cacheDirectory, bool learn = true);
    ~ConsoleLearning();
    ConsoleLearning(const ConsoleLearning&) = delete;
    ConsoleLearning& operator=(const ConsoleLearning&) = delete;
    ConsoleLearning(ConsoleLearning&&) = delete;
    ConsoleLearning& operator=(ConsoleLearning&&) = delete;

    [[nodiscard]] device::CommandTreeLearner& learner() { return m_learner; }

private:
    void onConnection(const device::ConnectionEvent& event);
    void onAnswered();

    std::string m_profileId;
    std::string m_targetId;
    bool m_readOnConnect;
    bool m_learn;
    QObject m_context; // the connections' context (gone before the members below)
    device::ShellQueryQueue m_queries;
    device::CommandTreeCache m_cache;
    device::CommandTreeLearner m_learner{m_queries, &m_cache};
    std::optional<device::DeviceInfoWatcher> m_watcher; // destroyed first
};

} // namespace hwio::cli
