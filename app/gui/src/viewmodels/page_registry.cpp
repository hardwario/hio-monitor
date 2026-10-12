#include "viewmodels/page_registry.h"

#include "gui_log.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QString>
#include <QtAssert>

#include <algorithm>
#include <utility>

namespace hio::gui {

void PageRegistry::add(PageDescriptor page) {
    if (find(page.id) != nullptr) {
        qCCritical(lcGui) << "PageRegistry: the page" << page.id << "is registered twice; ignored";
        Q_ASSERT_X(false, "PageRegistry::add", "duplicate page id");
        return;
    }
    const auto position = std::ranges::upper_bound(m_pages, page.order, {}, &PageDescriptor::order);
    m_pages.insert(position, std::move(page));
}

void PageRegistry::addSettingsSection(SettingsSectionDescriptor section) {
    const bool duplicate =
        std::ranges::any_of(m_sections, [&section](const SettingsSectionDescriptor& known) {
            return known.id == section.id;
        });
    if (duplicate) {
        qCCritical(lcGui) << "PageRegistry: the settings section" << section.id
                          << "is registered twice; ignored";
        Q_ASSERT_X(false, "PageRegistry::addSettingsSection", "duplicate section id");
        return;
    }
    const auto position =
        std::ranges::upper_bound(m_sections, section.order, {}, &SettingsSectionDescriptor::order);
    m_sections.insert(position, std::move(section));
}

const PageDescriptor* PageRegistry::find(const QString& id) const {
    const auto it =
        std::ranges::find_if(m_pages, [&id](const PageDescriptor& page) { return page.id == id; });
    return it == m_pages.end() ? nullptr : &*it;
}

QString PageRegistry::title(const PageDescriptor& page) {
    return QCoreApplication::translate("Pages", page.titleSourceText);
}

QString PageRegistry::title(const SettingsSectionDescriptor& section) {
    return QCoreApplication::translate("Pages", section.titleSourceText);
}

} // namespace hio::gui
