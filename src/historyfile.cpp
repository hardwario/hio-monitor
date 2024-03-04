#include "historyfile.h"

HistoryFile::HistoryFile(QObject *parent, const QString& fileName) : QObject(parent)
{
    _fileName = fileName;
    _file.setFileName(createFile());
}

QString HistoryFile::createFile() {
    auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    QDir().mkdir(path);
    return path + "/" + _fileName;
}

void HistoryFile::write(QString message) {
    message = message.trimmed();
    if (message.isEmpty()) return;

    _lock.lockForWrite();

    if(!_file.isOpen()) {
        _file.open(QIODevice::WriteOnly | QIODevice::Append);
    }

    QTextStream stream(&_file);
    stream << message << "\n";
    _file.close();
    _lock.unlock();

    emit historyChanged();
}

void HistoryFile::writeMoveOnMatch(QString message) {
    message = message.trimmed();
    if (message.isEmpty()) return;

    auto messages = readAll();
    auto ind = messages.lastIndexOf(message);

    if (ind != -1) {
        messages.remove(ind);
        messages.append(message);

        _lock.lockForWrite();

        if(!_file.isOpen()) {

            _file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        }

        QTextStream stream(&_file);
        for(auto i = 0; i < messages.size(); i++) {
            stream << messages.at(i) << "\n";
        }

        _file.close();
        _lock.unlock();
    } else {
        write(message);
    }

    emit historyChanged();
}

QVector<QString> HistoryFile::readAll() {
    _lock.lockForRead();

    if(!_file.isOpen()) {
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
