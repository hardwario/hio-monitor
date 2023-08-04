#ifndef MESSAGEMODEL_H
#define MESSAGEMODEL_H

#include <QObject>
#include <QStringListModel>
#include <QVariant>
#include <QRegularExpression>
#include <QQmlEngine>
#include <QTextDocument>

class MessageModel : public QStringListModel
{
    Q_OBJECT
public:
    using QStringListModel::QStringListModel;
    static void registerQmlType();
public slots:
    Q_INVOKABLE void addMessage(QString message);
    Q_INVOKABLE void addWithColor(const QString& message, const QString& color);
    Q_INVOKABLE void clear();
    Q_INVOKABLE int indexOf(const QString &term);
    Q_INVOKABLE QStringList getWithFilter(const QString &term);
private:
    QString getColorByMessageTag(const QString &tag);
    QString stripHTML(QString text);
};

#endif // MESSAGEMODEL_H
