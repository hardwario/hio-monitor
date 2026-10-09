# ADR-0050 — Adding a device: probe picker in a new tab with background enumeration

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI planning session)

## Context

Users must choose a probe and a target before connecting. `ProbeEnumerator::listProbes` is synchronous
(M1 plan notes "GUI will use a worker in M3").

## Options considered

1. **'+' opens a new tab showing a live probe list (chosen).**
2. '+' opens a modal dialog; the tab is created afterwards.

## Decision

- A new tab starts in the **"Choose device"** state: live list of probes (product, serial number, firmware,
  "in use in tab N" badge), device profile (CHESTER) and target (app / lte) pickers, **Connect**.
- Enumeration runs on an owned `ProbeScanner` worker (`std::jthread`, one for the application) every **2 s** while
  a picker is visible, and once on demand (Refresh). Results arrive by queued signal; at most one scan runs at a time.
- Exactly one free probe → preselected; first application start opens one such tab automatically.
- Empty list → empty state "No J-Link probe found" with checks (USB cable, J-Link software found?) and a
  disabled "Help me fix this" placeholder for the M4 Connect wizard.
- Choosing a probe already used in another tab switches to that tab instead.

## Consequences

- The same picker is reused by the M4 Connect-device wizard.
