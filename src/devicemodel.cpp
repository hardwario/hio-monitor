#include "devicemodel.h"

DeviceModel::DeviceModel(QObject *parent) : QAbstractListModel(parent) {}

int DeviceModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return _devices.size();
}

QVariant DeviceModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid())
        return QVariant();

    if (index.row() >= _devices.size())
        return QVariant();

    auto device = _devices.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return QVariant::fromValue(device);
    case NameRole:
        return device->getName();
    case AddressRole:
        return device->getAddress();
    case RSSIRole:
        return device->getRSSI();
    case WriteModeRole:
        return device->getWriteMode();
    case SortRole:
        return device->getRSSI();
    default:
        return QVariant();
    }
}

bool DeviceModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    Q_UNUSED(index)
    Q_UNUSED(value)
    Q_UNUSED(role)
    return false;
}

QHash<int, QByteArray> DeviceModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[AddressRole] = "address";
    roles[RSSIRole] = "rssi";
    roles[WriteModeRole] = "writeMode";
    roles[SortRole] = "sortRole";
    return roles;
}

void DeviceModel::addDevice(const QBluetoothDeviceInfo &info) {
    auto it = std::find_if(_devices.begin(), _devices.end(),
                           [&info](DeviceInfo *dev) {
                               return info.name() == dev->getName();
                           });
    if (it == _devices.end()) {
        beginInsertRows(QModelIndex(), rowCount(), rowCount());
        _devices.append(new DeviceInfo(info));
        endInsertRows();
    } else {
        (*it)->update(info);
    }
}
