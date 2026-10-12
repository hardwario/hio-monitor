#pragma once

// ShortcutRegistry - the keyboard shortcuts of the GUI (plan/features/0004-m3-gui-shell.md, B5;
// ADR-0051).
//
// The single source of the key sequences: the shell (AppViewModel) registers the global shortcuts,
// pages register theirs with their page id as scope. QML `Shortcut` items take their sequence from
// here (portableText()), tooltips show nativeText() ("Find (Ctrl+F)") and the F1 overlay lists
// `groups`. A sequence may be registered once per scope, and never both in a page scope and in the
// global scope (error usage.invalid_argument). Alternative keys for one action are separate ids
// with the same description ("help.shortcuts" F1, "help.shortcuts.alt" Ctrl+/); the overlay shows
// them in one row.
//
// Ownership: owned by AppViewModel (Qt parent). Thread safety: GUI thread only.

#include "hio/core/result.h"

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration>

#include <cstddef>
#include <vector>

namespace hio::gui {

class ShortcutRegistry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppViewModel")

    // [{title, items: [{keys, description}]}], translated; the global group(s) first.
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    static constexpr std::size_t maxShortcuts = 128;
    static inline const QString globalScope = QStringLiteral("global");

    struct Entry {
        QString id;
        QKeySequence sequence;
        QString scope;
        const char* descriptionSourceText = ""; // QT_TRANSLATE_NOOP("Shortcuts", "...")
    };

    explicit ShortcutRegistry(QObject* parent = nullptr);

    // scope: "global" or a page id. Errors (usage.invalid_argument): empty id or sequence,
    // duplicate id, the same sequence twice in one scope, or in a page scope and global, more than
    // maxShortcuts entries.
    [[nodiscard]] Status add(QString id, const QKeySequence& sequence, QString scope,
                             const char* descriptionSourceText);

    // The page title of a scope for the overlay (the shell sets it from the PageRegistry).
    void setScopeTitle(const QString& scope, const char* titleSourceText);

    // "Ctrl+F" (macOS "⌘F"); "" for an unknown id.
    Q_INVOKABLE [[nodiscard]] QString sequenceText(const QString& id) const;
    // The portable form for QML Shortcut.sequence ("Ctrl+F"); "" for an unknown id.
    Q_INVOKABLE [[nodiscard]] QString portableText(const QString& id) const;
    // "Find (Ctrl+F)": the tooltip of an action.
    Q_INVOKABLE [[nodiscard]] QString tooltip(const QString& text, const QString& id) const;
    // Every entry as [{id, scope, keys}] (tests: no duplicates in the map).
    Q_INVOKABLE [[nodiscard]] QVariantList entries() const;

    [[nodiscard]] QVariantList groups() const;
    [[nodiscard]] int count() const { return static_cast<int>(m_entries.size()); }

    // Re-emits `changed` so that translated texts are read again (language switch).
    void retranslate();

signals:
    void changed();

private:
    [[nodiscard]] const Entry* find(const QString& id) const;

    struct ScopeTitle {
        QString scope;
        const char* titleSourceText = "";
    };

    std::vector<Entry> m_entries;
    std::vector<ScopeTitle> m_scopeTitles;
};

} // namespace hio::gui
