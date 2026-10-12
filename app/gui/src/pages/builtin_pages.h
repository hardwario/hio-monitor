#pragma once

// The pages that plan 0004 registers itself (plan/features/0004-m3-gui-shell.md, B1): placeholders
// for Console (replaced by 0005), Flash and the global Settings page (replaced by 0006).

namespace hio::gui {

class AppViewModel;

// Registers the placeholder pages. Called from AppOptions::registerExtensions.
void registerBuiltInPages(AppViewModel& app);

} // namespace hio::gui
