# ADR-0020 — Logging with QLoggingCategory and an own bounded file sink

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M0 planning session)

## Context

The app needs an application diagnostic log (for support), separate from device logs, which is bounded and
rotated (the old app had unrotated logs). Qt itself emits warnings through its message handler.

## Options considered

1. `QLoggingCategory` + our own message handler and sinks (no new dependency, captures Qt messages).
2. spdlog — mature rotating sinks, but a new dependency, and Qt messages still need a bridge.
3. Own pure-C++ logger — more code, and Qt still needs a bridge.

## Decision

Use **Qt logging categories** (`hio.core`, `hio.cli`, `hio.gui`, later `hio.jlink`, `hio.ble`, …) and an
own message handler in `libs/core` that writes to stderr (configurable threshold) and to a **bounded
file sink** (per-process-run files, a per-file size cap, retention by count and total size; limits are documented in
the M0 feature plan). The handler is thread-safe and flushes on warning and above, and on terminate.

## Consequences

- Category filtering via `QT_LOGGING_RULES` works out of the box, which is useful for support.
- All modules log through `qCDebug(lcX)` etc. No `printf`-family output.
- The sink is our code and must be covered by tests (rotation, retention, concurrency).
