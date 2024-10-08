#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <QObject>
#include <QThread>
#include <QSortFilterProxyModel>

#include "historyfile.h"
#include "btdeviceinfo.h"
#include "btworker.h"
#include "deviceInterface.h"

Q_DECLARE_METATYPE(BtDeviceInfo*)

class Bluetooth : public DeviceInterface {
    Q_OBJECT
    Q_INTERFACES(DeviceInterface)
    Q_PROPERTY(bool isOn READ isBluetoothEnabled NOTIFY bluetoothChanged)
public:
    explicit Bluetooth(QObject *parent = nullptr, QSortFilterProxyModel *model = nullptr, HistoryFile *commandHistoryFile = nullptr);
    Q_INVOKABLE QVariant getCommandHistory() override;
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    QVariant devices();
    bool isBluetoothEnabled();

signals:
    void scanFinished();
    void deviceDiscovered(const QBluetoothDeviceInfo &device);
    void deviceConnected();
    void bluetoothChanged();
    void deviceIsUnpaired();
    void startScanRequested();
    void stopScanRequested();
    void deviceDisconnected();
    void disconnectRequested();
    void deviceScanCanceled();
    void deviceScanFinished();
    void errorOnConnect(QString msg);
    void connectRequested(BtDeviceInfo* device);
    void sendCommandRequested(const QString &command);
public slots:
    void disconnect();
    void connectToByIndex(int index);
    void sendCommand(const QString &command) override;

private slots:
    void checkMessageForCommandFailure(const QString &message);
private:
    QSortFilterProxyModel *_model = nullptr;
    bool _bluetoothEnabled = false;

    QString _currentCommand;
    QString _lastCommand;
    HistoryFile *_commandHistoryFile;

    BluetoothWorker *_worker;
};

#endif //BLUETOOTH_H
