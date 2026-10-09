# ADR-0034 — RTT behaviour follows rttt 1.9.2

- **Status:** Accepted (supersedes the "DLL RTT stop not called" rule in `0001-architecture.md`)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M1 planning session)

## Context

The architecture's robustness rules said the library's RTT stop must never be called (it was believed to
prevent re-discovery until a target reset). The rttt 1.9.2 sources (`rttt/connectors/pylink_rtt.py`,
read for M1, open-questions #12) are the newer, field-tested reference used by py-hardwario, and they
**do** call RTT stop: when a session stops, and between control-block search retries to restart a stale
search after a reflash.

## Options considered

1. Follow rttt 1.9.2 (chosen).
2. Keep "never call RTT stop" and restart searches by closing and reopening the J-Link connection.

## Decision

The v2 RTT transport reproduces rttt 1.9.2 `PyLinkRTTConnector` behaviour:

- **Attach:** `RTT start` → poll up/down buffer counts every 100 ms (overall deadline 15 s) → resolve the
  buffers **by name** on every attach (`Terminal`, `Logger`; a reflash may move them) → require a non-zero
  terminal buffer size; wait at most 3 s more for the logger buffer, then continue without logs (warning).
  A zero-sized terminal buffer means a stale control block: `RTT stop`, wait 200 ms, search again.
- **Read:** each cycle read up to the full up-buffer size per channel (so boot bursts are drained);
  a read failure reports Disconnected but reading continues; only received data (or a successful write)
  reports Connected again.
- **Write:** the line + `\n`, in chunks of the down-buffer size; a 0-byte write is retried every 5 ms up
  to a 2 s timeout → Disconnected "target is not reading the shell buffer"; the command is echoed only after
  it was written.
- **Power:** target voltage checked every 1 s while idle; below 1000 mV after a healthy reading has been
  seen → Disconnected "target has no power" (a probe that always reports 0 V never trips this).
- **Reconnect:** stop RTT → reopen the probe and reconnect the target (3 attempts, 500 ms apart) → attach.
  Automatic retries every 3 s when enabled and the target is powered; an explicit reconnect is always tried;
  a session suspended on purpose (flashing, nRF Util) is never re-attached automatically.
- **Reset:** stop RTT without reporting a disconnect → reset → attach again (unless halted).
- Connection state changes are reported only when the state actually changes.

## Consequences

- `0001-architecture.md` and `0002-legacy-inventory.md` are updated accordingly.
- Hardware tests must cover reattach after reset, after reflash and after power loss on J-Link 8.x (ADR-0029).
