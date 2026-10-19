#pragma once

// The built-in pages (plan/features/0004-m3-gui-shell.md, B1): the console page of plan 0005
// (registerConsolePage) and the Flash page and the global Settings page of plan 0006
// (registerFlashAndSettingsPages).

namespace hio::gui {

class AppViewModel;

// Registers the built-in pages. Called from AppOptions::registerExtensions.
void registerBuiltInPages(AppViewModel& app);

} // namespace hio::gui
