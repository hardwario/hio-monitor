#ifndef BTDEVICEMODEL_H
#define BTDEVICEMODEL_H

#include <QQmlEngine>
#include <QtCore/QList>
#include <QtCore/QAbstractListModel>

#include "btdeviceinfo.h"

class BtDeviceModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        AddressRole,
        RSSIRole,
        WriteModeRole,
        SortRole
    };

    explicit BtDeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void addDevice(const QBluetoothDeviceInfo &info);
private:
    QList<BtDeviceInfo*> _devices;
};

#endif // BTDEVICEMODEL_H
