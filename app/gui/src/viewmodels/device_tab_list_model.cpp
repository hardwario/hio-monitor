#include "viewmodels/device_tab_list_model.h"

#include "viewmodels/device_tab_view_model.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QQmlEngine>
#include <QVariant>

#include <algorithm>
#include <cstddef>
#include <memory>

namespace hio::gui {

DeviceTabListModel::DeviceTabListModel(QObject* parent) : QAbstractListModel(parent) {}

int DeviceTabListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant DeviceTabListModel::data(const QModelIndex& index, int role) const {
    DeviceTabViewModel* tab = at(index.row());
    if (!index.isValid() || tab == nullptr) {
        return {};
    }
    switch (role) {
    case TabRole:
        return QVariant::fromValue(tab);
    case TitleRole:
        return tab->title();
    case StateRole:
        return static_cast<int>(tab->state());
    case HasErrorRole:
        return tab->hasError();
    default:
        return {};
    }
}

QHash<int, QByteArray> DeviceTabListModel::roleNames() const {
    return {
        {TabRole, "tab"}, {TitleRole, "title"}, {StateRole, "state"}, {HasErrorRole, "hasError"}};
}

DeviceTabViewModel* DeviceTabListModel::at(int index) const {
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    return m_tabs.at(static_cast<std::size_t>(index));
}

int DeviceTabListModel::indexOf(const DeviceTabViewModel* tab) const {
    const auto it = std::ranges::find(m_tabs, tab);
    return it == m_tabs.end() ? -1 : static_cast<int>(it - m_tabs.begin());
}

bool DeviceTabListModel::append(std::unique_ptr<DeviceTabViewModel> tab) {
    if (!canAdd() || !tab) {
        return false;
    }
    beginInsertRows({}, count(), count());
    // Qt parent ownership from here on.
    DeviceTabViewModel* raw = tab.release();
    raw->setParent(this);
    // Returned to QML through roles and properties: never owned by the JavaScript engine.
    QQmlEngine::setObjectOwnership(raw, QQmlEngine::CppOwnership);
    m_tabs.push_back(raw);
    endInsertRows();
    watch(raw);
    countChanged(); // signal
    return true;
}

std::unique_ptr<DeviceTabViewModel> DeviceTabListModel::take(int index) {
    DeviceTabViewModel* tab = at(index);
    if (tab == nullptr) {
        return nullptr;
    }
    beginRemoveRows({}, index, index);
    m_tabs.erase(m_tabs.begin() + index);
    tab->disconnect(this);
    tab->setParent(nullptr);
    endRemoveRows();
    countChanged(); // signal
    return std::unique_ptr<DeviceTabViewModel>(tab);
}

void DeviceTabListModel::watch(DeviceTabViewModel* tab) {
    const auto changed = [this, tab](int role) {
        const int row = indexOf(tab);
        if (row >= 0) {
            dataChanged(index(row), index(row), {role}); // signal
        }
    };
    connect(tab, &DeviceTabViewModel::titleChanged, this, [changed] { changed(TitleRole); });
    connect(tab, &DeviceTabViewModel::stateChanged, this, [changed] {
        changed(StateRole);
        changed(HasErrorRole);
    });
}

} // namespace hio::gui
