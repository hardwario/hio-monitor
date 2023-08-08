#include "messagemodel.h"

MessageModel::MessageModel(QObject *parent) : QAbstractListModel(parent) {}

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

bool MessageModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    Q_UNUSED(index)
    Q_UNUSED(value)
    Q_UNUSED(role)
    return false;
}

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
    if(_model.empty())
        return -1;
    auto re = QRegularExpression(term, QRegularExpression::CaseInsensitiveOption);
    for(auto i = 0; i < _model.count(); i++) {
        auto match = re.match(_model.at(i));
        if(match.hasMatch()) {
            return i;
        }
    }
    return -1;
}

QStringList MessageModel::getWithFilter(const QString &term) {
    QStringList result;
    QString plainText;
    auto re = QRegularExpression(term, QRegularExpression::CaseInsensitiveOption);
    for(int i = 0; i < _model.count(); i++) {
        auto cur = _model.at(i);
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
    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    _model.append(finalMessage);
    endInsertRows();
}

QHash<int, QByteArray> MessageModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[Qt::DisplayRole] = "display";
    return roles;
}

void MessageModel::addWithColor(const QString& message, const QString& color) {
    QString finalMessage =  "<font color=" + color +  ">" +
                           message.toHtmlEscaped() +
                           "</font>";
    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    _model.append(finalMessage);
    endInsertRows();
}

void MessageModel::clear() {
    beginResetModel();
    _model.clear();
    endResetModel();
}

void MessageModel::registerQmlType() {
    qmlRegisterType<MessageModel>("hiomon", 1, 0, "MessageModel");
}
