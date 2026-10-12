#pragma once

// DeviceTabListModel - the device tabs of the window (plan/features/0004-m3-gui-shell.md, B2;
// ADR-0043): at most maxTabs (= SessionRegistry::maxSessions) DeviceTabViewModels.
//
// Ownership: the tabs are Qt children of this model; take() hands a removed tab to the caller
// (AppViewModel deletes it on the next event-loop turn, after QML has dropped its delegate).
// Owned by AppViewModel (Qt parent). GUI thread only.

#include "hio/device/session_registry.h"
#include "viewmodels/device_tab_view_model.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QVariant>
#include <QtQmlIntegration>
#include <QtTypes>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace hio::gui {

class DeviceTabViewModel;

class DeviceTabListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppViewModel")

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool canAdd READ canAdd NOTIFY countChanged)

public:
    static constexpr std::size_t maxTabs = device::SessionRegistry::maxSessions;

    // Unscoped: Qt model roles are ints.
    enum Roles : quint16 {          // NOLINT(cppcoreguidelines-use-enum-class)
        TabRole = Qt::UserRole + 1, // DeviceTabViewModel*
        TitleRole,
        StateRole,
        HasErrorRole,
    };
    Q_ENUM(Roles)

    explicit DeviceTabListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return static_cast<int>(m_tabs.size()); }
    [[nodiscard]] bool canAdd() const { return m_tabs.size() < maxTabs; }
    // nullptr for an invalid index.
    Q_INVOKABLE [[nodiscard]] hio::gui::DeviceTabViewModel* at(int index) const;
    [[nodiscard]] int indexOf(const DeviceTabViewModel* tab) const;

    // Takes ownership (Qt parent = this). False (and the tab is deleted) when full.
    bool append(std::unique_ptr<DeviceTabViewModel> tab);
    // Removes the row and returns the tab without a parent.
    [[nodiscard]] std::unique_ptr<DeviceTabViewModel> take(int index);

signals:
    void countChanged();

private:
    void watch(DeviceTabViewModel* tab);

    std::vector<DeviceTabViewModel*> m_tabs; // children
};

} // namespace hio::gui
