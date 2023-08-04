#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <QSortFilterProxyModel>
#include <QObject>
#include <QThread>
#include "filehandler.h"
#include "deviceinfo.h"
#include "bluetoothworker.h"

Q_DECLARE_METATYPE(DeviceInfo*)

class Bluetooth : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isOn READ isBluetoothEnabled NOTIFY bluetoothChanged)
public:
    explicit Bluetooth(QObject *parent = nullptr, QSortFilterProxyModel *model = nullptr, FileHandler *commandHistoryFile = nullptr);
    ~Bluetooth();

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
    void sendCommandSucceeded(const QString &command);
    void sendCommandFailed(const QString &command);
    void deviceMessageReceived(const QString &message);
    void connectRequested(DeviceInfo* device);
    void sendCommandRequested(const QString &command);
public slots:
    void disconnect();
    void connectToByIndex(int index);
    void sendCommand(const QString &command);

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
