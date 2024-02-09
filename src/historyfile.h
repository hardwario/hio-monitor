#ifndef HISTORYFILE_H
#define HISTORYFILE_H

#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QTextStream>
#include <QReadWriteLock>
#include <QStandardPaths>
#include <QCoreApplication>

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
