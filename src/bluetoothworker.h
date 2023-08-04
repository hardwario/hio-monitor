#ifndef BLUETOOTHWORKER_H
#define BLUETOOTHWORKER_H

#include <QObject>
#include <QtBluetooth/qbluetoothlocaldevice.h>
#include <QtBluetooth/qbluetoothdevicediscoveryagent.h>
#include <QtBluetooth/qlowenergycontroller.h>
#include <QtBluetooth/qlowenergyservice.h>
#include "deviceinfo.h"

class BluetoothWorker : public QObject {
    Q_OBJECT

public:
    BluetoothWorker(QObject *parent = nullptr);
    ~BluetoothWorker();
signals:
    void deviceDiscovered(const QBluetoothDeviceInfo &device);
    void deviceMessageReceived(const QString &message);
    void errorOccured(QString msg);
    void deviceConnected();
    void deviceDisconnected();
    void probablyUnpaired();
public slots:
    void startScan();
    void stopScan();
    void disconnect();
    void connectTo(DeviceInfo* device);
    void sendCommand(const QString &command);
private slots:
    void serviceScanDone();
    void findCharacteristics();
    void serviceDiscovered(const QBluetoothUuid &gatt);
    void handleDeviceDiscovered(const QBluetoothDeviceInfo &device);
    void serviceStateChanged(QLowEnergyService::ServiceState state);
    void descriptorRead(const QLowEnergyDescriptor &descriptor, const QByteArray &value);
    void characteristicRead(const QLowEnergyCharacteristic &info, const QByteArray &value);
    void descriptorWritten(const QLowEnergyDescriptor &descriptor, const QByteArray &value);
    void characteristicChanged(const QLowEnergyCharacteristic &characteristic, const QByteArray &value);
private:
    QBluetoothDeviceDiscoveryAgent *_deviceDiscoveryAgent;
    QBluetoothLocalDevice *_localDevice;
    QLowEnergyController *_control = nullptr;
    QLowEnergyService *_service = nullptr;

    QByteArray _msgBuffer;

    QBluetoothUuid _primaryUUID;
    QBluetoothUuid _rxUUID;
    QBluetoothUuid _txUUID;

    QLowEnergyCharacteristic chester_tx;
    QLowEnergyCharacteristic chester_rx;
    QLowEnergyService::WriteMode _writeMode;

    bool _foundPrimary = false;
    bool _connectionEstablished = false;
};

#endif // BLUETOOTHWORKER_H
