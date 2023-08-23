#ifndef HISTORYFILE_H
#define HISTORYFILE_H

#include <QFile>
#include <QObject>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QReadWriteLock>

class HistoryFile : public QObject
{
    Q_OBJECT
public:
    HistoryFile(QObject *parent = nullptr, const QString& fileName = "");
    ~HistoryFile() {
        if (_file.isOpen())
            _file.close();
    }
    void write(QString message);
    void writeMoveOnMatch(QString message);
    QVector<QString> readAll();
signals:
    void historyChanged();
private:
    QFile _file;
    QString _fileName;
    QReadWriteLock _lock;
    QString createDir();
};
#endif // HISTORYFILE_H
