# ADR-0098 — MCP device addressing: stable device ids, multi-device, `connect`/`disconnect` tools

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

rttt serves exactly one session that is already open. CHESTER work often involves two devices (or a J-Link and a BLE
connection to the same device), and the AI should be able to find and open a probe itself.

## Options considered

1. **Multi-device with ids and connect (chosen).**
2. One device per server (`hio mcp -n SN`, or the GUI's active tab).
3. Multi-device in the GUI, but the AI may only use tabs the user opened.

## Decision

- **Device id** = the session key as a string: `jlink:<SN>` or `ble:<BleDeviceId>` (ADR-0083). It is stable
  across reconnects and restarts, so there are no opaque handles.
- `list_devices` lists probes and BLE devices seen recently: id, kind, display name, profile, `state` (`open`,
  `available`, `in_use_by_other_process` + owner), and connection state.
- `connect(device, profile?, target?, speed_khz?, auto_reconnect?)` opens a session. If it is already open it
  returns the existing one (`already_open: true`).
- `disconnect(device)` closes it.
- Every device tool takes `device`. It may be omitted when **exactly one** session is open; otherwise the error is
  `mcp.device_ambiguous`, listing the ids. An unknown or closed id gives `mcp.device_not_open`.
- Targets (CHESTER `app`, `lte`): an optional `target` argument, defaulting to the profile's console target.
- `libs/mcp` reaches devices only through `IDeviceProvider`:
  - **CLI:** owns the sessions it opened, at most 8 (registry limit).
  - **GUI (0014):** devices are the open tabs. `connect` opens a new tab after consent (category `connect`,
    ADR-0101). `disconnect` is allowed only for tabs the AI opened; a user's tab gives `mcp.tool_not_allowed`.

## Consequences

- The AI can work with several devices. Ids appear in console markers and the activity list (ADR-0105).
- BLE devices use the same tools wherever `ConsoleSession` suffices. Tools that need J-Link capabilities return
  `mcp.capability_missing` for BLE.
