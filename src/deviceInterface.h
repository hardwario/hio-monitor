#ifndef DEVICEINTERFACE_H
#define DEVICEINTERFACE_H

#include <QObject>
#include <QDebug>
#include <QRegularExpression>
#include <QFile>
#include <QThread>

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
    void batchSendCommand(QString fileName) {
        static QRegularExpression re(".*?(?=[A-Z]:)");
        fileName.remove(re);
        qDebug() << "batchSendCommand filename " << fileName;
        auto *thread = QThread::create([this, fileName]{
            QFile _file(fileName);
            if(_file.open(QIODevice::ReadOnly)) {
                QTextStream in(&_file);
                while (!in.atEnd()) {
                    auto line = in.readLine();
                    qDebug() << "batchSendCommand send " << line;
                    this->sendCommand(line);
                    QThread::msleep(150);
                }
                _file.close();
            } else {
                qDebug() << "Cannot open file " << fileName;
            }
            return;
        });
        thread->start();
    }
signals:
    void sendCommandSucceeded(const QString &command);
    void sendCommandFailed(const QString &command);
    void deviceMessageReceived(const QString &msg);
    void historyChanged();
};

Q_DECLARE_INTERFACE(DeviceInterface, "DeviceInterface")

#endif // DEVICEINTERFACE_H
