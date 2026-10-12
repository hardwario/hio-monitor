# ADR-0052 — J-Link connection options in an Advanced popover

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI planning session)

## Context

`SessionOptions` contains speed, RTT latency, auto-reconnect and reset-on-open. Most users never change them.

## Options considered

1. **Device bar "Advanced" popover per tab, defaults from Settings, remembered per probe SN (chosen).**
2. Only in global Settings.

## Decision

- Device bar: probe, target, Connect. An **Advanced** (⋯) popover holds speed (kHz), RTT latency (ms),
  auto-reconnect, reset on open, console log file on/off. Values apply on the next connect (the popover says so).
- Defaults come from Settings (`jlink/speedKhz`, `rtt/latencyMs`, `rtt/autoReconnect`, `rtt/resetOnOpen`; the
  profile's values when unset). Per-probe overrides in `probes/<sn>/…`, bounded to **32** remembered probes (LRU).

## Consequences

- Power users get per-probe tuning without cluttering the default UI.
