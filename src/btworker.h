#ifndef BTWORKER_H
#define BTWORKER_H

#include <QtBluetooth/qlowenergyservice.h>
#include <QtBluetooth/qlowenergycontroller.h>
#include <QtBluetooth/qbluetoothlocaldevice.h>
#include <QtBluetooth/qbluetoothdevicediscoveryagent.h>

#include "btdeviceinfo.h"

class BluetoothWorker : public QObject {
    Q_OBJECT

public:
    BluetoothWorker(QObject *parent = nullptr);
    ~BluetoothWorker();

signals:
    void deviceConnected();
    void probablyUnpaired();
    void deviceDisconnected();
    void deviceScanCanceled();
    void deviceScanFinished();
    void errorOccured(QString msg);
    void deviceMessageReceived(const QString &message);
    void deviceDiscovered(const QBluetoothDeviceInfo &device);

public slots:
    void stopScan();
    void startScan();
    void disconnect();
    void connectTo(BtDeviceInfo* device);
    void sendCommand(const QString &command);

private slots:
    void serviceScanDone();
    void findCharacteristics();
    void serviceDiscovered(const QBluetoothUuid &gatt);
    void handleDeviceDiscovered(const QBluetoothDeviceInfo &device);
    void serviceStateChanged(QLowEnergyService::ServiceState state);
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

#endif // BTWORKER_H
