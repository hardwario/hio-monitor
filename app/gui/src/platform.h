#pragma once

// Per-OS application identity of the GUI (ADR-0018, plan/features/0015 A3).

namespace hio::gui {

// Windows: SetCurrentProcessExplicitAppUserModelID(L"com.hardwario.monitor") (taskbar grouping,
// the Start-menu shortcut has the same id). Call before the first window. No-op elsewhere.
void applyPlatformIdentity();

} // namespace hio::gui
