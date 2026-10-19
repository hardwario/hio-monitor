# ADR-0127 — The fake BLE backend is compiled under `HIO_TEST_OVERRIDES`

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad

Supersedes the "`HIO_TEST_FAKES`" part of ADR-0087. The rest of ADR-0087 (`IBleApi`, JSON scenarios,
`HIO_FAKE_BLE_SCENARIO`, `MockBleApi`, HIL opt-in) stays.

## Context

ADR-0087 (written 2026-10-10) introduced a new CMake option `HIO_TEST_FAKES` with its own release-preset guard for
compiling `FakeBleApi` into `hio_hw_ble`. Since then the code got `HIO_TEST_OVERRIDES` (top-level `CMakeLists.txt`):
a test-only switch, ON in the debug/sanitizer presets, that fails a release-type single-config build, never reaches
Release/RelWithDebInfo/MinSizeRel in a multi-config build (`HIO_TEST_OVERRIDES_DEFINE` genex), and allows a
one-shot local override. It already guards environment overrides such as `HIO_CATALOG_URL`.

## Options considered

1. **Reuse `HIO_TEST_OVERRIDES` (chosen).**
2. A second option `HIO_TEST_FAKES` with its own guard (ADR-0087 as written).

## Decision

- `FakeBleApi` (and `src/fake/*` of `hio_hw_ble`) is compiled only when `HIO_TEST_OVERRIDES` is ON, under the
  `HIO_TEST_OVERRIDES` preprocessor define (`${HIO_TEST_OVERRIDES_DEFINE}`); in other builds the sources are not part
  of the target and `createBleApi()` always returns `QtBleApi`.
- `createBleApi()` returns the fake only when that define is set **and** `HIO_FAKE_BLE_SCENARIO` is non-empty.
- No new option, no new release guard: the existing guard covers it.

## Consequences

- One switch and one guard for every test-only behaviour in shipped code.
- Tests that need the fake BLE backend are registered only when `HIO_TEST_OVERRIDES` is ON (as the catalog-URL
  tests are).
