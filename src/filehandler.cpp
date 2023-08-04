#include "filehandler.h"

FileHandler::FileHandler(const QString& fileName) {
    _fileName = fileName;
    _file.setFileName(createDir());
}

QString FileHandler::createDir() {
    auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    QDir().mkdir(path);
    return path + "/" + _fileName;
}

void FileHandler::write(QString message) {
    message = message.trimmed();
    if (message.isEmpty()) return;
    _lock.lockForWrite();
    if(!_file.isOpen()) {
        _file.setFileName(createDir());
        _file.open(QIODevice::WriteOnly | QIODevice::Append);
    }
    QTextStream stream(&_file);
    stream << message << "\n";
    _file.close();
    _lock.unlock();
}

void FileHandler::writeUnique(QString message) {
    message = message.trimmed();
    if (message.isEmpty()) return;
    qDebug() << "Write unique: " << message;
    auto messages = readAll();
    if (messages.contains(message)) {
        qDebug() << "message: " << message << " is already presented in history";
        return;
    }
    _lock.lockForWrite();
    if(!_file.isOpen()) {
        _file.setFileName(createDir());
        _file.open(QIODevice::WriteOnly | QIODevice::Append);
    }
    QTextStream stream(&_file);
    stream << message << "\n";
    _file.close();
    _lock.unlock();
}

QVector<QString> FileHandler::readAll() {
    _lock.lockForRead();
    if(!_file.isOpen()) {
        _file.setFileName(createDir());
        _file.open(QIODevice::ReadOnly);
    }
    QVector<QString> result;
    QTextStream in(&_file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        result.append(line);
    }
    _file.close();
    _lock.unlock();
    return result;
}
