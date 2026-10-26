#include "hio/device/command_tree_learner.h"

#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/device/command_capture.h"
#include "hio/device/command_tree.h"
#include "hio/device/command_tree_cache.h"
#include "hio/device/shell_query.h"

#include <QLoggingCategory>
#include <QObject>
#include <QtLogging>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hio::device {

CommandTreeLearner::CommandTreeLearner(ShellQueryQueue& queue, CommandTreeCache* cache,
                                       QObject* parent)
    : QObject(parent), m_queue(&queue), m_cache(cache) {}

void CommandTreeLearner::start(std::optional<FirmwareKey> key, bool readOnConnect) {
    reset();
    m_key = std::move(key);
    if (m_key && m_cache != nullptr) {
        if (auto cached = m_cache->load(*m_key); cached && !cached->empty()) {
            qCInfo(log::lcDevice).noquote()
                << "Command tree from the cache:" << toQString(m_key->fwName)
                << toQString(m_key->fwVersion) << cached->nodeCount() << "commands";
            m_tree = std::make_shared<const CommandTree>(std::move(*cached));
            m_topAttempted = true;
            treeChanged(); // signal
            return;
        }
    }
    if (readOnConnect) {
        requestTopLevel();
    }
}

void CommandTreeLearner::reset() {
    ++m_generation;
    const bool hadTree = static_cast<bool>(m_tree);
    const bool wasReading = reading();
    m_key.reset();
    m_tree.reset();
    m_attempted.clear();
    m_retryAfter.clear();
    m_topAttempted = false;
    m_pendingQueries = 0;
    if (hadTree) {
        treeChanged(); // signal
    }
    if (wasReading) {
        readingChanged(); // signal
    }
}

void CommandTreeLearner::requestTopLevel() {
    if (m_topAttempted || backingOff({})) {
        return;
    }
    m_topAttempted = true;
    queryStarted();
    const unsigned generation = m_generation;
    m_queue->run("help", [this, generation](const std::optional<ShellQueryResult>& result) {
        if (generation == m_generation) {
            learned({}, result);
            queryEnded();
        }
    });
}

void CommandTreeLearner::requestChildren(const std::vector<std::string>& path) {
    if (!m_tree) {
        requestTopLevel();
        return;
    }
    const CommandNode* node = m_tree->find(path);
    if (path.empty() || node == nullptr || node->childrenKnown ||
        path.size() >= CommandTree::maxDepth || m_attempted.size() >= CommandTree::maxNodes ||
        backingOff(path) || !m_attempted.insert(path).second) {
        return;
    }
    std::string command;
    for (const std::string& word : path) {
        command += word + " ";
    }
    command += "-h";
    queryStarted();
    const unsigned generation = m_generation;
    m_queue->run(std::move(command),
                 [this, generation, path](const std::optional<ShellQueryResult>& result) {
                     if (generation == m_generation) {
                         learned(path, result);
                         queryEnded();
                     }
                 });
}

void CommandTreeLearner::learned(const std::vector<std::string>& path,
                                 const std::optional<ShellQueryResult>& result) {
    if (!result || result->end == CaptureEnd::Timeout || result->truncated) {
        // Review P2: refused, disconnected, timed out or cut: nothing is applied or cached, the
        // step can be learned again (after retryInterval when the query ran).
        forget(path, result.has_value());
        return;
    }
    const std::vector<CommandEntry> entries = parseHelp(result->lines);
    if (path.empty() && entries.empty()) {
        qCInfo(log::lcDevice) << "No command list in the answer to `help`";
        forget(path, true);
        return;
    }
    auto tree = m_tree ? std::make_shared<CommandTree>(*m_tree) : std::make_shared<CommandTree>();
    if (!tree->setChildren(path, entries)) {
        return;
    }
    m_tree = std::move(tree);
    store();
    treeChanged(); // signal
}

void CommandTreeLearner::forget(const std::vector<std::string>& path, bool ran) {
    if (path.empty()) {
        m_topAttempted = false;
    } else {
        m_attempted.erase(path);
    }
    if (ran && m_retryAfter.size() < CommandTree::maxNodes) {
        m_retryAfter[path] = std::chrono::steady_clock::now() + retryInterval;
    }
}

bool CommandTreeLearner::backingOff(const std::vector<std::string>& path) const {
    const auto found = m_retryAfter.find(path);
    return found != m_retryAfter.end() && std::chrono::steady_clock::now() < found->second;
}

void CommandTreeLearner::store() {
    if (!m_key || m_cache == nullptr || !m_tree) {
        return;
    }
    if (const Status stored = m_cache->store(*m_key, *m_tree); !stored) {
        qCWarning(log::lcDevice).noquote()
            << "Command tree not cached:" << toQString(stored.error().detail);
    }
}

void CommandTreeLearner::queryStarted() {
    if (m_pendingQueries++ == 0) {
        readingChanged(); // signal
    }
}

void CommandTreeLearner::queryEnded() {
    if (m_pendingQueries == 0) {
        return;
    }
    --m_pendingQueries;
    if (m_pendingQueries == 0) {
        readingChanged(); // signal
    }
}

} // namespace hio::device
