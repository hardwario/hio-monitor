#include "messagemodel.h"

QString MessageModel::getColorByMessageTag(const QString& tag) {
    if (tag == "dbg")
        return "<font color='#B392F0'>";
    if (tag == "inf")
        return "<font color='#85E89D'>";
    if (tag == "wrn")
        return "<font color='#FFAB70'>";
    if (tag == "err")
        return "<font color='#F97583'>";
    return "<font color='#D1D5DA'>";
}

int MessageModel::indexOf(const QString &term) {
    auto messages = stringList();
    if(messages.empty())
        return -1;
    auto re = QRegularExpression(term, QRegularExpression::CaseInsensitiveOption);
    for(auto i = 0; i < messages.count(); i++) {
        auto match = re.match(messages.at(i));
        if(match.hasMatch()) {
            return i;
        }
    }
    return -1;
}

QStringList MessageModel::getWithFilter(const QString &term) {
    QStringList result;
    QString plainText;
    auto messages = stringList();
    auto re = QRegularExpression(term, QRegularExpression::CaseInsensitiveOption);
    for(int i = 0; i < messages.count(); i++) {
        auto cur = messages.at(i);
        plainText = stripHTML(cur);
        auto match = re.match(plainText);
        if(match.hasMatch()) {
            qDebug() << "Filtered msg append: " << cur;
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
    auto match = regex.match(message);
    QString finalMessage;
    if (match.hasMatch()) {
        QString tag = match.captured(1);
        finalMessage = getColorByMessageTag(tag) +
                       match.captured(0).toHtmlEscaped() +
                       "</font>" +
                       "<font color='#D1D5DA'>" +
                       message.mid(match.capturedEnd()).toHtmlEscaped() +
                       "</font>";
    } else {
        finalMessage = "<font color='#D1D5DA'>" +
                       message +
                       "</font>";
    }
    insertRow(rowCount());
    setData(index(rowCount()-1), finalMessage);
}

void MessageModel::addWithColor(const QString& message, const QString& color) {
    QString finalMessage =  "<font color=" + color +  ">" +
                           message.toHtmlEscaped() +
                           "</font>";
    insertRow(rowCount());
    setData(index(rowCount()-1), finalMessage);
}

void MessageModel::clear() {
    removeRows(0, rowCount());
}

void MessageModel::registerQmlType() {
    qmlRegisterType<MessageModel>("hiomon", 1, 0, "MessageModel");
}
