#pragma once

// PageListModel - the registered pages for QML (rail, page hosts, page shortcuts;
// plan/features/0004-m3-gui-shell.md, review S9). A list model over the PageRegistry, so that a
// language switch only updates the titles (dataChanged) instead of rebuilding every page.
//
// Ownership: owned by AppViewModel (Qt parent); the registry is AppViewModel's member and is
// complete before QML reads the model. GUI thread only.

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QtQmlIntegration>
#include <QtTypes>

namespace hio::gui {

class PageRegistry;

class PageListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppViewModel")

    Q_PROPERTY(int count READ count CONSTANT)
    // Increases on every language switch (for bindings that call title()).
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    // Unscoped: Qt model roles are ints.
    enum Roles : quint16 { // NOLINT(cppcoreguidelines-use-enum-class)
        PageIdRole = Qt::UserRole + 1,
        TitleRole,
        IconRole,
        QmlRole,
        GlobalRole,
    };
    Q_ENUM(Roles)

    PageListModel(const PageRegistry& registry, QObject* parent);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }
    [[nodiscard]] int revision() const noexcept { return m_revision; }
    // "" / an empty URL for an unknown page.
    Q_INVOKABLE [[nodiscard]] QString title(const QString& pageId) const;
    Q_INVOKABLE [[nodiscard]] QUrl qml(const QString& pageId) const;

    // The titles are read again (language switch).
    void retranslate();

signals:
    void revisionChanged();

private:
    const PageRegistry& m_registry;
    int m_revision = 0;
};

} // namespace hio::gui
