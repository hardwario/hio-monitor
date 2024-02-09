#include "searchhighlighter.h"

// SearchHighlighter implementation of QSyntaxHighlighter class
Search::Search(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // #F9826C
    format.setForeground(QColor(249, 130, 108));
}

void Search::setSearchText(const QString &text) {
    searchText = text;
    rehighlight();
}

void Search::highlightBlock(const QString &text) {
    if (searchText.isEmpty()) return;

    _matchedInds.clear();

    QRegularExpression re(QRegularExpression::escape(searchText),
                          QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator i = re.globalMatch(text);
    int blockPosition = currentBlock().position();

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        int start = static_cast<int>(match.capturedStart());
        int newBlockPosition = blockPosition + start;

        if (!_matchedInds.contains(newBlockPosition)) {
            _matchedInds.append(newBlockPosition);
        }

        int length = static_cast<int>(match.capturedLength());
        setFormat(start, length, format);
    }
}

void Search::reset() {
    searchText.clear();
    _matchedInds.clear();
    rehighlight();
}
