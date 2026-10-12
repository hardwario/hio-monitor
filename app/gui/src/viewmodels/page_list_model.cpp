#include "viewmodels/page_list_model.h"

#include "viewmodels/page_registry.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <cstddef>

namespace hio::gui {

PageListModel::PageListModel(const PageRegistry& registry, QObject* parent)
    : QAbstractListModel(parent), m_registry(registry) {}

int PageListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_registry.pages().size());
}

QVariant PageListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const PageDescriptor& page = m_registry.pages()[static_cast<std::size_t>(index.row())];
    switch (role) {
    case PageIdRole:
        return page.id;
    case TitleRole:
        return PageRegistry::title(page);
    case IconRole:
        return page.icon;
    case QmlRole:
        return page.qml;
    case GlobalRole:
        return page.global;
    default:
        return {};
    }
}

QHash<int, QByteArray> PageListModel::roleNames() const {
    return {{PageIdRole, "pageId"},
            {TitleRole, "title"},
            {IconRole, "iconName"},
            {QmlRole, "qmlUrl"},
            {GlobalRole, "isGlobal"}};
}

QString PageListModel::title(const QString& pageId) const {
    const PageDescriptor* page = m_registry.find(pageId);
    return page != nullptr ? PageRegistry::title(*page) : QString();
}

QUrl PageListModel::qml(const QString& pageId) const {
    const PageDescriptor* page = m_registry.find(pageId);
    return page != nullptr ? page->qml : QUrl();
}

void PageListModel::retranslate() {
    if (rowCount() > 0) {
        dataChanged(index(0), index(rowCount() - 1), {TitleRole}); // signal
    }
    ++m_revision;
    revisionChanged(); // signal
}

} // namespace hio::gui
