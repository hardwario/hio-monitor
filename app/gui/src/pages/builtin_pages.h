#pragma once

// The pages that plan 0004 registers itself (plan/features/0004-m3-gui-shell.md, B1): placeholders
// for Console (replaced by 0005); the Flash page and the global Settings page of plan 0006
// (registerFlashAndSettingsPages).

namespace hio::gui {

class AppViewModel;

// Registers the built-in pages. Called from AppOptions::registerExtensions.
void registerBuiltInPages(AppViewModel& app);

} // namespace hio::gui
