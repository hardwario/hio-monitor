# ADR-0158 — Low-level target operations live in `IProgrammer`

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6 implementation, question from the 0013 implementation agent)

## Context

Plan 0013 named only the width parameter of `IProgrammer::readMemory`. The implementation needs a hardware layer
for halt, go, status, memory reads/writes with width, registers and memory zones that is unit-testable with
`MockJLinkApi`.

## Options considered

1. **Extend `IProgrammer` / `JLinkProgrammer`** (same layer as program, erase, readMemory, reset).
2. A separate `ITargetDebug` interface next to it.

## Decision

Option 1. `IProgrammer` gains the target operations; the session worker calls them from free functions in
`libs/device/src/session_worker_target.cpp` (the `flash_workflows` pattern over `WorkflowContext`), dispatched from
the existing switch, instead of new `SessionWorker` members (that class is file-local in `session_worker.cpp`, which
the parallel M5 work also edits).

## Consequences

- One hardware interface per probe; every `IProgrammer` fake/mock must implement the new methods.
