#ifndef BTDEVICEINFO_H
#define BTDEVICEINFO_H

#include <QtCore/qobject.h>
#include <QtCore/qstring.h>

#include <QtBluetooth/qbluetoothuuid.h>
#include <QtBluetooth/qlowenergyservice.h>
#include <QtBluetooth/qbluetoothaddress.h>
#include <QtBluetooth/qbluetoothdeviceinfo.h>

class BtDeviceInfo: public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString deviceName READ getName NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceAddress READ getAddress NOTIFY deviceChanged)
    Q_PROPERTY(int deviceRssi READ getRSSI NOTIFY deviceChanged)

    bool operator<(const BtDeviceInfo& other) const {
        return this->getRSSI() < other.getRSSI();
    }
public:
    BtDeviceInfo(const QBluetoothDeviceInfo &device);
    void update(const QBluetoothDeviceInfo &device);
    void setWriteMode(QLowEnergyService::WriteMode mode);
    QLowEnergyService::WriteMode getWriteMode() const;
    QBluetoothDeviceInfo getDevice() const;
    int getRSSI() const;
    void setRSSI(const int rssi);
    QString getName() const;
    QString getAddress() const;

signals:
    void deviceChanged();

private:
    QBluetoothDeviceInfo _device;
    QLowEnergyService::WriteMode _writeMode;
    int _rssi;
};

#endif // BTDEVICEINFO_H
