# ADR-0172 — Settings open as a tab in the tab strip

- **Status:** Accepted (supersedes the Settings bullet of ADR-0043; corrected 2026-10-20: the rail item stays)
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (GUI feedback round 1, plan 0021)

## Context

ADR-0043 opens Settings from the gear at the bottom of the rail as a global page. Users expect it to open
next to the device tabs, like a browser's settings tab, but the tab should not be there all the time.

## Decision

- The **Settings item stays at the bottom of the rail** (gear, above Help). Clicking it, or Ctrl+, opens Settings as a
  **special tab in the top tab strip** (gear icon, title "Settings", at most one) or switches to it when it is open.
- The Settings tab exists **only while it is open**; nothing is shown in the tab strip otherwise. Ctrl+W / × closes it;
  Ctrl+Tab cycles through it like any tab.
- It does not count towards the 8-device limit, is not stored in `ui/tabs` and is not restored at start.
- The Settings tab has no rail and no device bar; it shows the Settings page (sections as in 0006 and the contributed
  sections) over the full width.
- The tab strip gets no extra Settings button. "Open Settings" actions (error panels, wizards)
  open the tab and scroll to the named section.

## Consequences

- `PageDescriptor::global` pages are hosted by the Settings tab instead of the rail (only `settings` is global).
- The M6 rail "MCP" indicator (0014) stays above Settings.
