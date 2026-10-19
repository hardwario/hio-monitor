#pragma once

// The console page (plan/features/0005-m3-console.md, C5/C5b): registers the "console" page
// (ConsoleViewModel per tab), the console/* setting keys, the Console section of the Settings page
// (ConsoleSection.qml), the page shortcuts (scope "console", ADR-0051) and the ConsoleSettings
// instance (a child of AppViewModel). Called from registerBuiltInPages().

namespace hio::gui {

class AppViewModel;

void registerConsolePage(AppViewModel& app);

} // namespace hio::gui
