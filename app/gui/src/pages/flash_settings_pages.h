#pragma once

// The pages of plan 0006 (plan/features/0006-m3-flash-settings.md): the per-tab Flash page
// (FlashViewModel, FlashPage.qml), the global Settings page (SettingsViewModel, SettingsPage.qml)
// with its sections General, J-Link, nRF Util and About (other plans add theirs, e.g. Console by
// 0005), the setting keys of 0006 and the Flash page shortcuts (Ctrl+Enter, Ctrl+O; ADR-0051).

namespace hio::gui {

class AppViewModel;

// Registers the pages, sections, keys and shortcuts and creates the FirmwareServices (Qt child of
// `app`). Called from AppOptions::registerExtensions (registerBuiltInPages), before the first tab.
void registerFlashAndSettingsPages(AppViewModel& app);

} // namespace hio::gui
