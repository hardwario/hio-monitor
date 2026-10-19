#include "pages/builtin_pages.h"

#include "pages/console_page.h"
#include "pages/flash_settings_pages.h"
#include "viewmodels/app_view_model.h"

namespace hio::gui {

void registerBuiltInPages(AppViewModel& app) {
    registerConsolePage(app);           // plan 0005
    registerFlashAndSettingsPages(app); // plan 0006: Flash and the global Settings page
}

} // namespace hio::gui
