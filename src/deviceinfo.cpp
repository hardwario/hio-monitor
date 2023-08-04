#include "deviceinfo.h"

#include <QtBluetooth/qbluetoothaddress.h>
#include <QtBluetooth/qbluetoothuuid.h>

DeviceInfo::DeviceInfo(const QBluetoothDeviceInfo &info)
: _device(info)
{
    setRSSI(info.rssi());
}

QBluetoothDeviceInfo DeviceInfo::getDevice() const {
    return _device;
}

QString DeviceInfo::getName() const {
    return _device.name();
}

int DeviceInfo::getRSSI() const {
    return _rssi;
}

void DeviceInfo::setRSSI(const int rssi) {
    _rssi = rssi;
    emit deviceChanged();
}

QLowEnergyService::WriteMode DeviceInfo::getWriteMode() const {
    return _writeMode;
}

void DeviceInfo::setWriteMode(QLowEnergyService::WriteMode mode) {
    _writeMode = mode;
}

QString DeviceInfo::getAddress() const {
#ifdef Q_OS_DARWIN
    // workaround for Core Bluetooth:
    return _device.deviceUuid().toString();
#else
    return _device.address().toString();
#endif
}

void DeviceInfo::update(const QBluetoothDeviceInfo &device) {
    _device = device;
    setRSSI(device.rssi());
    emit deviceChanged();
}
