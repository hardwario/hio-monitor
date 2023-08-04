#include "searchcomponent.h"

SearchComponent::SearchComponent(QObject* parent)
    : inherited(parent) {}

QQuickTextDocument* SearchComponent::findTextDocument(QObject* parentObject) {
    for (int i = 0; i < parentObject->children().size(); i++) {
        QObject* child = parentObject->children().at(i);
        QVariant property = child->property("textDocument");
        if (property.isValid()) {
            return property.value<QQuickTextDocument*>();
        }
        auto result = findTextDocument(child);
        if(result) return result;
    }
    return nullptr;
}

void SearchComponent::onCompleted() {
    auto textDocument = findTextDocument(parent());
    if (textDocument) {
        _doc = textDocument->textDocument();
        _highlight = new Search(_doc);
        connect(_doc, &QTextDocument::blockCountChanged, 
                this, &SearchComponent::blockCountChanged);
    }
}

void SearchComponent::reset() {
    _highlight->reset();
}

void SearchComponent::searchFor(const QString &pattern) {
    _highlight->setSearchText(pattern);
}

QVector<int> SearchComponent::getMatchedInds() {
    return _highlight->getMatchedInds();
}

void SearchComponent::registerQmlType() {
    qmlRegisterType<SearchComponent>("hiomon", 1,
                                       0, "SearchComponent");
}
