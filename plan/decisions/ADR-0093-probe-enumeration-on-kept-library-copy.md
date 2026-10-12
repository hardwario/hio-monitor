# ADR-0093 — Probe enumeration on a kept library copy

- **Status:** Accepted (amends ADR-0050 and feature 0002; added on `v2/m2-flashing` for feature 0004)
- **Date:** 2026-10-12
- **Decided by:** Matej Strnad

## Context

ADR-0050 has the GUI probe scanner enumerate probes every 2 s while a picker is visible. The M1 call
`jlink::enumerateProbes` copies (about 29 MB) and loads the JLinkARM library for every call
(`JLinkLibrary::loadCopy`, ADR-0030), i.e. a copy and a load every 2 s (M3 shell review, finding S4).

## Options considered

1. **The scanner keeps one library copy loaded while a picker is visible (chosen).**
2. Enumerate less often (slower reaction to plugged probes; still copies).
3. Share a library copy with sessions (breaks ADR-0030: one copy per user, vendor calls serialized per copy).

## Decision

- `libs/hw/jlink` offers `ProbeEnumerator`: an owned, movable object that loads its own library copy once
  (`JLinkLibrary::loadCopy`, ADR-0028/0030 rules unchanged; never shared with a session) and provides
  `[[nodiscard]] Result<std::vector<ProbeInfo>> enumerate()` with the existing enumeration logic (USB and network
  probes, bounded as before). It is not thread-safe: one thread uses it. The destructor unloads the copy and
  removes its directory.
- `enumerateProbes()` stays as the one-shot convenience and is implemented with `ProbeEnumerator`, so its
  behaviour is unchanged.
- The GUI scanner (ADR-0050) keeps one `ProbeEnumerator` while a picker is visible and drops it when no picker is
  visible or when the library location changes (the caller's job: `location()` tells which library it holds).

## Consequences

- No copy and load every 2 s; one extra library copy exists while a picker is visible.
- A probe opened by a session is still listed through the scanner's own copy (unchanged).

## Amendment (2026-10-12, M3 review fix S4, accepted by Matej Strnad)

Loading and unloading of library copies (`QLibrary::load()` / `unload()` in `jlink_library.cpp`) is serialized by
one process-wide mutex. Copies are loaded and unloaded by different threads (session workers, the GUI probe
scanner), and a new copy may be mapped where an unloaded one was; the dynamic loader's own locking is invisible to
ThreadSanitizer. Cost: concurrent loads wait for each other (one library load).
