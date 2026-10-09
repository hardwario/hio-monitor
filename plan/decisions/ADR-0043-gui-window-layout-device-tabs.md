# ADR-0043 — GUI window layout: device tabs, per-tab rail and device bar

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI planning session)

## Context

The old app had a left page column and a right-hand tool panel whose buttons changed per page; users found the
right panel hard to follow, and it supported only one device. The backend supports up to 8 sessions
(`SessionRegistry`, ADR-0031).

## Options considered

1. Single device per window: left rail + top device bar + per-page toolbar.
2. **Device tabs on top, each tab with its own rail (Console / Flash), device bar and state; Settings global (chosen).**
3. Rail pages on top, device tabs inside each page.
4. Top page tabs without a rail; console-centric layout with flash as a drawer.

## Decision

- Window = **device tab strip** (top) → per tab: **left rail** (Console, Flash; BLE added in M5) + **device bar**
  (probe, target, Connect/Disconnect, connection state, Advanced popover) + the page with its **own toolbar**
  directly above the content it acts on.
- Each tab owns one `DeviceSession` (or none while choosing a probe). At most **8 tabs** (= registry limit);
  the `+` button is disabled with a tooltip at the limit.
- **Settings** is global: gear at the bottom of the rail opens the Settings page as a full-window overlay
  page (not per tab). Help (shortcut overlay, docs link, About) sits below it.
- Tab title: `<profile> <target> · <probe SN>` + state dot; closing a connected tab disconnects it (no prompt
  unless an operation is running — then confirm "Cancel flashing and close?").
- The old right-hand ToolPanel is removed.

## Consequences

- Every ViewModel except App/Settings is per tab (`DeviceTabViewModel` owns them).
- Several probes can be watched side by side by switching tabs; detaching a tab into its own window is not in M3.
- Layout must stay usable at the minimum window size of **960 × 600** (design system).
