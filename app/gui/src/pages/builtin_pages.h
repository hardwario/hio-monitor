#pragma once

// The built-in pages (plan/features/0004-m3-gui-shell.md, B1): the console page of plan 0005
// (registerConsolePage) and placeholders for Flash and the global Settings page (replaced by 0006).

namespace hio::gui {

class AppViewModel;

// Registers the pages. Called from AppOptions::registerExtensions.
void registerBuiltInPages(AppViewModel& app);

} // namespace hio::gui
