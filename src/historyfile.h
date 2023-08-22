#ifndef HISTORYFILE_H
#define HISTORYFILE_H

#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QReadWriteLock>

class HistoryFile
{
public:
    HistoryFile(const QString& fileName);
    ~HistoryFile() {
        if (_file.isOpen())
            _file.close();
    }
    void write(QString message);
    void writeMoveOnMatch(QString message);
    QVector<QString> readAll();
private:
    QFile _file;
    QString _fileName;
    QReadWriteLock _lock;
    QString createDir();
};
#endif // HISTORYFILE_H
