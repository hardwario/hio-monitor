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
    QVector<QString> readAll();
    void write(QString message);
    void writeMoveOnMatch(QString message);
    QString getFilePath() const { return _file.fileName(); }

signals:
    void historyChanged();

private:
    QFile _file;
    QString _fileName;
    QReadWriteLock _lock;
    QString createFile();
};
#endif // HISTORYFILE_H
