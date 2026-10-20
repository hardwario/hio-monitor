# ADR-0031 — DeviceSession threading: owned jthread, bounded queues, QObject façade

- **Status:** Accepted (BLE sessions use the shared BLE thread of ADR-0080 instead)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M1 planning session)

## Context

Every hardware session must be owned by exactly one worker thread, with all vendor-library calls
serialized on it (CLAUDE.md). The old app used `QThread::create` without ownership and `QTimer`s in
threads without an event loop. The GUI, the CLI and later MCP all consume the same session events.

## Options considered

1. Owned `std::jthread` running an explicit loop, with bounded command and event queues and a thin
   `QObject` façade on the owner thread (chosen).
2. Worker `QObject` in an owned `QThread` with an event loop, `QTimer` polling and queued slot calls.

## Decision

- Each `DeviceSession` owns one **`std::jthread`**. Its loop: drain the command queue → poll RTT every
  `latency` ms (default 50) → check target voltage every 1 s → wait on the command queue with a
  timeout of the remaining poll interval. No `QTimer` and no event loop in the worker.
- **Commands** (send line, reset, reconnect, suspend/resume, close) go through
  `BoundedQueue<Command>` (capacity 256, `Reject` → `internal.queue_full`). Commands that need a
  result carry a one-shot completion delivered back as an event.
- **Events** go through `BoundedQueue<SessionEvent>` (capacity 10 000, `DropOldest`); dropped events are
  reported as one `LinesDropped{n}` event so loss is never silent.
- The façade `hio::device::DeviceSession : QObject` lives on the owner thread (GUI main thread or the CLI's
  `QCoreApplication` thread). The worker wakes it with one coalesced queued `QMetaObject::invokeMethod`
  (atomic "pending" flag); the façade drains the queue and emits batched Qt signals.
- Shutdown: `request_stop()` + join in the destructor / `close()`; every blocking wait takes the
  `std::stop_token`; every hardware step has a timeout.

## Consequences

- Deterministic, testable without an event loop (tests drive the worker through the queues).
- Signal floods are avoided by batching; memory is bounded by queue capacities.
- The façade API is the only public surface used by the CLI, GUI ViewModels and MCP.
