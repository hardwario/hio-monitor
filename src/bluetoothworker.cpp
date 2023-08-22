#include "bluetoothworker.h"

BluetoothWorker::BluetoothWorker(QObject *parent) : QObject(parent) {
    _deviceDiscoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    _deviceDiscoveryAgent->setLowEnergyDiscoveryTimeout(5000);
    _localDevice = new QBluetoothLocalDevice(this);

    connect(_deviceDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BluetoothWorker::handleDeviceDiscovered);
    connect(_deviceDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            [this](QBluetoothDeviceDiscoveryAgent::Error error) {
                auto errorStr = _control->errorString();
                qDebug() << "Device discovery agent error: " << error;
                emit errorOccured("Cannot connect to remote device due to: " + errorStr);
            });
    connect(_deviceDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BluetoothWorker::deviceScanFinished);
    connect(_deviceDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::canceled,
            this, &BluetoothWorker::deviceScanCanceled);

    _primaryUUID = QBluetoothUuid::fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    _rxUUID = QBluetoothUuid::fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
    _txUUID = QBluetoothUuid::fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
}

BluetoothWorker::~BluetoothWorker() {
    stopScan();
    disconnect();
    delete _deviceDiscoveryAgent;
    delete _control;
    delete _service;
}

void BluetoothWorker::startScan() {
    qDebug() << "Scanning...";
    _deviceDiscoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}
void BluetoothWorker::stopScan() {
    _deviceDiscoveryAgent->stop();
}
void BluetoothWorker::disconnect() {
    if (_control)
        _control->disconnectFromDevice();
    qDebug() << "Disconnected from device";
}

void BluetoothWorker::handleDeviceDiscovered(const QBluetoothDeviceInfo &device) {
    if (device.name().contains("CHESTER")) {
        qDebug() << "Found new device:" << device.name() << '(' << device.address().toString() << ')' << "Rssi: " << device.rssi();
        emit deviceDiscovered(device);
    }
}

void BluetoothWorker::connectTo(DeviceInfo* device) {
    // Disconnect and delete old connection
    if (_control) {
        _control->disconnectFromDevice();
        _control->deleteLater();
        _control = nullptr;
    }

    qDebug() << "Connecting to device" << device->getName();
    _control = QLowEnergyController::createCentral(device->getDevice(), this);
    _control->setRemoteAddressType(QLowEnergyController::PublicAddress);

    connect(_control, &QLowEnergyController::serviceDiscovered,
            this, &BluetoothWorker::serviceDiscovered);
    connect(_control, &QLowEnergyController::discoveryFinished,
            this, &BluetoothWorker::serviceScanDone);

    connect(_control, &QLowEnergyController::errorOccurred, this,
            [this](QLowEnergyController::Error error) {
                auto errorStr = _control->errorString();
                if (error == QLowEnergyController::UnknownError) {
                    errorStr = "Device is not powered on";
                }
                qDebug() << "Cannot connect to remote device due to: " << error;
                emit errorOccured("Cannot connect to remote device due to: " + errorStr);
            });
    connect(_control, &QLowEnergyController::connected, this,
            [this]() {
                qDebug() << "Controller connected. Search services...";
                stopScan();
                _control->discoverServices();
            });
    connect(_control, &QLowEnergyController::disconnected, this,
            [this]() {
                qDebug() << "LowEnergy controller disconnected";
                stopScan();
                _connectionEstablished = false;
                emit deviceDisconnected();
            });
    // Connect
    _control->connectToDevice();
}

void BluetoothWorker::findCharacteristics() {
    for (const auto &characteristic : _service->characteristics()) {
        qDebug() << "Characteristic: " << characteristic.uuid();
        if (characteristic.uuid() == _rxUUID) {
            qDebug() << "Found RX characteristic";
            chester_rx = characteristic;
            if (characteristic.properties() & QLowEnergyCharacteristic::Write) {
                qDebug() << "The characteristic is writable";
            } else {
                qDebug() << "The characteristic is not writable";
            }
            _writeMode = QLowEnergyService::WriteMode::WriteWithResponse;
            if (characteristic.properties() == QLowEnergyCharacteristic::PropertyType::WriteNoResponse)
                _writeMode = QLowEnergyService::WriteMode::WriteWithoutResponse;
            qDebug() << "Write mode: " << _writeMode;
        }
        if (characteristic.uuid() == _txUUID) {
            qDebug() << "Found TX characteristic";
            chester_tx = characteristic;
            if (chester_tx.properties() & QLowEnergyCharacteristic::Notify) {
                qDebug() << "The characteristic supports notifications";
            } else {
                qDebug() << "The characteristic does not support notifications";
            }
        }
    }
}

void BluetoothWorker::serviceDiscovered(const QBluetoothUuid &gatt) {
    if (gatt == _primaryUUID) {
        qDebug() << "Primary service discovered";
        if (_service) {
            _service ->deleteLater();
            _service = nullptr;
        }
        _service = _control->createServiceObject(gatt, this);
        if (!_service) {
            qDebug() << "Cannot create service for uuid: " << gatt.toString();
            return;
        }
        _foundPrimary = true;
    }
}

void BluetoothWorker::serviceScanDone() {
    if (!_foundPrimary) {
        qDebug() << "Primary service not found";
        return;
    }

    connect(_service, &QLowEnergyService::stateChanged,
            this, &BluetoothWorker::serviceStateChanged);
    connect(_service, &QLowEnergyService::characteristicChanged,
            this, &BluetoothWorker::characteristicChanged);
    connect(_service, &QLowEnergyService::characteristicRead,
            this, &BluetoothWorker::characteristicRead);
    connect(_service, &QLowEnergyService::descriptorWritten,
            this, &BluetoothWorker::descriptorWritten);
    connect(_service, &QLowEnergyService::descriptorRead,
            this, &BluetoothWorker::descriptorRead);

    connect(_service, &QLowEnergyService::errorOccurred, this,
            [this](QLowEnergyService::ServiceError error) {
                qDebug() << "Write failed with: " << error;
                qDebug() << "High chances that device is not paired";
                _connectionEstablished = false;
                emit probablyUnpaired();
            });

    _service->discoverDetails();

    findCharacteristics();
}

void BluetoothWorker::characteristicRead(const QLowEnergyCharacteristic &characteristic, const QByteArray &value) {
    qDebug() << "Characteristic read: " << characteristic.uuid() << ", value: " << value;
}

void BluetoothWorker::descriptorWritten(const QLowEnergyDescriptor &descriptor, const QByteArray &value) {
    if (descriptor.isValid() && !_connectionEstablished) {
        _connectionEstablished = true;
        emit deviceConnected();
    }
    qDebug() << "Descriptor written: " << descriptor.uuid() << ", new value: " << value;
}

void BluetoothWorker::descriptorRead(const QLowEnergyDescriptor &descriptor, const QByteArray &value) {
    qDebug() << "Descriptor read: " << descriptor.uuid() << ", value: " << value;
}

void BluetoothWorker::characteristicChanged(const QLowEnergyCharacteristic &characteristic, const QByteArray &value) {
    _msgBuffer.append(value);

    qDebug() << characteristic.uuid() << " changed with value: " << value;

    if (!_msgBuffer.endsWith("bt_nus:~$ "))
        return; // if the message isn't complete yet, wait for more data

    auto receivedMessage = QString::fromUtf8(_msgBuffer).trimmed();
    _msgBuffer.clear();

    qDebug() << "Message received from device:" << receivedMessage;
    emit deviceMessageReceived(receivedMessage);
}

void BluetoothWorker::serviceStateChanged(QLowEnergyService::ServiceState newState) {
    qDebug() << "State changed: " << newState;
    if (newState == QLowEnergyService::RemoteServiceDiscovered) {
        findCharacteristics();
        if (!chester_tx.isValid()) {
            qCritical() << "tx characteristic is invalid";
        }
        auto descriptor = chester_tx.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
        if (descriptor.isValid()) {
            _service->writeDescriptor(descriptor, QByteArray::fromHex("0100"));
        } else {
            qCritical() << "Descriptor for characteristic" << chester_tx.uuid() << "is invalid. Cannot write to enable notifications";
        }
    }
}

void BluetoothWorker::sendCommand(const QString &command) {
    if (!_service) {
        qDebug() << "Bt service is not init, command send abonded";
        return;
    }
    if (_service->state() != QLowEnergyService::RemoteServiceDiscovered) {
        emit errorOccured("Service is not in discovered state, can not write to it. Try reset the device.");
        return;
    }
    if (!chester_rx.isValid()) {
        qCritical() << "rx characteristic is invalid";
        emit errorOccured("Devices's write characteristic is invalid. Try reset the device.");
        return;
    }
    QByteArray value = command.toUtf8();
    value.append('\n');
    qDebug() << "Sending: " << value;
    _service->writeCharacteristic(chester_rx, value, _writeMode);
}
