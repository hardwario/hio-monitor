#include "viewmodels/history_store.h"

#include "gui_log.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/command_history.h"
#include "hwio/device/profile.h"

#include <QLoggingCategory>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtContainerFwd>
#include <QtCore/qnamespace.h>

#include <cstddef>
#include <deque>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace hwio::gui {

SharedHistory::SharedHistory(std::filesystem::path file,
                             std::span<const std::string_view> sensitivePrefixes)
    : m_sensitivePrefixes(sensitivePrefixes) {
    auto opened = device::CommandHistory::open(std::move(file), sensitivePrefixes);
    if (opened) {
        m_history.emplace(std::move(*opened));
    } else {
        qCWarning(lcGui) << "Console history could not be read; kept in memory for this run:"
                         << toQString(opened.error().detail);
    }
}

SharedHistory::~SharedHistory() { save(); }

void SharedHistory::add(const QString& command) {
    std::string entry = toStdString(command);
    if (entry.empty() || entry.find_first_of("\r\n") != std::string::npos ||
        entry.size() > device::CommandHistory::maxEntryBytes ||
        !device::CommandHistory::isRecordable(entry, m_sensitivePrefixes)) {
        return;
    }
    ++m_revision;
    if (m_history) {
        m_history->add(std::move(entry));
        return;
    }
    if (!m_memory.empty() && m_memory.back() == entry) {
        return;
    }
    m_memory.push_back(std::move(entry));
    while (m_memory.size() > device::CommandHistory::maxEntries) {
        m_memory.pop_front();
    }
}

const std::deque<std::string>& SharedHistory::entries() const {
    return m_history ? m_history->entries() : m_memory;
}

QStringList SharedHistory::search(const QString& text, std::size_t max) const {
    QStringList result;
    QSet<QString> seen;
    const std::deque<std::string>& all = entries();
    for (auto it = all.rbegin(); it != all.rend() && std::cmp_less(result.size(), max); ++it) {
        const QString entry = toQString(*it);
        if ((text.isEmpty() || entry.contains(text, Qt::CaseInsensitive)) &&
            !seen.contains(entry)) {
            seen.insert(entry);
            result.append(entry);
        }
    }
    return result;
}

void SharedHistory::save() {
    if (!m_history || m_history->pendingCount() == 0) {
        return;
    }
    if (const Status saved = m_history->save(); !saved) {
        qCWarning(lcGui) << "Console history could not be saved:"
                         << toQString(saved.error().detail);
    }
}

void SharedHistory::clear() {
    ++m_revision;
    m_memory.clear();
    if (m_history) {
        m_history->clear();
    }
}

HistoryStore::HistoryStore(std::filesystem::path directory) : m_directory(std::move(directory)) {}

std::filesystem::path HistoryStore::fileFor(const QString& profileId,
                                            const QString& targetId) const {
    // The CLI's file name (CommandHistory::defaultFile) in the registry's console directory.
    return m_directory /
           device::CommandHistory::defaultFile(toStdString(profileId), toStdString(targetId))
               .filename();
}

std::shared_ptr<SharedHistory> HistoryStore::history(const QString& profileId,
                                                     const QString& targetId) {
    const auto key = std::make_pair(profileId, targetId);
    if (const auto it = m_histories.find(key); it != m_histories.end()) {
        return it->second;
    }
    auto created = std::make_shared<SharedHistory>(
        fileFor(profileId, targetId),
        device::sensitivePrefixesOf(toStdString(profileId), toStdString(targetId)));
    m_histories.emplace(key, created);
    return created;
}

Status HistoryStore::clearAll() {
    for (const auto& [key, history] : m_histories) {
        history->clear();
    }
    return device::CommandHistory::clearAll(m_directory);
}

} // namespace hwio::gui
