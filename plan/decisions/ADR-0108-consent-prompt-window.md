# ADR-0108 — MCP consent prompt is its own always-on-top window (1Password-style); the app does not stay in the tray

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

ADR-0101 put the consent dialog inside the main window, on the device's tab. While the user works in an editor or a
terminal with the AI assistant, the main window is usually behind other windows or minimised. A request there goes
unnoticed and ends in `mcp.consent_timeout`. The user asked for prompts like 1Password's SSH-agent and browser
prompts: the app's own small window that appears over everything.

## Options considered

1. **Its own always-on-top top-level window plus a taskbar/dock alert (chosen).**
2. The same, plus a plain system notification (tray / notification API).
3. Native OS notifications with Allow/Deny buttons (D-Bus, Windows toast, `UNUserNotificationCenter`). This needs an
   installed app identity and signing (M7) and native code per OS.
4. Keep the in-window modal dialog.

Separately: whether the app keeps running in the tray after its window is closed (as 1Password does), so that MCP
stays available. Options: no; optional tray mode; always when MCP is on.

## Decision

- **Consent prompt window** (`ConsentWindow.qml`, a QML `Window`, not a `Dialog`):
  - frameless with its own title row; flags `Qt::Window | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint`;
  - no transient parent, so it shows while the main window is minimised or on another virtual desktop. On X11 and
    macOS it is visible on all workspaces (`_NET_WM_STATE_STICKY` via Qt where available; macOS collection
    behaviour `canJoinAllSpaces` through the Qt window property; best effort, no native code);
  - centred on the screen under the mouse cursor (`QGuiApplication::screenAt(QCursor::pos())`), falling back to
    the main window's screen;
  - raised with `requestActivate()` plus `QGuiApplication::alert(mainWindow)`, which flashes the taskbar entry
    (Windows/Linux) or bounces the dock icon (macOS);
  - contents, buttons, queue (one prompt at a time, ≤ 8 queued), 120 s countdown and withdrawal are as in
    ADR-0101. Esc = Deny, and there is no default button;
  - the main window does **not** switch tabs. The prompt names the device and tab, and has a link "Show device"
    that raises the main window on that tab.
- **Platform limits accepted:** Windows foreground-lock rules and Wayland compositors may show the window without
  keyboard focus (1Password has the same limit). The alert still marks it. Nothing tries to bypass these rules (no
  `AllowSetForegroundWindow` tricks, no `AttachThreadInput`).
- **No tray mode:** closing the main window quits the app as today (0004). The MCP server stops, its calls are
  cancelled, and the probes are released. Background use is `hio mcp` (ADR-0106). A later ADR may add an optional
  tray mode.
- Native notifications with buttons (option 3) are reconsidered in M7, once installers give the app an identity.

## Consequences

- Prompts are noticed while the user is in another app, with no new dependency and no native code.
- 0014 replaces the in-window `ConsentDialog` with `ConsentWindow`. Qt Quick tests check the window flags, the
  screen choice and the queue. The manual checks cover focus and stacking on Windows 10/11, macOS, GNOME X11 and
  GNOME/KDE Wayland.
- ADR-0101 stays valid apart from where the prompt appears.
