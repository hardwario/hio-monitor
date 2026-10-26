#pragma once

// CommandTreeLearner - learns the device shell's command tree (plan 0021 phase 2, ADR-0170),
// shared by the GUI console and the CLI console.
//
// start(key, readOnConnect) begins a console attach: with a firmware key (profile, target and
// `info show` fw-name / fw-version) a cached tree is loaded (CommandTreeCache); without a cached
// tree the top level is learned with `help` at once when readOnConnect is on, else on the first
// requestTopLevel() (the first time a suggestion needs it). requestChildren(path) learns the
// subcommands of a known command with `<path> -h` once (a command without a list is remembered as
// having none). Every learned step is stored in the cache when the key is known. Queries run
// through the ShellQueryQueue (silent, one at a time); a refused, timed-out or truncated query
// leaves the tree as it is (review P2) and the step can be asked again - at once when the query
// did not run, after retryInterval when it ran (so that typing does not hold the user's commands
// again and again).
//
// tree() is an immutable snapshot (a new one per change), so another thread may keep using an
// older snapshot (the CLI console's input thread).
//
// Thread safety: owner thread of the session (and of the queue).

#include "hio/device/command_tree.h"
#include "hio/device/command_tree_cache.h"
#include "hio/device/shell_query.h"

#include <QObject>

#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace hio::device {

class CommandTreeLearner : public QObject {
    Q_OBJECT

public:
    static constexpr std::chrono::seconds retryInterval{30};

    // `queue` and `cache` (may be nullptr: no caching) must outlive the learner.
    CommandTreeLearner(ShellQueryQueue& queue, CommandTreeCache* cache, QObject* parent = nullptr);

    void start(std::optional<FirmwareKey> key, bool readOnConnect);
    // Forgets the tree (disconnect).
    void reset();
    void requestTopLevel();
    void requestChildren(const std::vector<std::string>& path);

    [[nodiscard]] std::shared_ptr<const CommandTree> tree() const { return m_tree; }
    [[nodiscard]] bool reading() const noexcept { return m_pendingQueries > 0; }

signals:
    void treeChanged();
    void readingChanged();

private:
    void learned(const std::vector<std::string>& path,
                 const std::optional<ShellQueryResult>& result);
    void store();
    void forget(const std::vector<std::string>& path, bool ran);
    [[nodiscard]] bool backingOff(const std::vector<std::string>& path) const;
    void queryStarted();
    void queryEnded();

    ShellQueryQueue* m_queue;  // non-owning
    CommandTreeCache* m_cache; // non-owning, may be nullptr
    std::optional<FirmwareKey> m_key;
    std::shared_ptr<const CommandTree> m_tree;
    std::set<std::vector<std::string>> m_attempted; // bounded by CommandTree::maxNodes
    // Paths whose query ran without a usable answer: not asked again before the time.
    std::map<std::vector<std::string>, std::chrono::steady_clock::time_point>
        m_retryAfter; // bounded by CommandTree::maxNodes
    bool m_topAttempted = false;
    int m_pendingQueries = 0;
    unsigned m_generation = 0; // a reset() makes running queries stale
};

} // namespace hio::device
