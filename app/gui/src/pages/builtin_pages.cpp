#include "pages/builtin_pages.h"

#include "pages/console_page.h"
#include "pages/placeholder_view_model.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/page_registry.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtCore/qttranslation.h>

namespace hio::gui {

namespace {

const QUrl
    placeholderUrl(QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/pages/PlaceholderPage.qml"));

PageDescriptor placeholder(const QString& id, const char* title, const QString& icon, int order,
                           bool global) {
    return PageDescriptor{.id = id,
                          .titleSourceText = title,
                          .icon = icon,
                          .qml = placeholderUrl,
                          .order = order,
                          .global = global,
                          .createViewModel = [id](DeviceTabViewModel* tab) -> QObject* {
                              // Qt parent ownership: the tab, or AppViewModel for the global page
                              // (set by the shell).
                              // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
                              return new PlaceholderViewModel(id, tab);
                          }};
}

} // namespace

void registerBuiltInPages(AppViewModel& app) {
    registerConsolePage(app); // plan 0005 (replaces the console placeholder)
    PageRegistry& pages = app.pageRegistry();
    pages.add(placeholder(QStringLiteral("flash"), QT_TRANSLATE_NOOP("Pages", "Flash"),
                          QStringLiteral("flash"), 20, false));
    pages.add(placeholder(QStringLiteral("settings"), QT_TRANSLATE_NOOP("Pages", "Settings"),
                          QStringLiteral("settings"), 100, true));
}

} // namespace hio::gui
