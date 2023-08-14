#ifndef FILEHANDLER_H
#define FILEHANDLER_H

#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QReadWriteLock>

class FileHandler
{
public:
    FileHandler(const QString& fileName);
    ~FileHandler() {
        if (_file.isOpen())
            _file.close();
    }
    void write(QString message);
    void writeUnique(QString message);
    QVector<QString> readAll();
private:
    QFile _file;
    QString _fileName;
    QReadWriteLock _lock;
    QString createDir();
};
#endif // FILEHANDLER_H
