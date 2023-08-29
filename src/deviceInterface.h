#ifndef DEVICEINTERFACE_H
#define DEVICEINTERFACE_H

#include <QObject>

class DeviceInterface : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariant history READ getCommandHistory() NOTIFY historyChanged)
    Q_PROPERTY(QString name MEMBER _name)
public:
    using QObject::QObject;
    virtual ~DeviceInterface(){}
    virtual QVariant getCommandHistory() = 0;
    QString _name;
public slots:
    virtual void sendCommand(const QString &command) = 0;
signals:
    void sendCommandSucceeded(const QString &command);
    void sendCommandFailed(const QString &command);
    void deviceMessageReceived(const QString &msg);
    void historyChanged();
};

Q_DECLARE_INTERFACE(DeviceInterface, "DeviceInterface")

#endif // DEVICEINTERFACE_H
