# ADR-0051 — GUI keyboard shortcuts

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI planning session)

## Context

The old app had only view-local keys (Up/Down, Ctrl+R, Ctrl+C, n/N, F5). UX principle 6 asks for keyboard
friendliness with shortcuts documented in the UI.

## Options considered

1. **Fixed default map, documented in an F1 overlay (chosen).**
2. Same defaults, user-remappable in Settings.

## Decision

Fixed map (Cmd instead of Ctrl on macOS via `StandardKey` where one exists), shown in the **F1 / Ctrl+/**
overlay and in tooltips:

| Keys | Action |
|---|---|
| Ctrl+T / Ctrl+W | New device tab / close tab |
| Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PgDn/PgUp | Next / previous tab |
| Ctrl+1 / Ctrl+2 | Console / Flash page |
| Ctrl+, | Settings |
| Ctrl+Shift+D / F4 | Connect-disconnect / reconnect |
| Ctrl+F, F3 / Shift+F3, Esc | Find, next / previous match, close find |
| Ctrl+L | Focus log filter |
| Ctrl+K | Clear the focused view |
| Ctrl+P | Pause / resume view |
| End | Resume following |
| Ctrl+R, Up / Down | History search, history (shell input) |
| Ctrl+B | Run batch file |
| Ctrl+S | Save the focused view to a file |
| Ctrl+E | Focus shell input |
| Ctrl+Enter | Flash (Flash page) |
| Ctrl+= / Ctrl+- / Ctrl+0 | Console font size |
| F1, Ctrl+/ | Shortcut overlay |

`n` / `Shift+N` keep working inside a focused log view (legacy).

## Consequences

- Shortcuts are `Shortcut` items in QML bound to ViewModel invokables; a test asserts there are no duplicates.
- Remapping can be added later by a new ADR without changing defaults.
