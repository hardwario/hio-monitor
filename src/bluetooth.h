#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <QSortFilterProxyModel>
#include <QObject>
#include <QThread>
#include "filehandler.h"
#include "deviceinfo.h"
#include "bluetoothworker.h"
#include "deviceInterface.h"

Q_DECLARE_METATYPE(DeviceInfo*)

class Bluetooth : public DeviceInterface {
    Q_OBJECT
    Q_INTERFACES(DeviceInterface)
    Q_PROPERTY(bool isOn READ isBluetoothEnabled NOTIFY bluetoothChanged)
public:
    explicit Bluetooth(QObject *parent = nullptr, QSortFilterProxyModel *model = nullptr, FileHandler *commandHistoryFile = nullptr);
    ~Bluetooth();
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
    void errorOnConnect(QString msg);
    void connectRequested(DeviceInfo* device);
    void sendCommandRequested(const QString &command);
public slots:
    void disconnect();
    void connectToByIndex(int index);
    void sendCommand(const QString &command) override;

private slots:
    bool isPaired(DeviceInfo* device);
    void checkMessageForCommandFailure(const QString &message);
private:
    QSortFilterProxyModel *_model = nullptr;
    bool _bluetoothEnabled = false;

    QString _currentCommand;
    FileHandler *_commandHistoryFile;
  
    // Ensure Bluetooth won't block UI
    QThread *_workerThread;
    BluetoothWorker *_worker;
};

#endif //BLUETOOTH_H
