#include "btdeviceinfo.h"

BtDeviceInfo::BtDeviceInfo(const QBluetoothDeviceInfo &info)
: _device(info)
{
    setRSSI(info.rssi());
}

QBluetoothDeviceInfo BtDeviceInfo::getDevice() const {
    return _device;
}

QString BtDeviceInfo::getName() const {
    return _device.name();
}

int BtDeviceInfo::getRSSI() const {
    return _rssi;
}

void BtDeviceInfo::setRSSI(const int rssi) {
    _rssi = rssi;
    emit deviceChanged();
}

QLowEnergyService::WriteMode BtDeviceInfo::getWriteMode() const {
    return _writeMode;
}

void BtDeviceInfo::setWriteMode(QLowEnergyService::WriteMode mode) {
    _writeMode = mode;
}

QString BtDeviceInfo::getAddress() const {
#ifdef Q_OS_DARWIN
    // workaround for Core Bluetooth:
    return "";
#else
    return _device.address().toString();
#endif
}

void BtDeviceInfo::update(const QBluetoothDeviceInfo &device) {
    _device = device;
    setRSSI(device.rssi());
    emit deviceChanged();
}
