#ifndef MESSAGEMODEL_H
#define MESSAGEMODEL_H

#include <QVariant>
#include <QClipboard>
#include <QQmlEngine>
#include <QTextDocument>
#include <QGuiApplication>
#include <QStringListModel>
#include <QRegularExpression>

class MessageModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit MessageModel(QObject *parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    Q_INVOKABLE void prevMatch();
    Q_INVOKABLE void nextMatch();
    Q_INVOKABLE void searchAndHighlight(const QString& searchTerm);

    Q_INVOKABLE void reset();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void clearCopyBuff();
    Q_INVOKABLE bool copyToClipboard();
    Q_INVOKABLE void deselect(int index);
    Q_INVOKABLE int indexOf(const QString &term);
    Q_INVOKABLE void addMessage(QString message);
    Q_INVOKABLE void filterFor(const QString &term);
    Q_INVOKABLE void addSelectedText(int index, QString message);
    Q_INVOKABLE void addWithColor(const QString& message, const QString& color);
    Q_INVOKABLE bool replaceWithColor(const QString& message, const QString& oldColor, const QString& newColor);

signals:
    void foundMatch(bool found);
    void currentMatchPositionChanged(int row, int index);

private:
    // fields
    QStringList _model;
    QString _searchTerm;
    QString _filterTerm;
    QString _defaultColor;
    QString _highlightColor;
    QStringList _backupModel; // to restore after search or filter

    // filter/search
    int _currentIndex = -1;
    QList<QPair<int, int>> _matchedIndices;

    // selection buffer
    QMap<int, QString> _selectedBuffer;

    // methods
    QString stripHTML(QString text);
    void setModel(const QStringList model);
    QStringList getWithFilter(const QString &term);
    QString highlightOnMatch(const QString& message);
    QString getColorByMessageTag(const QString &tag);
    QString extractOriginalColor(const QString& segment);
    void highlightIfMatch(QString &message, const QString& color);
    QString colorMsg(const QString& message, const QString& color);
    QList<QPair<QString, QString>> extractSegmentsWithColors(const QString& message);
};

#endif // MESSAGEMODEL_H
