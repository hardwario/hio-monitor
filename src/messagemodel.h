#ifndef MESSAGEMODEL_H
#define MESSAGEMODEL_H

#include <QObject>
#include <QStringListModel>
#include <QVariant>
#include <QRegularExpression>
#include <QQmlEngine>
#include <QTextDocument>

class MessageModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit MessageModel(QObject *parent = nullptr);
    static void registerQmlType();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void addMessage(QString message);
    Q_INVOKABLE void addWithColor(const QString& message, const QString& color);
    Q_INVOKABLE void clear();
    Q_INVOKABLE bool replaceWithColor(const QString& message, const QString& oldColor, const QString& newColor);
    Q_INVOKABLE int indexOf(const QString &term);
    Q_INVOKABLE QStringList getWithFilter(const QString &term);
private:
    QString colorMsg(const QString& message, const QString& color);
    QString getColorByMessageTag(const QString &tag);
    QString stripHTML(QString text);
    QStringList _model;
};

#endif // MESSAGEMODEL_H
