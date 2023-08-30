#include "bluetooth.h"

Bluetooth::Bluetooth(QObject *parent, QSortFilterProxyModel *model, HistoryFile *commandHistoryFile)
    : DeviceInterface(parent), _model(model) {
    qRegisterMetaType<DeviceInfo*>("DeviceInfo*");

    _name = "bluetooth";
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
    connect(_worker, &BluetoothWorker::deviceScanCanceled, this, &Bluetooth::deviceScanCanceled);
    connect(_worker, &BluetoothWorker::deviceScanFinished, this, &Bluetooth::deviceScanFinished);
    // TODO: find a way to check pairing status on windows
    connect(_worker, &BluetoothWorker::probablyUnpaired, this, &Bluetooth::deviceIsUnpaired);

    _workerThread->start();
    _commandHistoryFile = commandHistoryFile;
    connect(_commandHistoryFile, &HistoryFile::historyChanged,
            this, &Bluetooth::historyChanged);
}

Bluetooth::~Bluetooth() {
    _workerThread->quit();
    _workerThread->wait();
}

QVariant Bluetooth::getCommandHistory() {
    return QVariant::fromValue(_commandHistoryFile->readAll());
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

void Bluetooth::connectToByIndex(int index) {
    DeviceInfo* device = qvariant_cast<DeviceInfo*>(_model->data(_model->index(index, 0), Qt::DisplayRole));
    if(device) {
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
        _commandHistoryFile->writeMoveOnMatch(_currentCommand);
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
