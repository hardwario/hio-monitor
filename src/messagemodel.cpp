#include "messagemodel.h"

MessageModel::MessageModel(QObject *parent) : QAbstractListModel(parent)
{
    _defaultColor = "#ebf2fc";
    _highlightColor = "#ffff00";
}

int MessageModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return _model.size();
}

QVariant MessageModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid())
        return QVariant();

    if (index.row() >= _model.size())
        return QVariant();

    auto messsage = _model.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return QVariant::fromValue(messsage);
    default:
        return QVariant();
    }
}

// Helper function to extract text segments and their colors
QList<QPair<QString, QString>> MessageModel::extractSegmentsWithColors(const QString& message) {
    QList<QPair<QString, QString>> segments;
    static QRegularExpression re("<font color='(#[0-9A-Fa-f]+)'>(.*?)</font>");

    auto matchIterator = re.globalMatch(message);

    while (matchIterator.hasNext()) {
        auto match = matchIterator.next();
        auto first = match.captured(1);
        auto second = match.captured(2);

        second = second.replace("&lt;", "<");
        second = second.replace("&gt;", ">");

        segments.append(qMakePair(second, first));
    }

    return segments;
}

// Modified search and highlight function
void MessageModel::searchAndHighlight(const QString& searchTerm) {
    _searchTerm = searchTerm;

    QRegularExpression searchRe(_searchTerm, QRegularExpression::CaseInsensitiveOption);

    _matchedIndices.clear();

    beginResetModel();
    _backupModel = _model;

    bool firstMatch = true;

    for (int i = 0; i < _model.size(); ++i) {
        QString message = _model.at(i);

        QString highlighted = highlightOnMatch(message);
        _model[i] = highlighted;

        auto matchIterator = searchRe.globalMatch(stripHTML(message));
        if (!matchIterator.hasNext()) continue;

        if (firstMatch) {
            firstMatch = false;
            emit foundMatch(true);
        }

        while (matchIterator.hasNext()) {
            auto match = matchIterator.next();
            _matchedIndices.append(qMakePair(i, match.capturedStart()));
        }
    }

    // if no matches found, emit signal
    if (firstMatch) {
        emit foundMatch(false);
    }

    endResetModel();
}

QString MessageModel::highlightOnMatch(const QString &message) {
    QRegularExpression searchRe(_searchTerm, QRegularExpression::CaseInsensitiveOption);

    // Extract text segments and their original colors
    auto segments = extractSegmentsWithColors(message);

    QString newMessage;
    // highlight matched text keeping original colors
    for (const auto &segment : segments) {
        QString text = segment.first;
        QString color = segment.second;

        auto matchIterator = searchRe.globalMatch(text);
        int lastPosition = 0;
        QString highlightedText;

        while (matchIterator.hasNext()) {
            auto match = matchIterator.next();
            int matchStart = match.capturedStart();
            int matchEnd = match.capturedEnd();

            // Non-matched part
            highlightedText += colorMsg(text.mid(lastPosition, matchStart - lastPosition), color);
            // Matched part, highlighted
            highlightedText += colorMsg(match.captured(0), _highlightColor);

            lastPosition = matchEnd;
        }

        // Append any remaining part of the segment after the last match
        if (lastPosition < text.length()) {
            highlightedText += colorMsg(text.mid(lastPosition), color);
        }

        newMessage += highlightedText;
    }

    return newMessage;
}

bool MessageModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    Q_UNUSED(index)
    Q_UNUSED(value)
    Q_UNUSED(role)
    return false;
}

QString MessageModel::getColorByMessageTag(const QString& tag) {
    switch (tag.at(0).toLatin1()) {
        case 'd': // dbg
            return "<font color='#B392F0'>";
        case 'i': // inf
            return "<font color='#85E89D'>";
        case 'w': // wrn
            return "<font color='#FFAB70'>";
        case 'e': // err
            return "<font color='#F97583'>";
        default:
            return "<font color='#D1D5DA'>";
    }
}

QString MessageModel::colorMsg(const QString& message, const QString& color) {
    return "<font color='" + color +  "'>" +
           message.toHtmlEscaped() +
           "</font>";
}

bool MessageModel::replaceWithColor(const QString& message, const QString& oldColor, const QString& newColor) {
    auto searchMsg = colorMsg(message, oldColor);
    auto index = indexOf(searchMsg);
    auto matched = index != -1;
    if (matched) {
        auto newMessage = colorMsg(message, newColor);

        beginRemoveRows(QModelIndex(), index, index);
        _model.removeAt(index);
        endRemoveRows();

        beginInsertRows(QModelIndex(), index, index);
        _model.insert(index, newMessage);
        endInsertRows();
    }
    return matched;
}

int MessageModel::indexOf(const QString &term) {
    if(_model.empty())
        return -1;

    auto re = QRegularExpression(term, QRegularExpression::CaseInsensitiveOption);
    for(auto i = 0; i < _model.count(); i++) {
        auto item = re.match(_model.at(i));
        if(item.hasMatch()) {
            return i;
        }
    }

    return -1;
}

QStringList MessageModel::getWithFilter(const QString &term) {
    QStringList result;

    auto re = QRegularExpression(term, QRegularExpression::CaseInsensitiveOption);
    for(int i = 0; i < _model.count(); i++) {
        auto cur = _model.at(i);
        auto item = re.match(stripHTML(cur));
        if(item.hasMatch()) {
            result.append(cur);
        }
    }

    return result;
}

QString MessageModel::stripHTML(QString text) {
    QTextDocument doc;
    doc.setHtml(text);
    return doc.toPlainText();
}

void MessageModel::addMessage(QString message) {
    static QRegularExpression regex("^\\[\\d+:\\d+:\\d+\\.\\d+,\\d+\\] <(dbg|inf|wrn|err)>");
    auto item = regex.match(message);
    QString finalMessage;

    if (item.hasMatch()) {
        QString tag = item.captured(1);
        QString msgBody = colorMsg(message.mid(item.capturedEnd()), _defaultColor);
        QString prefix = getColorByMessageTag(tag) + item.captured(0).toHtmlEscaped() + "</font>";

        if (!_searchTerm.isEmpty()) {
            QRegularExpression searchRe(_searchTerm, QRegularExpression::CaseInsensitiveOption);
            QString msg = prefix + msgBody;

            _backupModel.append(msg);

            prefix = highlightOnMatch(prefix);
            msgBody = highlightOnMatch(msgBody);

            auto matchIterator = searchRe.globalMatch(stripHTML(msg));
            while (matchIterator.hasNext()) {
                auto match = matchIterator.next();
                _matchedIndices.append(qMakePair(_backupModel.size() - 1, match.capturedStart()));
            }
        }

        finalMessage = prefix + msgBody;
    } else {
        highlightIfMatch(message, _defaultColor);
        finalMessage = message;
    }

    // filter/search term check
    bool append = true;
    if (!_filterTerm.isEmpty()) {
        append = stripHTML(finalMessage).contains(_filterTerm, Qt::CaseInsensitive);
    }

    if (append) {
        beginInsertRows(QModelIndex(), rowCount(), rowCount());
        _model.append(finalMessage);
        endInsertRows();
    } else {
        _backupModel.append(finalMessage);
    }
}

void MessageModel::addWithColor(const QString& message, const QString& color) {
    // split final message by newline char and add each line separately
    QStringList lines = message.split("\n", Qt::SkipEmptyParts);

    for (auto& line : lines) {
        highlightIfMatch(line, color);

        if (!_searchTerm.isEmpty()) {
            _backupModel.append(colorMsg(line, _defaultColor));
        }

        beginInsertRows(QModelIndex(), rowCount(), rowCount());
        _model.append(line);
        endInsertRows();
    }
}

void MessageModel::highlightIfMatch(QString &message, const QString& originalColor) {
    if (_searchTerm.isEmpty()) {
        message = colorMsg(message, originalColor);
        return;
    }

    QRegularExpression re(_searchTerm, QRegularExpression::CaseInsensitiveOption);
    int lastPosition = 0;
    QString newMessage;

    auto matchIterator = re.globalMatch(message);

    while (matchIterator.hasNext()) {
        auto match = matchIterator.next();
        int matchStart = match.capturedStart();
        int matchEnd = match.capturedEnd();

        // Add the part of the message before the match, using the original color
        QString beforeMatch = message.mid(lastPosition, matchStart - lastPosition);
        if (!beforeMatch.isEmpty()) {
            newMessage += colorMsg(beforeMatch, originalColor);
        }

        // Add the matched text, highlighted
        QString matchedText = match.captured(0);
        newMessage += colorMsg(matchedText, _highlightColor); // Highlight color for matched part

        lastPosition = matchEnd;
    }

    // Append any remaining part of the message after the last match
    if (lastPosition < message.length()) {
        newMessage += colorMsg(message.mid(lastPosition), originalColor);
    }

    message = newMessage.isEmpty() ? colorMsg(message, originalColor) : newMessage;
}

void MessageModel::setModel(const QStringList model) {
    beginResetModel();
    _model = model;
    endResetModel();
}

void MessageModel::filterFor(const QString &term) {
    if (term.isEmpty()) {
        return;
    }

    _filterTerm = term;
    _backupModel = _model;
    auto filtered = getWithFilter(term);

    bool isFiltered = !filtered.isEmpty();
    if (isFiltered) {
        setModel(filtered);
    }

    emit foundMatch(isFiltered);
}

void MessageModel::addSelectedText(int index, QString message) {
    _selectedBuffer[index] = message;
}

QHash<int, QByteArray> MessageModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[Qt::DisplayRole] = "display";
    return roles;
}

void MessageModel::reset() {
    if (!_backupModel.isEmpty()) {
        setModel(_backupModel);
    }

    _searchTerm.clear();
    _filterTerm.clear();
    _matchedIndices.clear();
    _currentIndex = -1;
}

void MessageModel::nextMatch() {
    if (_matchedIndices.isEmpty()) {
        return;
    }

    if (_currentIndex < _matchedIndices.size() - 1) {
        _currentIndex++;
    }

    emit currentMatchPositionChanged(_matchedIndices[_currentIndex].first, _matchedIndices[_currentIndex].second);
}

void MessageModel::prevMatch() {
    if (_matchedIndices.isEmpty()) {
        return;
    }

    if (_currentIndex > 0) {
        _currentIndex--;
    }

    emit currentMatchPositionChanged(_matchedIndices[_currentIndex].first, _matchedIndices[_currentIndex].second);
}

void MessageModel::clear() {
    beginResetModel();
    reset();
    _model.clear();
    endResetModel();
    clearCopyBuff();
}

void MessageModel::clearCopyBuff() {
    _selectedBuffer.clear();
}

bool MessageModel::copyToClipboard() {
    static QRegularExpression re(".+"); // remove empty lines
    QClipboard *clipboard = QGuiApplication::clipboard();

    auto buff = _selectedBuffer.values();
    buff = buff.filter(re);

    bool result = !buff.isEmpty();
    if (result) {
        clipboard->setText(buff.join("\n"));
    }

    return result;
}

void MessageModel::deselect(int index) {
    _selectedBuffer.remove(index);
}
