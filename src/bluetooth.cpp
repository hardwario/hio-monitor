#include "bluetooth.h"

Bluetooth::Bluetooth(QObject *parent, QSortFilterProxyModel *model, FileHandler *commandHistoryFile)
    : QObject(parent), _model(model) {
    qRegisterMetaType<DeviceInfo*>("DeviceInfo*");

    _workerThread = new QThread(this);
    _worker = new BluetoothWorker();
    _worker->moveToThread(_workerThread);

    connect(_workerThread, &QThread::finished, _worker, &QObject::deleteLater);

    connect(this, &Bluetooth::startScanRequested, _worker, &BluetoothWorker::startScan);
    connect(this, &Bluetooth::stopScanRequested, _worker, &BluetoothWorker::stopScan);
    connect(this, &Bluetooth::disconnectRequested, _worker, &BluetoothWorker::disconnect);
    connect(this, &Bluetooth::connectRequested, _worker, &BluetoothWorker::connectTo);
    connect(this, &Bluetooth::sendCommandRequested, _worker, &BluetoothWorker::sendCommand);
    connect(_worker, &BluetoothWorker::deviceDiscovered, this, &Bluetooth::deviceDiscovered);
    connect(_worker, &BluetoothWorker::deviceMessageReceived, this, &Bluetooth::checkMessageForCommandFailure);
    connect(_worker, &BluetoothWorker::errorOccured, this, &Bluetooth::errorOnConnect);
    connect(_worker, &BluetoothWorker::deviceConnected, this, &Bluetooth::deviceConnected);
    connect(_worker, &BluetoothWorker::deviceDisconnected, this, &Bluetooth::deviceDisconnected);
    // TODO: find a way to check pairing status on windows
    connect(_worker, &BluetoothWorker::probablyUnpaired, this, &Bluetooth::deviceIsUnpaired);

    _workerThread->start();
    _commandHistoryFile = commandHistoryFile;
}

Bluetooth::~Bluetooth() {
    _workerThread->quit();
    _workerThread->wait();
}

void Bluetooth::startScan() {
    if (!isBluetoothEnabled()) {
        emit bluetoothChanged();
        qDebug() << "Bluetooth is disabled";
        return;
    }
    emit startScanRequested();
}

void Bluetooth::stopScan() {
    emit stopScanRequested();
}

void Bluetooth::disconnect() {
    emit disconnectRequested();
}

bool Bluetooth::isPaired(DeviceInfo* device) {
QBluetoothLocalDevice localDevice;
#if defined Q_OS_LINUX || defined Q_OS_DARWIN
    auto deviceAddr = QBluetoothAddress(device->getAddress());
    if (localDevice.pairingStatus(deviceAddr) == QBluetoothLocalDevice::Unpaired) {
        localDevice.requestPairing(deviceAddr, QBluetoothLocalDevice::AuthorizedPaired);
    }

    if (localDevice.pairingStatus(deviceAddr) == QBluetoothLocalDevice::Unpaired) {
        qDebug() << "Device is in unpaired state";
        emit deviceIsUnpaired();
        return false;
    }
    return true;
#else
    // for some reason pairingStatus on Windows always returns Unpaired
    localDevice.pairingStatus(QBluetoothAddress(device->getAddress()));
    return true;
#endif
}

void Bluetooth::connectToByIndex(int index) {
    DeviceInfo* device = qvariant_cast<DeviceInfo*>(_model->data(_model->index(index, 0), Qt::DisplayRole));
    if(device) {
        if(!isPaired(device)) return;
        emit connectRequested(device);
    } else {
        qDebug() << "cannot retrieve device from model";
    }
}

void Bluetooth::checkMessageForCommandFailure(const QString &message) {
    if (message.contains("command not found") ||
        message.contains("wrong")) {
        qDebug() << "Command failed";
        emit sendCommandFailed(_currentCommand);
    } else {
        qDebug() << "Bluetooth send command succeeded: " << _currentCommand;
        emit sendCommandSucceeded(_currentCommand);
        _commandHistoryFile->writeUnique(_currentCommand);
    }
    emit deviceMessageReceived(message);
}

void Bluetooth::sendCommand(const QString &command) {
    _currentCommand = command;
    emit sendCommandRequested(command);
}

bool Bluetooth::isBluetoothEnabled() {
    QBluetoothLocalDevice localDevice;
    return localDevice.isValid() && 
        localDevice.hostMode() != QBluetoothLocalDevice::HostPoweredOff;
}
