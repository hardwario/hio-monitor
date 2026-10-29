#include "pages/placeholder_view_model.h"

#include <QObject>
#include <QString>

#include <utility>

namespace hwio::gui {

PlaceholderViewModel::PlaceholderViewModel(QString pageId, QObject* parent)
    : QObject(parent), m_pageId(std::move(pageId)) {}

} // namespace hwio::gui
