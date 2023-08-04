#ifndef SEARCH_H
#define SEARCH_H

#include <QSyntaxHighlighter>
#include <QRegularExpression>

class Search : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit Search(QTextDocument *parent = nullptr);

    void setSearchText(const QString &searchText);
    void reset();
    QVector<int> getMatchedInds() {
        return _matchedInds;
    }
protected:
    void highlightBlock(const QString &text) override;

private:
    QString searchText = "";
    QTextCharFormat format;
    QVector<int> _matchedInds;
};

#endif // SEARCH_H
