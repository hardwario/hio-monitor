#include "viewmodels/shortcut_registry.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"

#include <QCoreApplication>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qttranslation.h>

#include <algorithm>
#include <expected>
#include <utility>
#include <vector>

namespace hio::gui {

namespace {

Error invalid(const QString& why) {
    return makeError(ErrorCode::UsageInvalidArgument, toStdString(why));
}

// Global shortcuts whose id starts with "tab." form the "Tabs" group of the overlay.
bool isTabShortcut(const ShortcutRegistry::Entry& entry) {
    return entry.scope == ShortcutRegistry::globalScope &&
           entry.id.startsWith(QStringLiteral("tab."));
}

} // namespace

ShortcutRegistry::ShortcutRegistry(QObject* parent) : QObject(parent) {}

Status ShortcutRegistry::add(QString id, const QKeySequence& sequence, QString scope,
                             const char* descriptionSourceText) {
    if (id.isEmpty() || sequence.isEmpty() || scope.isEmpty()) {
        return std::unexpected(
            invalid(QStringLiteral("shortcut %1: empty id, sequence or scope").arg(id)));
    }
    if (m_entries.size() >= maxShortcuts) {
        return std::unexpected(invalid(
            QStringLiteral("shortcut %1: more than %2 shortcuts").arg(id).arg(maxShortcuts)));
    }
    if (find(id) != nullptr) {
        return std::unexpected(invalid(QStringLiteral("shortcut %1 is registered twice").arg(id)));
    }
    const auto clash = std::ranges::find_if(m_entries, [&](const Entry& entry) {
        const bool scopesOverlap =
            entry.scope == scope || entry.scope == globalScope || scope == globalScope;
        return scopesOverlap && entry.sequence.matches(sequence) == QKeySequence::ExactMatch;
    });
    if (clash != m_entries.end()) {
        return std::unexpected(invalid(QStringLiteral("shortcut %1 (%2, %3) clashes with %4 (%5)")
                                           .arg(id, sequence.toString(QKeySequence::PortableText),
                                                scope, clash->id, clash->scope)));
    }
    m_entries.push_back(Entry{.id = std::move(id),
                              .sequence = sequence,
                              .scope = std::move(scope),
                              .descriptionSourceText = descriptionSourceText});
    changed(); // signal
    return {};
}

void ShortcutRegistry::setScopeTitle(const QString& scope, const char* titleSourceText) {
    const auto it = std::ranges::find_if(
        m_scopeTitles, [&scope](const ScopeTitle& title) { return title.scope == scope; });
    if (it != m_scopeTitles.end()) {
        it->titleSourceText = titleSourceText;
    } else {
        m_scopeTitles.push_back(ScopeTitle{.scope = scope, .titleSourceText = titleSourceText});
    }
    changed(); // signal
}

const ShortcutRegistry::Entry* ShortcutRegistry::find(const QString& id) const {
    const auto it =
        std::ranges::find_if(m_entries, [&id](const Entry& entry) { return entry.id == id; });
    return it == m_entries.end() ? nullptr : &*it;
}

QString ShortcutRegistry::sequenceText(const QString& id) const {
    const Entry* entry = find(id);
    return entry != nullptr ? entry->sequence.toString(QKeySequence::NativeText) : QString();
}

QString ShortcutRegistry::portableText(const QString& id) const {
    const Entry* entry = find(id);
    return entry != nullptr ? entry->sequence.toString(QKeySequence::PortableText) : QString();
}

QString ShortcutRegistry::tooltip(const QString& text, const QString& id) const {
    const QString keys = sequenceText(id);
    return keys.isEmpty() ? text : QStringLiteral("%1 (%2)").arg(text, keys);
}

QVariantList ShortcutRegistry::entries() const {
    QVariantList list;
    for (const Entry& entry : m_entries) {
        list.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("scope"), entry.scope},
            {QStringLiteral("keys"), entry.sequence.toString(QKeySequence::PortableText)}});
    }
    return list;
}

QVariantList ShortcutRegistry::groups() const {
    // Group order: Tabs, General (other global shortcuts), then the page scopes in order of
    // registration. Entries with the same description in one group share a row.
    struct Group {
        QString key;
        QString title;
        QVariantList items;
        QStringList descriptions;
    };
    std::vector<Group> groups;
    const auto groupFor = [&groups](const QString& key, const QString& title) -> Group& {
        const auto it =
            std::ranges::find_if(groups, [&key](const Group& group) { return group.key == key; });
        if (it != groups.end()) {
            return *it;
        }
        groups.push_back(Group{.key = key, .title = title, .items = {}, .descriptions = {}});
        return groups.back();
    };
    // Fix the order of the two global groups even when no tab shortcut exists yet.
    (void)groupFor(QStringLiteral("#tabs"), QCoreApplication::translate("Shortcuts", "Tabs"));
    (void)groupFor(globalScope, QCoreApplication::translate("Shortcuts", "General"));

    for (const Entry& entry : m_entries) {
        QString key = entry.scope;
        QString title;
        if (isTabShortcut(entry)) {
            key = QStringLiteral("#tabs");
        } else if (entry.scope != globalScope) {
            const auto scopeTitle =
                std::ranges::find_if(m_scopeTitles, [&entry](const ScopeTitle& known) {
                    return known.scope == entry.scope;
                });
            title = scopeTitle != m_scopeTitles.end()
                        ? QCoreApplication::translate("Pages", scopeTitle->titleSourceText)
                        : entry.scope;
        }
        Group& group = groupFor(key, title);
        // Page switching (Ctrl+1, ...) uses the page title (context "Pages").
        const QString description = QCoreApplication::translate(
            entry.id.startsWith(QStringLiteral("page.")) ? "Pages" : "Shortcuts",
            entry.descriptionSourceText);
        const QString keys = entry.sequence.toString(QKeySequence::NativeText);
        const auto row = group.descriptions.indexOf(description);
        if (row >= 0) {
            QVariantMap item = group.items.at(row).toMap();
            item.insert(QStringLiteral("keys"), item.value(QStringLiteral("keys")).toString() +
                                                    QStringLiteral(", ") + keys);
            group.items[row] = item;
        } else {
            group.descriptions.append(description);
            group.items.append(QVariantMap{{QStringLiteral("keys"), keys},
                                           {QStringLiteral("description"), description}});
        }
    }

    QVariantList result;
    for (const Group& group : groups) {
        if (!group.items.isEmpty()) {
            result.append(QVariantMap{{QStringLiteral("title"), group.title},
                                      {QStringLiteral("items"), group.items}});
        }
    }
    return result;
}

void ShortcutRegistry::retranslate() {
    changed(); // signal
}

} // namespace hio::gui
