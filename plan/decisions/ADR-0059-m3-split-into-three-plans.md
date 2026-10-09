# ADR-0059 — M3 is split into three feature plans

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 planning review)

## Context

The first M3 draft (feature 0004 "GUI core", 584 lines, phases A–E) is too large for one implementation agent.
Several agents work on the code base in parallel.

## Options considered

1. **Three plans: shell, console, flash + settings (chosen).**
2. Two plans: shell + console, flash + settings.
3. Keep one plan.

## Decision

| Plan | Content | Depends on |
|---|---|---|
| **0004 — M3 GUI shell** | style, Theme, fonts, icons, base components, `ErrorInfo`, single instance, window, device tabs and restore, probe picker, device bar and J-Link options, shortcuts framework and F1 overlay, shell responsive layout, page-extension contract | M0; M1 for real sessions |
| **0005 — M3 console page** | `ConsoleLineModel`, `FindFilterEngine`, shell/log views, search bar, history, batch, banners | 0004, M1 |
| **0006 — M3 flash and settings pages** | `FirmwareLoader`, `FlashViewModel` and page, maintenance actions, `SettingsViewModel` and page | 0004, M2 |

- 0004 defines the **page-extension contract** that 0005 and 0006 implement:
  - child ViewModel slots on `DeviceTabViewModel`;
  - rail entries;
  - page `Loader`s;
  - the busy-badge / operation-state API;
  - the shortcut registration API;
  - the Settings page section API.
- After 0004 is done, 0005 and 0006 can be implemented in parallel by different agents. Shared QML components and
  `Theme` change only through 0004 (or an amendment to it).
- Czech translation and screenshots are part of "done" in each plan, for its own screens.

## Consequences

- Smaller plans that can be reviewed and implemented in parallel.
- The contract in 0004 must be stable before 0005/0006 start. Changing it needs an amendment to 0004 and the
  user's approval.
- The design system and the mockups stay shared (`plan/gui/`).
