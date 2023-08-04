#ifndef SEARCHHIGHLIGHTERCOMPONENT_H
#define SEARCHHIGHLIGHTERCOMPONENT_H

#include <QObject>
#include <QQuickTextDocument>
#include "searchhighlighter.h"

class SearchComponent : public QObject
{
    Q_OBJECT
    using inherited = QObject;
public:
    explicit SearchComponent(QObject* parent = nullptr);
    static void registerQmlType();

    Q_INVOKABLE void reset();
    Q_INVOKABLE void searchFor(const QString &pattern);
    Q_INVOKABLE QVector<int> getMatchedInds();
    Q_INVOKABLE void onCompleted();

signals:
    void blockCountChanged();

private:
    QString _text = "";
    Search* _highlight;
    QTextDocument* _doc;
    QQuickTextDocument* findTextDocument(QObject* parentObject);
};

#endif // SEARCHHIGHLIGHTERCOMPONENT_H
