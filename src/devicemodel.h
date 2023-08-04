#ifndef DEVICEMODEL_H
#define DEVICEMODEL_H

#include <QtCore/QAbstractListModel>
#include <QtCore/QList>
#include <QQmlEngine>
#include "deviceinfo.h"

class DeviceModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        AddressRole,
        RSSIRole,
        WriteModeRole,
        SortRole
    };

    explicit DeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void addDevice(const QBluetoothDeviceInfo &info);
private:
    QList<DeviceInfo*> _devices;
};

#endif // DEVICEMODEL_H
