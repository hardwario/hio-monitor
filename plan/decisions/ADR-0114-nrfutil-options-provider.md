# ADR-0114 — nRF Util locator options resolved per operation

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad

Amends plan 0003, review R9 ("`SessionOptions::nrfUtil` ... resolved in `open()`").

## Context

`DeviceSession::open()` turned `SessionOptions::nrfUtil` into locator options once: either the caller's options or
`nrfutil::defaultLocatorOptions()` with the user's settings. Every later modem step (`updateModem`, step 1 of
`flashLteBundle`) used that snapshot. In the GUI a session lives as long as the tab is connected. A new nRF Util path
set in Settings or by the setup wizard (0008) did not reach an open session. The Flash page then enabled Flash (its
own detection found the new path), but the modem update failed with `dependency.nrfutil_not_found`. This was review
finding F5 of plan 0006.

## Options considered

- **(a) A provider in `SessionOptions`.** `std::function<nrfutil::LocatorOptions()> nrfUtilProvider` is called on the
  owner thread by `updateModem()` / `flashLteBundle()` when the operation is requested. The options travel to the
  worker with the operation command.
- **(b) Options in the operation request.** `updateModem(target, zip, std::optional<LocatorOptions>)` and the same
  for `flashLteBundle`. Every caller (Flash page, flash wizard) must pass them.
- **(c) No API change.** The default is resolved lazily on the worker with `Settings::openDefault()`. The GUI's
  `SettingsStore` and the backend would then read the settings file independently, from two threads.

## Decision

Option (a).

- `SessionOptions::nrfUtilProvider` is optional.
- When it is set, `updateModem()` and `flashLteBundle()` call it on the owner thread each time such an operation is
  requested. The result is stored in the operation command, and the worker's modem step locates nRF Util with it.
- When it is empty, the snapshot taken in `open()` is used, as before: `SessionOptions::nrfUtil`, or the defaults
  with the user's settings.
- The provider is never called on the worker, never at `open()`, and never for other operations.
- The GUI tab passes a provider that builds `nrfutil::defaultLocatorOptions()` from its `SettingsStore`.
- The CLI keeps its fixed options (`--nrfutil`), with no provider.
- The internal `ModemUpdaterFactory`, which is also a test hook, receives the locator options as a parameter.

## Consequences

- A changed nRF Util path is used by the next modem update of an open session; no reconnect is needed.
- The provider runs on the owner thread, so it may read GUI-thread state (`SettingsStore`). It must be cheap and must
  not throw (ADR-0007).
- The snapshot path stays, for the CLI and for callers without a provider.
- Tests:
  - `Operations.NrfUtilProviderIsCalledPerOperationOnTheOwnerThread`
  - `Operations.WithoutAProviderTheSnapshotOfOpenIsUsed`
  - `FlashTest.AChangedNrfUtilPathReachesAnOpenSession`
