#pragma once

// RecentSourcesModel - the Flash page's recent firmware sources
// (plan/features/0006-m3-flash-settings.md, D1): a list model over the setting flash/recent (a JSON
// list of {kind, source, time}, most recent first, at most maxEntries for all kinds together),
// filtered to one kind ("app", "lte", "modem"). `source` is a file path or "catalog:<id>". Every
// instance reads the shared key and follows its changes (SettingsStore::valueChanged), so the Flash
// pages of all tabs and the flash wizard (0010) show the same list. An unreadable value counts as
// an empty list.
//
// Ownership: child of its FirmwareSourceViewModel (Qt parent). GUI thread only.

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQmlIntegration>

#include <cstddef>
#include <vector>

namespace hio::gui {

class SettingsStore;

class RecentSourcesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by FirmwareSourceViewModel")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    static constexpr std::size_t maxEntries = 10;
    static inline const QString catalogPrefix = QStringLiteral("catalog:");

    // Unscoped: Qt roles are ints.
    enum Roles : int { // NOLINT(performance-enum-size,cppcoreguidelines-use-enum-class)
        NameRole = Qt::UserRole + 1, // file name or "Catalog ID <id>"
        DetailRole,                  // full path or the ID
        TypeRole,                    // "HEX file", "Zip archive", "Catalog ID"
        DateRole,                    // localized date and time
        SourceRole,                  // the stored source
    };

    struct Entry {
        QString kind;
        QString source;
        QDateTime time;
    };

    RecentSourcesModel(SettingsStore* settings, QObject* parent);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return static_cast<int>(m_rows.size()); }

    // The kind whose entries are shown.
    void setKind(const QString& kindId);
    // Adds `source` for `kind` at the front (an equal entry moves to the front); the list is cut to
    // maxEntries.
    void add(const QString& kindId, const QString& source);
    // Removes the shown row from the stored list.
    Q_INVOKABLE void remove(int row);
    [[nodiscard]] QString sourceAt(int row) const;
    // A live language switch (type and date texts).
    void retranslate();

    // The stored list (all kinds); exposed for tests.
    [[nodiscard]] std::vector<Entry> entries() const;

signals:
    void countChanged();

private:
    void reload();
    void store(const std::vector<Entry>& entries);

    SettingsStore* m_settings; // non-owning, outlives this model
    QString m_kind;
    std::vector<Entry> m_rows; // entries of m_kind, in stored order
};

} // namespace hio::gui
