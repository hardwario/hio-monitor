# ADR-0181 — Project prefix `hwio` / `HWio` / `HWIO_` (executables `hwio` and `hwio-monitor`)

- **Status:** Accepted (supersedes the executable names of ADR-0018)
- **Date:** 2026-10-27
- **Decided by:** Matej Strnad (plan 0024)

## Context

ADR-0018 named the CLI `hio` and the GUI `hio-monitor`, and the code took the same prefix: C++ namespace `hio::`,
include paths `<hio/...>`, CMake modules `Hio*.cmake`, CMake targets `hio_*`, macros, CMake variables and environment
variables `HIO_*`, Qt log categories `hio.*`, translation files `hio_*.ts`. The user wants the prefix to read as
"HARDWARIO" more clearly. v2 has had only one testing alpha (2.0.0-alpha.1), so the names can still change.

## Options considered

- Keep `hio`.
- `hwio` everywhere: lower case `hwio`, CamelCase `HWio`, upper case `HWIO_`, executables `hwio` / `hwio-monitor`.
- Same, but keep the GUI executable `hio-monitor`.
- Backward compatibility for the alpha (also read the old `HIO_*` environment variables) or none.

## Decision

| Item | Old | New |
|---|---|---|
| CLI executable | `hio` | **`hwio`** |
| GUI executable, package names, icons, single-instance pipe | `hio-monitor` | **`hwio-monitor`** |
| C++ namespace / include root | `hio::` / `<hio/...>` | **`hwio::`** / **`<hwio/...>`** |
| CamelCase names (CMake modules, any `Hio*` type) | `Hio*` | **`HWio*`** |
| Macros, CMake variables, environment variables | `HIO_*` | **`HWIO_*`** |
| CMake targets, translation files, log categories | `hio_*`, `hio_*.ts`, `hio.*` | **`hwio_*`**, **`hwio_*.ts`**, **`hwio.*`** |

Unchanged: product name "HARDWARIO Monitor", reverse-DNS id `com.hardwario.monitor`, Qt organization / application
names (so settings paths stay), the GitHub repository `hardwario/hio-monitor` (update check, release links), firmware
names from the catalog (e.g. `hio-chester-push-fm`), QML module URI `HardwarioMonitor`.

No backward compatibility: old names are not read. The release notes of the next alpha say that `hio` became `hwio`
and `HIO_*` variables became `HWIO_*`.

ADRs and feature plans written before this ADR are history and keep the old names.

## Consequences

- Scripts of alpha testers must replace `hio` with `hwio` and `HIO_*` with `HWIO_*`.
- Branches with old names (e.g. the CI repair branch) are merged after running `tools/rename-hwio.sh` on them.
- Every later plan and ADR uses the new names.
