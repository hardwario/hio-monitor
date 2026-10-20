# 0003 — M2 Flashing: J-Link programmer, firmware images and catalog, nRF Util modem update, CLI

- **Status:** In progress (implementation started 2026-10-11; approved by Matej Strnad, 2026-10-11, in the text amended by ADR-0057 and ADR-0062; ADR-0069 is not part of this implementation); amended by 0007 (approved by Matej Strnad 2026-10-16 together with 0007–0010)
- **Milestone:** M2
- **Author:** Claude (with Matej Strnad), 2026-10-09
- **Related ADRs:** 0006, 0007, 0015, 0016, 0017, 0022, 0024, 0026, 0027, 0028–0032, 0034, 0035–0042, 0057, 0062, 0069, 0109, 0114
- **Depends on:** 0001 (M0 skeleton), 0002 (M1 J-Link & RTT: library loading, `JLinkProbe`, `DeviceSession`, fake library)

> **Amended by ADR-0057 and ADR-0062 (M3 planning, applied in this text):** every operation carries its
> target MCU. The `Operation*` types are concrete, an LTE bundle is one operation, and reset is an operation.
> Cancel reports the M0 code `cancelled`. Operations also work on sessions opened with `attachRtt = false`.

> **Amended by ADR-0114 (2026-10-16, plan 0006 review F5):** `SessionOptions` gets an optional `nrfUtilProvider`.
> When it is set, `updateModem()` / `flashLteBundle()` call it on the owner thread each time such an operation is
> requested, and the options travel with the operation to the worker. When it is empty, the snapshot of `open()` (R9)
> is used. The GUI passes a provider; the CLI keeps its fixed `--nrfutil` options.

> **Amended by 0007 (approved 2026-10-16; ADR-0069; applied in this text):** `OperationKind::Diagnose` with
> `DeviceSession::diagnose(targetId)`, the `DiagnoseStep` result type, `OperationResult::checks` and the
> `diagnoseStep` signal; operations also run on probe-only sessions (`connectTarget = false`, 0002). Diagnose never
> resets, halts or writes. The implementation is tracked in 0007 (step B2), not in this plan's checklist.

> **Pending amendment from M6 (ADR-0099, specified in [0013](0013-m6-mcp-core-cli.md); applied to this text when 0013
> is approved — then this plan needs re-approval):** new operations `halt`, `go`, `targetStatus`, `readMemory` with
> access width, `writeMemory`, `readRegisters`, `memoryZones`, `writeFlash` (raw bytes through `IProgrammer`, M2
> range and PIB rules); CLI `chester app|lte status|halt|go|regs|zones|mem read|mem write|flash-write`.

## Context

Flashing is the second core feature of HARDWARIO Monitor. The CHESTER has two MCUs: the application MCU
(nRF52840, "app") and the LTE MCU (nRF9160 or nRF9151, "lte"). Users program application firmware from a
local `.hex` file or by a 32-character firmware ID from the HARDWARIO catalog. LTE firmware comes as a
CHESTER bundle (`modem.zip` + `application.hex`).

**py-hardwario v1.7.2** is the behavioural reference. It was read for this plan (`hardwario/device/nrfjprog.py`,
`hardwario/chester/nrfjprog.py`, `hardwario/chester/cli/{app,lte,validate}.py`, `hardwario/common/pib.py`,
`hardwario/common/utils.py`, `hardwario/resources/nrf91_disable_ap_protect.hex`). It is a thin layer over
pynrfjprog: it opens the session with family UNKNOWN, connects to the probe (by SN or the first one),
maps `NO_EMULATOR_CONNECTED` / `LOW_VOLTAGE` to cable / power hints and checks the device family (app = NRF52,
lte = NRF91). Then it programs with `sys_reset → halt → erase_file(ERASE_SECTOR) → program_file → verify_file →
sys_reset → go|halt`. It does no hex parsing and no image checks; nrfjprog does all of that. It writes modem firmware with nrfjprog's built-in
modem DFU (`program_file(modem.zip)`); v2 uses nRF Util for that instead (ADR-0016).

py-hardwario defects found while reading, which v2 does **not** reproduce:

- `chester lte flash` with a `.zip` that is not a 2-entry bundle programs nothing and prints "Successfully completed".
- Group-level `-n/--jlink-sn` of `chester app` is overwritten with `None` by `flash`, `erase`, `reset`, `pib`, `uicr`.
- `chester app flash` without an argument crashes (`len(None)`) when no build output is found.
- The firmware cache returns any existing file unchecked; a failed download leaves a partial file that becomes a
  cache hit; no HTTP timeouts.
- `erase` without `--all` erases pages from address 0 over the descriptor size (not from `start`).
- Recover + `nrf91_disable_ap_protect.hex` exists only inside `device nrf91 pib write`; `chester` has no recover.

The **old app** (branch `main`, `src/flash.cpp`, `src/filedownloader.cpp`) had one hard-coded NRF52 nrfjprog pipeline
in a leaked `QThread`, no family check, no probe selection, no progress (indeterminate bar), required the user to
detach RTT manually, re-downloaded catalog firmware every time with no timeout or HTTP status check, ignored save
errors and wrote an unbounded flash log.

M2 delivers the backend and the CLI. The GUI flash page is M3, the flash wizard M4, MCP flash tools M6.

## Goals / Non-goals

**Goals**

1. **Phase 0 hardware spike** (required by ADR-0015) with explicit go/no-go criteria, before any other phase.
2. `libs/firmware` (new): Intel HEX parser and `FirmwareImage`, read-only zip access (miniz, ADR-0041), LTE input
   classification (ADR-0040), firmware catalog download by ID with a bounded cache (ADR-0037).
3. `libs/hw/jlink`: `IProgrammer` on the J-Link library (ADR-0015, ADR-0035): program, read-back verify,
   erase (code / all), reset, halt/go, memory read, FICR family detection, recover (CTRL-AP ERASEALL).
4. `libs/hw/nrfutil` (new): nRF Util locator, version check (ADR-0042), modem firmware update (ADR-0016).
5. `libs/device`: profile extensions (flash/UICR ranges, expected parts, PIB location, recover image), flash /
   erase / recover / modem workflows running as `DeviceSession` operations with progress, PIB v2 decoder,
   UICR handling (ADR-0036), the nRF91 disable-AP-protect image (ADR-0039).
6. CLI: `hio chester app flash|erase|reset|recover|pib read|uicr read`, `hio chester lte flash|erase|reset|recover`,
   global `--nrfutil`, confirmations per ADR-0038.
7. Fake J-Link library extended with a flash memory model so everything is tested in CI; opt-in HIL tests.
8. Every user-visible text translatable (ADR-0027); every error an `hio::Error` with cause and suggestion.

**Non-goals**

- PIB / UICR **write** (open-questions #11), catalog browsing / upload / delete and Cloud token handling
  (open-questions #15), `lte trace`, generic `hio device <family>` commands.
- GUI flash page (M3), flash and setup wizards (M4; errors already carry `WizardId`s), MCP tools (M6).
- Delta modem firmware handling beyond what nRF Util does itself.

## User experience

No GUI changes in M2. CLI transcripts (English only, ADR-0027):

```
$ hio chester app flash 0123456789abcdef0123456789abcdef
Using J-Link 801234567 (J-Link probe), library 8.10a
Firmware 0123456789abcdef0123456789abcdef: cached (412 KiB)
Target: nRF52840 (app)
Erasing     [##############################] 100 %
Programming [##############################] 100 %  412 KiB  3.1 s
Verifying   [##############################] 100 %
Resetting (go)
Successfully completed
```

```
$ hio chester lte flash chester-lte-v2.1.0.zip
Bundle: modem firmware mfw_nrf91x1_2.0.2 + application.hex (198 KiB)
Using nRF Util 7.13.0 (device 2.8.1)
[1/2] Modem firmware  [##############################] 100 %
[2/2] Application     [##############################] 100 %
Resetting (go)
Successfully completed
```

```
$ hio chester lte recover
This erases ALL flash, UICR and the Product Information Block of the LTE MCU and removes read-back protection.
Continue? [y/N] y
Recovering (can take up to 30 s)…
Programming the debug-port unlock image…
Successfully completed
```

Progress rendering: a bar per phase on a TTY (stderr), plain phase lines when stderr is not a TTY, NDJSON
events in `--json` mode. Ctrl+C cancels between blocks (never in the middle of an erase-all or recover), resets
the target out of halt where possible and exits 130; the probe is always released.

## CLI

All `chester app` / `chester lte` subcommands accept `-n/--jlink-sn SN` and `--jlink-speed KHZ` both on the
group and on the subcommand; the subcommand value wins only if given (py-hardwario `-n` bug fixed).

| Command | Behaviour |
|---|---|
| `hio chester app flash [HEX_FILE_OR_ID] [--halt] [--allow-uicr] [--no-cache]` | Argument rules: an existing path → file; else 32 hex digits → catalog ID (download / cache, ADR-0037); else `usage.invalid_argument` "path does not exist". No argument → first non-empty of `build/` then `build/zephyr/` × `merged.hex`, `zephyr.hex`, `merged_chester_nrf52840.hex`; none → `usage.invalid_argument` listing searched paths. Sequence per Design. `--halt` leaves the CPU halted. `--allow-uicr` per ADR-0036. |
| `hio chester app erase [--all] [--yes]` | Without `--all`: erase the code flash of the target (UICR and PIB kept). `--all`: erase-all of flash **and UICR** (PIB lost) — confirmation per ADR-0038. |
| `hio chester app reset [--halt]` | Reset; go (default) or halt. |
| `hio chester app recover [--yes]` | ADR-0039: confirmation, CTRL-AP ERASEALL, reset. |
| `hio chester app pib read [--json]` | Read 128 B at UICR + 0x80, decode PIB v2. Human: the seven `Name: value` lines of py-hardwario. `--json`: `data = {"signature":"0xbabecafe","version":"0x02","size":"0x7b","crc":"0x…","vendor_name","product_name","hw_variant","hw_revision","serial_number","claim_token","ble_passkey"}` (py-hardwario keys). Invalid PIB → `pib.invalid` naming the failed check. |
| `hio chester app uicr read --format hex\|bin FILE` | 128 B at UICR + 0x80 as lowercase hex without newline, or raw bytes; `FILE` `-` = stdout (py-hardwario compatible). |
| `hio chester lte flash FILE [--allow-uicr]` | ADR-0040 input detection: bundle → modem (nRF Util) then application (J-Link); modem zip → modem only; `.hex` → application only. No catalog IDs (as py-hardwario). Always resets and runs. |
| `hio chester lte erase [--yes]` | Erase-all (py-hardwario semantics) — confirmation per ADR-0038. |
| `hio chester lte reset` | Reset and run. |
| `hio chester lte recover [--yes]` | ADR-0039: confirmation, ERASEALL, program the disable-AP-protect image, reset. |
| global `--nrfutil PATH` | nRF Util override; precedence: option > env `HIO_NRFUTIL` > setting `nrfutil/path` > auto-discovery. |

Confirmation (ADR-0038): on a TTY stdin an interactive `[y/N]` prompt naming what will be lost; non-TTY without
`--yes` → `usage.confirmation_required` (exit 2). `--yes` never changes anything else.

**`--json` stream** (NDJSON envelopes, ADR-0026) for `flash`, `erase`, `recover`, `lte flash`:

```json
{"schema":1,"ok":true,"data":{"type":"progress","step":1,"steps":2,"phase":"programming","done":204800,"total":421888}}
{"schema":1,"ok":true,"data":{"type":"warning","error":{"code":"probe.firmware_outdated","...":"..."}}}
{"schema":1,"ok":true,"data":{"type":"result","target":"app","part":"nRF52840","image":{"source":"catalog","id":"0123…","bytes":421888,"sha256":"…"},"halted":false,"durationMs":5120}}
```

`phase` = `connecting`|`downloading`|`switchingTarget`|`erasing`|`programming`|`verifying`|`modem`|`recovering`|`resetting`|`restoring`
(`switchingTarget` / `restoring` occur only in the GUI's cross-target case, ADR-0057; the CLI opens its session on
the operation's target).
`step`/`steps` count the images in a bundle. A failure ends with the error envelope and its exit code.
`reset` / `pib read` / `uicr read` print one result envelope. Exit codes per ADR-0024 (no new categories).

## Design

### Modules

```
libs/firmware/    hio_firmware   (QtCore, QtNetwork, miniz)    include/hio/firmware/*.h  src/*  tests/*
libs/hw/jlink/    + programmer (extends hio_hw_jlink)
libs/hw/nrfutil/  hio_hw_nrfutil (QtCore)                      include/hio/hw/nrfutil/*.h  src/*  tests/*
libs/device/      + flash workflows, PIB, profile extensions, resources/nrf91_disable_ap_protect.hex
app/cli/src/      flash_commands.cpp, progress_ui.{h,cpp}, confirm.{h,cpp}
tests/fake_jlink/ + flash memory model, protection state
tests/fake_nrfutil/ test-only executable emulating `nrfutil --version` / `device program --json`
```

New libraries are created with `hio_add_backend_library` (static, ADR-0022; no Qt GUI modules, ADR-0004).
`hio_device` links `hio_firmware` and `hio_hw_nrfutil`.

### `libs/firmware`

- **`FirmwareImage`** — `struct Segment { std::uint32_t address; std::vector<std::byte> data; };`
  `class FirmwareImage { std::vector<Segment> segments_; /* sorted, non-overlapping, adjacent merged */ }` with
  `segments()`, `totalBytes()`, `sha256()`, `std::optional<std::uint32_t> startAddress()`,
  `[[nodiscard]] Result<FirmwareImage> splitOut(AddressRange)` (used for UICR, ADR-0036).
- **`IntelHex`** — `[[nodiscard]] Result<FirmwareImage> parseIntelHex(std::span<const std::byte>, std::string_view sourceName)`:
  record types 00 data, 01 EOF, 02 extended segment address, 03 start segment address, 04 extended linear address,
  05 start linear address; per-record checksum; `\r\n` / `\n`; EOF required, data after EOF rejected; overlapping
  data → error (identical bytes allowed); max **16 MiB** of data and **1 MiB** file line count limit
  (`firmware.hex_parse_error` with line number). Input ≤ **64 MiB** file size.
- **`ZipReader`** (ADR-0041, miniz, RAII) — `open(path)`, `entries()` (name, size), `extract(name, maxBytes)` into
  memory, `extractTo(name, dir, maxBytes)`. Limits: **64** entries, **128 MiB** per entry uncompressed, compression
  ratio ≤ 200; names with `..`, absolute paths or drive letters rejected (zip-slip); nested zips are read as bytes.
  Errors → `firmware.invalid_image` with the zip reason in `detail`.
- **`LteInput`** classifier (ADR-0040) — `[[nodiscard]] Result<LteInput> classifyLteInput(path)` →
  `variant<Bundle{modemZip: path (extracted to a QTemporaryDir), application: FirmwareImage}, ModemOnly{path}, AppHex{FirmwareImage}>`.
  Bundle = zip whose top-level entries are exactly `modem.zip` and `application.hex` (case-sensitive, as
  py-hardwario). Modem zip = zip containing Nordic modem firmware markers (`firmware.update.image.segments.*.hex`
  and/or a `*.digest.txt` / manifest — exact markers fixed in Phase 0 from real `mfw_nrf91*` files).
  Other zips and non-hex files → `firmware.invalid_image` "not a CHESTER LTE bundle, modem firmware or HEX file".
- **`FirmwareCatalog`** (ADR-0037) — lives on the owner thread (GUI main / CLI `QCoreApplication`), owns one
  `QNetworkAccessManager` (Qt parent ownership). `download(FirmwareId, Options{noCache}, ProgressFn, std::stop_token)`
  returns a `CatalogOperation` QObject emitting `progress(done,total)` and `finished(Result<CachedFirmware>)`;
  the CLI runs a local event loop until finished. URL `https://firmware.hardwario.com/chester/{id}/hex`
  (base URL in the CHESTER profile); `QNetworkRequest::setTransferTimeout(60 s)`; redirects only to https;
  HTTP 404 → `firmware.not_found_in_catalog`; other non-2xx / network errors → `network.download_failed`
  (HTTP status + Qt error in `detail`); body capped at **32 MiB**; body must parse as Intel HEX before it is cached.
  `FirmwareId` = exactly 32 hex digits, normalized to lowercase.
- **`FirmwareCache`** — `AppLocalDataLocation/firmware-cache/<id>.hex`; written with `QSaveFile` only after a full,
  parsed download; a hit is re-parsed (a corrupt entry is deleted and re-downloaded); LRU by mtime (touched on hit),
  limits **100 files / 500 MiB**, enforced after each insert; `--no-cache` bypasses the read but still stores.
  Cache directory errors → `Warning` (`io.write_failed`) and the download continues in memory.

### `libs/hw/jlink` — programmer

- **ABI additions** in `jlink_abi.h` (ADR-0028), final list fixed in Phase 0: `JLINKARM_BeginDownload`,
  `JLINKARM_WriteMem`, `JLINKARM_EndDownload`, `JLINKARM_ReadMemEx`, `JLINK_EraseChip`,
  `JLINKARM_SetFlashProgProgressCallback` (if usable), `JLINKARM_CORESIGHT_Configure`,
  `JLINKARM_CORESIGHT_ReadAPDPReg`, `JLINKARM_CORESIGHT_WriteAPDPReg`, `JLINKARM_WriteU32`, `JLINKARM_ReadMemU32`.
  `IJLinkApi` gets the matching typed methods (spans, no pointer+length).
- **`IProgrammer`** (implemented by `JLinkProgrammer`, used only on the session worker):
  ```cpp
  struct ProgramProgress { ProgramPhase phase; std::uint64_t done; std::uint64_t total; };
  using ProgressFn = std::function<void(const ProgramProgress&)>;   // called on the worker, never throws
  class IProgrammer {
  public:
      virtual ~IProgrammer() = default;
      [[nodiscard]] virtual Result<ChipInfo> identify() = 0;                       // FICR INFO.PART/VARIANT, protection state
      [[nodiscard]] virtual Status program(const firmware::FirmwareImage&, const ProgressFn&, std::stop_token) = 0;
      [[nodiscard]] virtual Status verify(const firmware::FirmwareImage&, const ProgressFn&, std::stop_token) = 0;
      [[nodiscard]] virtual Status eraseCode(AddressRange flash, const ProgressFn&, std::stop_token) = 0;
      [[nodiscard]] virtual Status eraseAll() = 0;                                 // flash + UICR
      [[nodiscard]] virtual Status recover(std::stop_token) = 0;                   // CTRL-AP ERASEALL
      [[nodiscard]] virtual Result<std::vector<std::byte>> readMemory(std::uint32_t address, std::size_t size) = 0;
      [[nodiscard]] virtual Status reset(ResetMode /*Go, Halt*/) = 0;
  };
  ```
- **Program** (ADR-0035): `BeginDownload` → `WriteMem` per segment in **4 KiB** chunks (progress + stop check per
  chunk) → `EndDownload` (the flash loader erases only the touched sectors and programs; return value < 0 →
  `target.program_failed`). **Verify** = `ReadMemEx` of every written range in **16 KiB** chunks, byte compare;
  first mismatch → `firmware.verify_failed` with address in `detail`. Bytes outside flash/UICR (e.g. RAM records)
  are rejected earlier by the workflow (`firmware.out_of_range`).
- **eraseCode** = sector-wise erase of the profile's flash range via the flash loader (Phase 0 decides between the
  loader erase API and programming 0xFF pages); never touches UICR. **eraseAll** = `JLINK_EraseChip` (spike confirms
  it erases UICR on nRF52840 and nRF91, i.e. equals nrfjprog `erase_all`).
- **identify** reads FICR `INFO.PART` / `INFO.VARIANT` (nRF52: `0x10000100`/`0x10000104`; nRF91: `0x00FF0140`/`0x00FF0144`)
  and the CTRL-AP `APPROTECTSTATUS`; a protected chip → `target.protected` before anything is written.
- **recover**: CoreSight access to CTRL-AP (AP index 1 on nRF52, AP 4 on nRF91 — confirmed in Phase 0):
  write `ERASEALL = 1`, poll `ERASEALLSTATUS` every 100 ms up to **60 s**, then `RESET` toggle; reconnect the core.
  Failure / timeout → `target.recover_failed`.
- Timeouts: program and verify **5 min** overall, erase-all **60 s**, single memory op 5 s (library calls are
  synchronous; overall deadlines are checked between chunks).

### `libs/hw/nrfutil`

- **`NrfUtilLocator`** — override path > env `HIO_NRFUTIL` > setting `nrfutil/path` > `PATH` lookup of `nrfutil`
  (`nrfutil.exe`) > standard install dirs (`~/.nrfutil/bin`, `%USERPROFILE%\.nrfutil\bin`, `/usr/local/bin`).
  Not found → `dependency.nrfutil_not_found` (searched paths in `detail`, wizard FirstRunSetup).
- **Version check** (ADR-0042): `nrfutil --version` and `nrfutil device --version` (parsed, 10 s timeout); missing
  `device` command → `dependency.nrfutil_device_missing` "run `nrfutil install device`"; older than the minimum →
  `dependency.nrfutil_too_old`.
- **`IModemUpdater` / `NrfUtilModemUpdater`** — `[[nodiscard]] Status updateModem(path modemZip, std::uint32_t probeSn, const ProgressFn&, std::stop_token)`:
  `QProcess` (owned, on the calling worker thread with its own `waitFor*` loop — no event loop needed) running
  `nrfutil device program --firmware <zip> --serial-number <SN> --json` (exact arguments fixed in Phase 0), NDJSON
  progress parsed into `ProgramProgress{Modem,…}`; stdout/stderr capped at **1 MiB** each (excess dropped and
  counted, tail kept for `detail`); overall timeout **10 min**; cancel = `terminate()`, then `kill()` after 5 s.
  Non-zero exit / error JSON → `firmware.modem_update_failed` with nRF Util's message in `detail`.
  Arguments are passed as a list (no shell). The environment is inherited.

### `libs/device`

- **Profile extensions** (ADR-0032): `TargetProfile` gains `std::span<const ChipPart> parts` (FICR part number →
  display name + J-Link device name; app: `0x52840` → nRF52840 / `NRF52840_xxAA`; lte: `0x9160` → nRF9160 /
  `NRF9160_xxAA`, `0x9120` → nRF9151 / `NRF9151_XXCA`), `AddressRange flash`, `AddressRange uicr`,
  `std::uint32_t pibOffset` (0x80 app, 0x108 lte), `bool pibErasableWithUicr`, `RecoverImage recoverImage`
  (`None` | `Nrf91DisableApProtect`), `EraseScope defaultErase` (app: Code, lte: All). The `lte` target becomes
  usable for programming; its console stays out of scope.
- **Connecting to the LTE MCU**: connect with the first part's J-Link name, read FICR, reconnect with the exact
  name if it differs (Phase 0 verifies whether a generic Cortex-M33 name is better).
- **Family check**: unknown or other-target part → `target.wrong_family` "Expected the %1 MCU (%2) but found %3".
- **`FlashWorkflow`** (pure logic over `IProgrammer`, unit-tested with GMock):
  1. `identify` → family check → protection check.
  2. Validate the image: every byte inside `flash` or `uicr` of the target, else `firmware.out_of_range`.
     UICR bytes present → ADR-0036 (`firmware.uicr_in_image` unless `allowUicr`).
  3. `reset(Halt)` → `program` (Erasing/Programming phases from the loader progress) → `verify` →
     with `allowUicr`: UICR read-modify-write keeping the PIB bytes (`pibErasableWithUicr`) → `reset(Go|Halt)`.
  4. Log every phase to the `hio.flash` category and to the session's console file (`@` lines).
- **`EraseWorkflow`**, **`RecoverWorkflow`** (recover → on nRF91 program + verify the embedded
  `nrf91_disable_ap_protect.hex` → reset Go), **`ModemWorkflow`** (session `suspend()` → `updateModem` →
  `resume()`; `resume()` also runs on failure and cancel), **`LteBundleWorkflow`** (ADR-0062: step 1/2
  `ModemWorkflow`, step 2/2 `FlashWorkflow` with the bundle's `application.hex`; a failed or cancelled step 1
  skips step 2).
- **`TargetSwitch`** (ADR-0057, worker-internal RAII guard): when the operation's target differs from the
  session's current probe target, it pauses RTT (ADR-0034 "stop without reporting a disconnect"), connects the
  probe to the operation target's J-Link device (phase `SwitchingTarget`; for `Modem` / `LteBundle` the probe is
  released via `suspend()` instead), and in its destructor reconnects to the console target and re-attaches RTT
  (phase `Restoring`). This also happens on failure and on cancel. A failed restore → the session reports
  `Disconnected` with the error, and normal auto-reconnect rules apply. Sessions opened with `attachRtt = false`
  skip the RTT pause and re-attach.
- **`Pib`** — `[[nodiscard]] Result<Pib> decodePib(std::span<const std::byte, 128>)`: big-endian, signature
  `0xBABECAFE`, version 1|2, size, CRC-32 (reflected `0xEDB88320`, init/xorout `0xFFFFFFFF`) over `[0, size-4)`
  stored big-endian at `size-4`; fields per `plan/0002-legacy-inventory.md`; ASCII, trailing NULs stripped.
  Check order as py-hardwario (version, signature, size, crc) → `pib.invalid` with the failed check in `detail`.
- **Embedded image**: `libs/device/resources/nrf91_disable_ap_protect.hex` copied verbatim from py-hardwario
  (MIT, © HARDWARIO a.s.; origin commit `9ada1c6`), compiled in via `qt_add_resources`; a unit test checks its
  SHA-256 and that it parses to one segment in `0x0–0xBA4` (ADR-0039).
- **`DeviceSession` additions** (façade API, ADR-0031, ADR-0057, ADR-0062; each is one command on the worker,
  results via a one-shot completion event; RTT is paused with ADR-0034 "stop without reporting a disconnect" and
  re-attached after, unless the result leaves the CPU halted). Every operation names its **target**, which must be
  a target of the session's profile (else `usage.invalid_argument`):
  ```cpp
  enum class OperationKind { Flash, EraseCode, EraseAll, Recover, Modem, LteBundle, Reset, ReadMemory,
                             Diagnose /* amended by 0007, ADR-0069 */ };
  enum class ProgramPhase { Connecting, SwitchingTarget, Erasing, Programming, Verifying, Modem, Recovering,
                            Resetting, Restoring };
  struct OperationProgress {
      OperationKind kind;
      std::span<const ProgramPhase> plannedPhases;   // static per kind + target (+ cross-target), for steppers
      ProgramPhase phase;
      int step = 1, steps = 1;                       // LteBundle: 1/2 modem, 2/2 application
      std::uint64_t done = 0, total = 0;             // total 0 = indeterminate
      std::chrono::milliseconds elapsed{};
  };
  // Amended by 0007 (ADR-0069); DiagnoseStep reuses diag::CheckStatus (header-only target hio::diag_types, 0007)
  struct DiagnoseStep { std::string id; diag::CheckStatus status; std::string result; std::optional<hio::Error> error; };
  struct OperationResult {
      OperationKind kind;
      Status status;                                 // error incl. code/wizard; cancel → M0 code `cancelled`
      std::chrono::milliseconds duration{};
      std::optional<ChipInfo> chip;                  // identified part (shown by GUI/CLI)
      std::vector<std::byte> data;                   // ReadMemory only (≤ 64 KiB)
      std::vector<DiagnoseStep> checks;              // Diagnose only, ≤ 16 (amended by 0007, ADR-0069)
  };
  struct FlashOptions { bool halt = false; bool allowUicr = false; };

  [[nodiscard]] Result<OperationId> flash(std::string_view targetId, std::shared_ptr<const firmware::FirmwareImage>, FlashOptions);
  [[nodiscard]] Result<OperationId> flashLteBundle(std::string_view targetId, std::shared_ptr<const firmware::LteBundle>, FlashOptions);
  [[nodiscard]] Result<OperationId> erase(std::string_view targetId, EraseScope);
  [[nodiscard]] Result<OperationId> recover(std::string_view targetId);
  [[nodiscard]] Result<OperationId> updateModem(std::string_view targetId, std::filesystem::path modemZip);  // suspend → nRF Util → resume
  [[nodiscard]] Result<OperationId> reset(std::string_view targetId, ResetMode);   // replaces M1 `Status reset(bool)`
  [[nodiscard]] Result<OperationId> readMemory(std::string_view targetId, std::uint32_t address, std::size_t size);  // ≤ 64 KiB
  [[nodiscard]] Result<OperationId> diagnose(std::string_view targetId);          // amended by 0007 (ADR-0069)
  [[nodiscard]] Status cancel(OperationId);
  signals:
      void operationProgress(hio::device::OperationId, const hio::device::OperationProgress&);
      void operationFinished(hio::device::OperationId, const hio::device::OperationResult&);
      void diagnoseStep(hio::device::OperationId, const hio::device::DiagnoseStep&);  // queued, one per step (0007)
  ```
  - One operation at a time per session; a second → `internal.busy` "Another operation is running".
  - `SessionOptions` gains `bool attachRtt` (false for CLI flash/erase/reset/recover/pib/uicr, so the CLI never
    starts RTT for them). All operations work on such sessions. The GUI opens one for a tab that only flashes.
  - The M1 `Status reset(bool halt)` is removed. The CLI console's F4 / `--reset` and the GUI Reset button call
    `reset(consoleTarget, …)`; `resetOnOpen` stays worker-internal.
  - `LteBundle` (`libs/firmware`): `{ std::filesystem::path modemZip; FirmwareImage application;
    std::shared_ptr<QTemporaryDir> extractDir; }` (from `classifyLteInput`). The temporary directory lives until
    the operation ends.
  - Progress events are coalesced to at most one per 100 ms per operation (plus the final one).
  - Cancel ends with `status` = error `cancelled` (category Cancelled, CLI exit 130; already defined in M0).
  - *Amended by 0007 (2026-10-16, ADR-0069):* operations also run on a probe-only session (`ProbeOpen`, 0002).
    **`Diagnose`** runs six steps in this order, each reported by `diagnoseStep` and collected in
    `OperationResult::checks`; a failed step marks the remaining ones Skipped:
    1. `probe` — probe open, product, SN, firmware status (`probe.firmware_outdated` → Warning);
    2. `power` — `hwStatus()` VTref against the profile's `minTargetMillivolts` (`target.no_power`);
    3. `connect` — connect to the target's J-Link device with the session's speed (automatic when
       `SessionOptions::speedKhz` is `nullopt`, ADR-0090), one attempt (no 3 × 500 ms retry) (`target.connect_failed`);
    4. `identify` — FICR part and family and the protection state as `identify` above (`target.wrong_family`;
       `target.protected` → Warning);
    5. `rtt` — RTT control block search with a 3 s budget (`target.rtt_not_found`); Skipped when protected;
    6. `rtt-buffers` — the profile's buffer names present (`target.rtt_buffer_not_found`).

    It never resets, halts or writes the target. On the console target of a connected session the RTT steps check
    the running attachment instead of re-attaching; for another target `TargetSwitch` applies. `OperationProgress`:
    `phase = Connecting`, `step` = index of the current check (1-based), `steps = 6`. Total ≤ 15 s (a blocking
    vendor call can exceed it; the step then reports `diag.check_timeout`, 0007).

### Error codes added (texts translatable, ADR-0027)

| Code | Category | Message / cause / suggestion | Wizard |
|---|---|---|---|
| `firmware.hex_parse_error` | Firmware | "The firmware file is not a valid HEX file (line %1)." / damaged or wrong file / "Use the .hex file produced by the build or download it again." | FlashFirmware |
| `firmware.invalid_image` | Firmware | "The file is not a supported firmware file." / not a bundle, modem zip or HEX; zip limits exceeded / "Use a CHESTER .hex file or LTE bundle (.zip)." | FlashFirmware |
| `firmware.out_of_range` | Firmware | "The firmware does not fit the %1 MCU (address %2)." / image built for another MCU / "Check that the firmware is for the %1 MCU." | FlashFirmware |
| `firmware.uicr_in_image` | Firmware | "The firmware also writes the UICR configuration area." / build includes UICR settings / "Re-run with --allow-uicr if this is intended; the Product Information Block is kept." | — |
| `firmware.verify_failed` | Firmware | "Verification failed at address %1." / unstable connection, protected or faulty flash / "Flash again; if it repeats, lower the J-Link speed or check the cables." | Troubleshooting |
| `firmware.not_found_in_catalog` | Firmware | "Firmware %1 was not found in the catalog." / wrong or deleted ID / "Check the firmware ID." | FlashFirmware |
| `firmware.modem_update_failed` | Firmware | "The modem firmware update failed." / wrong modem zip, connection lost, nRF Util error (detail) / "Check the cable and power, then flash the modem firmware again." | Troubleshooting |
| `network.download_failed` | Network | "Downloading firmware %1 failed." / no internet, proxy, server error / "Check the internet connection and try again." | — |
| `target.wrong_family` | Target | "Expected the %1 MCU (%2) but found %3." / J-Link connected to the other connector / "Connect the J-Link to the %1 connector." | ConnectDevice |
| `target.protected` | Target | "The %1 MCU is read-back protected." / APPROTECT enabled / "Run `hio chester %2 recover` (erases the MCU)." | Troubleshooting |
| `target.program_failed` | Target | "Programming the %1 MCU failed." / connection lost, low power, protected / "Check the cables and power, then try again." | Troubleshooting |
| `target.erase_failed` | Target | "Erasing the %1 MCU failed." / same / same | Troubleshooting |
| `target.recover_failed` | Target | "Recovering the %1 MCU failed." / no power, CTRL-AP not reachable / "Power-cycle the device and try again." | Troubleshooting |
| `pib.invalid` | Target | "The Product Information Block is missing or damaged (%1)." / never written, erased by erase --all or recover / "Contact HARDWARIO support to restore it." | — |
| `dependency.nrfutil_not_found` | Dependency | "nRF Util was not found." / not installed; needed only for modem firmware / "Install nRF Util and its device command, or set its path with --nrfutil." | FirstRunSetup |
| `dependency.nrfutil_device_missing` | Dependency | "The nRF Util device command is not installed." / — / "Run `nrfutil install device`." | FirstRunSetup |
| `dependency.nrfutil_too_old` | Dependency | "nRF Util %1 is too old." / minimum per ADR-0042 / "Run `nrfutil self-upgrade` and `nrfutil upgrade device`." | FirstRunSetup |
| `internal.busy` | Internal | "Another operation is running on this device." / — / "Wait for it to finish or cancel it." | — |

(The final message wording is settled during implementation; codes and categories are the contract.)

### Threading summary

Session worker: `JLinkProgrammer`, all workflows, the nRF Util `QProcess` (blocking waits with stop checks).
Owner thread: façade, `FirmwareCatalog` (QNetworkAccessManager), `FirmwareCache`, zip extraction and HEX parsing
for CLI/GUI inputs (done before the operation is queued; GUI will move parsing to a worker in M3 if large files
stall the UI). Images cross threads as `std::shared_ptr<const FirmwareImage>` (immutable). No other shared state.

### Bounds (all documented here)

HEX file 64 MiB, image data 16 MiB · zip 64 entries, 128 MiB/entry, ratio ≤ 200 · download 32 MiB, transfer timeout
60 s · cache 100 files / 500 MiB · write chunk 4 KiB, verify chunk 16 KiB, `readMemory` ≤ 64 KiB · program/verify
5 min, erase-all 60 s, recover 60 s, memory op 5 s · Diagnose 6 steps / 15 s, `checks` ≤ 16 (amended by 0007) · nRF Util output 1 MiB per stream, version probe 10 s,
modem update 10 min, terminate→kill 5 s · progress events ≤ 10/s per operation · one operation per session.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Hardware spike (ADR-0015) | separate plan / phase 0 in this plan / already done | Phase 0 in this plan, go/no-go | — |
| Plan split | one / two / three plans | one phased plan | — |
| Programming method | own HEX parser + flash download + read-back verify / `JLINK_DownloadFile` / own NVMC driver | own parser + flash download + read-back | ADR-0035 |
| UICR records in an image | reject unless `--allow-uicr` / always RMW / always reject | reject unless `--allow-uicr` (RMW keeps PIB) | ADR-0036 |
| Firmware catalog | download + list/info / download only | download by ID only (API needs a token); bounded LRU cache | ADR-0037 |
| Confirmation of destructive operations | only new commands / erase-all + recover / never | erase-all (app + lte) and recover | ADR-0038 |
| Recover | app + lte / lte only / none | app + lte, never automatic | ADR-0039 |
| nRF91 disable-AP-protect image | own source / copy py-hardwario hex / generated in C++ | copy py-hardwario hex | ADR-0039 |
| `lte flash` input | bundle + modem zip + hex / bundle + hex | bundle, modem zip or hex, by content | ADR-0040 |
| Zip library | miniz / minizip-ng / Qt private QZipReader | miniz via CPM | ADR-0041 |
| Minimum nRF Util | from spike / latest | from spike | ADR-0042 (Proposed) |
| `app flash` without argument | keep (fixed) / drop | keep, fixed | — |
| New dependency miniz (MIT) | approve / reject | approve (decided 2026-10-11) | ADR-0041 |
| Operations on another MCU than the console's (M3 review) | operation carries target / retarget tab / second session | operation carries target | ADR-0057 |
| Operation types, LTE bundle, reset (M3 review) | one bundle operation / caller sequences; reset as operation / keep both | concrete types; bundle = one operation; reset = operation; cancel = `cancelled` | ADR-0062 |
| Live connection checks (M4 planning) | session operation `Diagnose` / standalone checker / interpret `open()` | `Diagnose` operation (approved with 0007 on 2026-10-16) | ADR-0069 |
| Minor defaults: flashing as a session operation with RTT paused and re-attached; nRF Util only while suspended; progress phases; FICR family check and LTE device-name selection; full read-back verify; group/subcommand `-n` handling; existing path wins over ID; `pib`/`uicr read` app-only; flash log in the console file + `hio.flash`; all bounds and timeouts above; `internal.busy` | accept / change | accept (decided 2026-10-11) | — |
| Phase 0 order (no CHESTER available on 2026-10-11) | spike first / A–F first on fakes | A–F first against the fake J-Link library and fake nRF Util; Phase 0 and G1 with the CHESTER afterwards (decided 2026-10-11). Values Phase 0 decides use the defaults in this plan, marked "to confirm in Phase 0". Risk: a failed spike criterion (e.g. ADR-0035 UICR behaviour) means a new ADR and rework | — |

## Implementation steps

Phase 0 — hardware spike (go/no-go; originally before Phase A — run after A–F by decision of 2026-10-11, see the decision table)
- [ ] 0.1 Throw-away tool (not merged) on M1's `JLinkLibrary`; CHESTER with nRF52840 + nRF9151 (+ nRF9160 if available), J-Link 8.x on Linux.
  - Run 2026-10-16 on Linux, J-Link Software 9.84, one J-Link probe, CHESTER-M R3.4 (variant CGLS). Only the
    application MCU (nRF52840) was tested; the LTE MCU (nRF9151) is deferred by decision of the user, and nRF Util is
    not installed. No separate spike tool was needed except a throw-away ctypes script for 0.10 (not committed);
    the M2 CLI and `JLinkExe` were used for backups and read-back.
  - Found and fixed during the run: **H1** `JLINKARM_SetLogOutHandler` is not exported by 9.84 → the log handler is
    registered with `JLINKARM_EnableLog`. **H3** `JLINK_SetFlashProgProgressCallback` called right after loading
    the library (before `JLINKARM_EMU_SelectByUSBSN`) made the select fail (-1) → the callback is registered only
    around `EndDownload` and cleared afterwards, as pylink does per flash write (the fake library's select now fails
    while a callback is registered; `FakeLibrary.FlashFunctionsReachTheLibrary`,
    `FakeLibrary.FlashProgressCallbackIsClearedAfterAFailedDownload`). **H4** the wrong-connector error named the
    raw target id ("the lte connector") → targets have a translatable display name ("application", "LTE";
    `Workflow.WrongFamilyOnTheLteTargetNamesTheLteConnector`). **H5** `JLINK_EraseChip` after only a halt returned
    0 but erased nothing on a running nRF52840 (log "Erase: 0.001s") → `eraseAll()` resets and halts first, as
    J-Link Commander's erase does (`Programmer.EraseAll`, `Programmer.EraseAllStopsWhenTheResetFails`).
- [ ] 0.2 Flash-loader download of a real CHESTER app hex keeps UICR and the PIB byte-for-byte (nRF52840, nRF9151).
  - nRF52840 **PASS** (2026-10-16): flashing a full application image (797 696 bytes) kept the UICR and the PIB
    byte-for-byte (whole flash + UICR compared with a backup). nRF9151: open.
- [ ] 0.3 `JLINK_EraseChip` erases flash + UICR on both; code-only erase method chosen.
  - nRF52840 **PASS after fix H5** (2026-10-16): `JLINK_EraseChip` needs a reset + halt first; it then erases flash
    and UICR (all 0xFF afterwards). Code-only erase method: as implemented, `JLinkProgrammer::eraseCode` downloads
    0xFF over the profile's flash range through the flash loader; the G1 `EraseCodeKeepsThePibAndPibDecodes` gate
    passed (erase + reflash 47.8 s). nRF9151: open.
- [ ] 0.4 CTRL-AP ERASEALL recovers a protected nRF52840 and nRF9151; nRF91 stays debuggable after the disable-AP-protect image + reset.
  - Not run (2026-10-16): the user decided not to protect / recover the hardware in this session. Open.
- [ ] 0.5 FICR part read works on both; LTE connect name strategy chosen.
  - nRF52840 (2026-10-16): FICR part 0x52840, variant 0x41414430 read OK. nRF9151 and the LTE connect name
    strategy: open.
- [ ] 0.5b Cross-target switching (ADR-0057): with the J-Link on one CHESTER connector, can the probe connect to
  the other MCU (app ↔ lte) without re-plugging? Record the wiring per CHESTER variant. If not, operations for the
  other target fail early with `target.wrong_family` (connector hint), and the GUI Flash page says so before
  starting → update ADR-0057 with the result.
  - Result (2026-10-16, CHESTER-M): the LTE MCU is **not** reachable through the application connector. A session for
    target `lte` with the probe on the application connector connected (J-Link device `NRF9151_XXCA` selected), but
    FICR showed an nRF52840 → `target.wrong_family` with the connector hint, as designed. Recorded in ADR-0057.
    Other CHESTER variants: not checked.
- [ ] 0.6 Programming speed within 20 % of nrfjprog on the same image.
  - Not run (2026-10-16): nrfjprog is not installed. Note: flashing an image identical to the flash content took
    1.4 s (the flash loader skips equal pages).
- [ ] 0.7 `nrfutil device program` of a modem zip with our session suspended (library copy kept loaded, probe closed); JSON progress format captured; modem zip markers recorded; tested versions → ADR-0042.
  - Open (2026-10-16): nRF Util is not installed.
- [ ] 0.8 Go/no-go recorded; any failed criterion → stop, new ADR, update this plan, ask the user.
  - 2026-10-16: application MCU (nRF52840) **GO** with the fixes H3, H5 and ADR-0109; LTE MCU pending.
- [ ] 0.9 Review R1: with the `JLINK_SetHookUnsecureDialog` hook (answers NO) a protected nRF52840 and nRF91
  are never mass-erased by Open/Connect (PIB unchanged, connect fails → `target.protected`); record what the
  library does. Also the CTRL-AP IDR values (R2, R15: record the full IDRs at AP 1 and AP 4 on both MCUs).
  - Not run (2026-10-16): no protect / recover on hardware in this session (user decision). Open.
- [ ] 0.10 Review R16: abandoned download (R3): on each MCU, read a flash range, `BeginDownload` + `WriteMem` of N
  chunks with a different pattern, close the probe without `EndDownload`, reopen, read the range back — it must be
  unchanged. Otherwise stop: new decision on how a download is abandoned.
  - nRF52840 **FAILED** (2026-10-16): closing the probe without `EndDownload` programs the buffer — 4091 of 4096
    bytes of the page changed. New decision: **ADR-0109** (an abandoned download is reported as an interrupted
    flash, `target.program_failed` "the firmware may be partly written"); implemented in `JLinkProgrammer`
    (`Programmer.WriteFailureAbandonsTheDownload`, `Programmer.DeadlineAbandonsTheDownload`,
    `Programmer.AbandonedEraseCodeIsAnInterruptedFlash`); the fake library's `JLINKARM_Close` now programs a
    buffered download too (`FakeLibrary.CloseDuringADownloadProgramsTheBuffer`). nRF9151: not run.

Phase A — `libs/firmware`
- [x] A1 `FirmwareImage`, `parseIntelHex` + tests (corpus incl. CHESTER build outputs)
- [x] A2 CPM: miniz (version + SHA-256 below); `ZipReader` + limit/zip-slip tests
- [x] A3 `classifyLteInput` + tests
- [x] A4 `FirmwareCache` (LRU, atomic) + tests; `FirmwareCatalog` + tests against a local `QTcpServer` HTTP fixture (200, 404, 500, timeout, oversize, invalid hex, cancel)

Phase B — J-Link programmer
- [x] B1 ABI additions + `IJLinkApi` methods + fake-library symbols
- [x] B2 `JLinkProgrammer` (identify, program, verify, eraseCode, eraseAll, recover, readMemory, reset) + GMock tests

Phase C — device
- [x] C1 Profile extensions (`chester.cpp` app + lte), LTE connect strategy + tests
- [x] C2 `FlashWorkflow` (range check, UICR rules, sequence), `EraseWorkflow`, `RecoverWorkflow` + embedded image, `LteBundleWorkflow`, `TargetSwitch` (ADR-0057) + tests
- [x] C3 `decodePib` + test vectors (valid, erased, bad CRC/signature/size/version)
- [x] C4 `DeviceSession` operations with `targetId` and the ADR-0062 types (incl. `reset` replacing M1's, `flashLteBundle`), `attachRtt`, `internal.busy`, RTT pause/re-attach + TSan tests
- [x] C5 Error codes added to the `libs/core` table (+ `cs` translations)

Phase D — nRF Util
- [x] D1 `NrfUtilLocator`, version checks (ADR-0042) + tests
- [x] D2 `NrfUtilModemUpdater` + `tests/fake_nrfutil` (progress, failure, hang → timeout, cancel, output flood) + `ModemWorkflow`

Phase E — fake J-Link library
- [x] E1 Flash memory model (flash/UICR/FICR per scenario, sector erase semantics, protection state, ERASEALL, write faults, verify mismatch injection)
- [x] E2 Integration tests: flash with RTT attached (pause/re-attach), two sessions flashing concurrently

Phase F — CLI
- [x] F1 `progress_ui` (TTY bars on stderr / plain lines / NDJSON), `confirm` (TTY prompt / `--yes`)
- [x] F2 `chester app flash|erase|reset|recover`, `--nrfutil`, group/subcommand `-n`
- [x] F3 `chester app pib read`, `uicr read`
- [x] F4 `chester lte flash|erase|reset|recover`
- [x] F5 CLI end-to-end tests (QProcess + fake library + fake nRF Util + local HTTP fixture)

Phase G — hardware and docs
- [ ] G1 HIL tests (opt-in `HIO_HIL=1`): flash by file and ID, verify, erase, erase --all, recover (app + lte), LTE bundle, PIB read — code written (`tests/integration/hil_flash_test.cpp`), not run yet. **Release gate (review R5):** `HilFlash.EraseCodeKeepsThePibAndPibDecodes` must pass on nRF52840 (and the LTE MCU) before `erase` ships.
  - Run 2026-10-16 (nRF52840 only, setup as in 0.1): `HilFlash.FlashByFileVerifiesAndKeepsThePib` **PASS** (flash of an
    identical image 1392 ms); `HilFlash.EraseCodeKeepsThePibAndPibDecodes` **PASS** (release gate met for the
    nRF52840; the LTE MCU is pending). Not run: `FlashByCatalogId`, `EraseAllAndRecoverApp`, the LTE tests.
- [ ] G2 Manual checks on Linux, Windows, macOS (incl. nRF Util discovery on each)
- [ ] G3 Update `plan/0006-cli.md` statuses, `plan/0002-legacy-inventory.md` parity rows, roadmap M2 → Done

| Dependency | Version | SHA-256 |
|---|---|---|
| miniz | 3.1.2 (release archive `miniz-3.1.2.zip`, 2026-07-01; amalgamated `miniz.c`/`miniz.h`) | `f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a` |

## Implementation notes (2026-10-11/12, phases A–F on `v2/m2-flashing`)

Phase 0 has not run yet (no CHESTER available). Every value below marked **(Phase 0)** is the default of this
plan / the ADRs / py-hardwario and must be confirmed on hardware; a failed confirmation means a new ADR.

**To confirm in Phase 0**
- **Code-only erase** (0.3): `JLinkProgrammer::eraseCode` downloads 0xFF over the profile's flash range through
  the flash loader (sectors that differ are erased; the UICR is outside the range). **(Phase 0)** Confirmed on
  the nRF52840 on 2026-10-16 (0.3, G1 gate); nRF9151 open.
- **Flash loader keeps UICR/PIB** (0.2): a download touches only the sectors of the written bytes and keeps
  their other bytes (the fake library models exactly this). **(Phase 0)** Confirmed on the nRF52840 on 2026-10-16
  (0.2); nRF9151 open.
- **LTE connect strategy** (0.5): connect as `NRF9151_XXCA` (first part of the `lte` profile), read FICR, and when
  the part is another one (0x9160) send `Device = NRF9160_xxAA` + `JLINKARM_Connect` again on the open probe.
  **(Phase 0)**
- **Cross-target switch** (0.5b, ADR-0057): `Device = <other MCU>` + `JLINKARM_Connect` on the open probe (no
  close/open); restore the same way. **(Phase 0)**
- **FICR / CTRL-AP** (0.4, 0.5): FICR read with `JLINKARM_ReadMemU32` (nRF52 `0x10000100/104`, nRF91
  `0x00FF0140/144`); CTRL-AP index 1 (nRF52) / 4 (nRF91), registers RESET 0x0, ERASEALL 0x4, ERASEALLSTATUS 0x8,
  APPROTECTSTATUS 0xC (bit 0 = 0: protected), selected with DP SELECT after `JLINKARM_CORESIGHT_Configure("")`.
  A target that does not connect is asked through its CTRL-AP (→ `target.protected`). After ERASEALL the CTRL-AP
  RESET is toggled and the core is connected again. **(Phase 0)**
- **Flash-loader progress**: the setter is `JLINK_SetFlashProgProgressCallback` (not `JLINKARM_…`; checked with
  `nm` on 9.84). The C callback has no user data, so it is routed to the running `endDownload()` through a
  `thread_local` pointer, i.e. it must be called synchronously on the calling thread; Compare/Erase are reported
  as Erasing, Program/Verify as Programming (review R6). A call from another thread is ignored. **(Phase 0)**
  HIL H3 (2026-10-16): the callback is registered only for the duration of `EndDownload` (registered at load time,
  it made `JLINKARM_EMU_SelectByUSBSN` fail).
- **`--allow-uicr`** (ADR-0036): the UICR page (4 KiB: nRF52840 `0x10001000–0x10002000`, nRF91
  `0x00FF8000–0x00FF9000`) is read, the image's UICR bytes merged in and the page programmed together with the
  code in one download; on the nRF91 the PIB range (UICR OTP, `pibErasableWithUicr = false`) is left out of the
  write. Whether a 4 KiB read of the UICR is valid on both MCUs and how the loader handles the nRF91 UICR/OTP is
  **(Phase 0)**.
- **nRF Util** (0.7, ADR-0042): arguments `device program --firmware <zip> --serial-number <SN> --json` with the
  J-Link SN zero-padded to 12 digits (as nRF Util lists J-Links); NDJSON parsed tolerantly
  (`data.progress.progressPercentage`, `type: task_end` + `data.result`, `data.message` /
  `data.error.description`); modem zip marker = a top-level `firmware.update.image.segments.<N>.hex`; the
  **provisional minimums core 7.0.0 / device 2.0.0** (`nrfutil::minimumCoreVersion/minimumDeviceVersion`) replace
  ADR-0042's _tbd_ until the spike records the tested versions. **(Phase 0)**
- **Erase-all timeout**: `JLINK_EraseChip` is one synchronous call; the 60 s bound is checked after it returns.
- **Unsecure dialog hook** (review R1): `JLINK_SetHookUnsecureDialog` installed right after Open answers NO
  (`DLG_BUTTON_NO` = 2); a probe whose library rejects the hook is not used. **(Phase 0, 0.9)**
- **CTRL-AP IDR** (reviews R2, R15): nRF52840 `0x02880000`, nRF91 `0x12880000`, compared with the revision
  bits masked (`0x0FFFFFFF`), read from bank 0xF register 0xFC before APPROTECTSTATUS is trusted or ERASEALL is
  written. Under the mask both are the same Nordic CTRL-AP ID: the IDR only proves a Nordic CTRL-AP at that AP
  index, and the AP index (1 vs 4) is the family discriminator. **(Phase 0)**
- **Abandoned download** (review R3): closing the probe discards a buffered download (no `EndDownload`).
  **(Phase 0, item 0.10)**
- **Recover register values** (RESET/ERASEALL/ERASEALLSTATUS/APPROTECTSTATUS, ERASEALLSTATUS polling, RESET
  toggle), the 12-digit nRF Util `--serial-number`, and the 4 KiB UICR read are hardware-unconfirmed (review
  R10); no destructive command (erase --all, recover, --allow-uicr) ships before Phase 0 and G1 pass.

**Local choices (inside the plan, recorded here)**
- `jlink_abi.h` gets `JLINKARM_BeginDownload/WriteMem/EndDownload/ReadMemEx/ReadMemU32`, `JLINK_EraseChip`,
  `JLINK_SetFlashProgProgressCallback`, `JLINKARM_CORESIGHT_Configure/ReadAPDPReg/WriteAPDPReg`;
  `JLINKARM_WriteU32` was not needed and is not declared (the final list is Phase 0's).
- Cancellation (Ctrl+C, `cancel()`): checked before a download starts, between verify chunks (16 KiB), inside
  nRF Util (terminate, kill after 5 s) and between bundle steps. One download (buffering with `WriteMem` +
  `EndDownload`) is not interrupted: `WriteMem` only fills the library's buffer and the library cannot discard a
  buffered download (the plan said "stop check per 4 KiB chunk"; the deadline is still checked per chunk). A
  flash cancelled while the CPU is halted is reset with go (best effort).
- Progress events: a phase or step change is emitted at once (first the coalesced last state of the previous
  phase, e.g. 100 %), updates within a phase at most every 100 ms; phases only move forward through
  `plannedPhases`. Erase reports no `Resetting` (the CPU stays halted, like py-hardwario); after an erase,
  `flash --halt` and `reset --halt` RTT is not re-attached (Disconnected without error, as M1's halt reset).
- Sessions with `attachRtt = false` connect the target and report Connected without RTT; a target without an RTT
  console (`lte`) is accepted only with `attachRtt = false`. An operation connects the target itself when needed
  (also after a failed first connect), so the CLI queues its operation right after `open()`.
- Small API additions next to the plan's façade: `DeviceSession::runningOperation()`, the CLI helper
  `waitForOperation()` (like M1's `waitUntilConnected`), and `SessionOptions::nrfUtil` (locator options carrying
  `--nrfutil`; nRF Util is located and version-checked on the worker when a modem step starts, the CLI checks it
  before opening the session as well so a missing nRF Util fails before the device is touched).
- `FirmwareImage::sha256()` hashes (address, size, data) of every segment (independent of the HEX text);
  `splitOut()` moves the bytes out of the image. Images outside flash/UICR, into the PIB (always) or with UICR
  bytes without `--allow-uicr` are rejected before the probe is used.
- The CLI prints `Using J-Link <SN> (<product>)` (no library version) and `Target: <part> (<target>)` after the
  operation (the part is known from the result). `pib read` / `uicr read` / `reset` print no progress (one result
  envelope); `--json` is the global flag (accepted after the subcommand, too).
- Log categories `hio.firmware` and `hio.flash` were added; the flash log also goes to the session's console file
  as `@` lines (CLI flash sessions use the default console file).
- PIB version 1 is decoded as py-hardwario does with `nrf=True`, i.e. it never passes the size check.
- miniz is built as a static C library (`enable_language(C)`) with `MINIZ_NO_STDIO` (archives are read through a
  QFile callback), `MINIZ_NO_TIME`, `MINIZ_USE_UNALIGNED_LOADS_AND_STORES=0` (UBSan-clean).
- The HEX parser was run on one real CHESTER build output (opt-in `HIO_TEST_HEX_CORPUS`; build outputs are not
  committed).
- Added on this branch for feature 0004 (M3 GUI), approved 2026-10-12: ADR-0091 (`ConnectionEvent::targetMillivolts`)
  and ADR-0092 (`Settings::remove`), and ADR-0093 (`jlink::ProbeEnumerator`, enumeration on a kept library
  copy; amends ADR-0050).
- TSan: the tests that use `QNetworkAccessManager` (10 catalog tests, 2 CLI catalog downloads) are filtered
  out in the TSan preset only: Qt's HTTP thread is uninstrumented Qt code whose posted events and implicitly
  shared buffers TSan reports as races (`ignore_noninstrumented_modules=1` crashes TSan's thread registry);
  `FirmwareCatalog` runs no threads of ours. They run in Debug and ASan/UBSan.
- G1: `tests/integration/hil_flash_test.cpp` is written (label `hil`, `HIO_HIL=1`); erase-all / recover tests
  additionally need `HIO_HIL_DESTRUCTIVE=1` because they erase the PIB. Not run yet.

## Tests and verification

- **Unit (no hardware):** HEX parser (all record types, checksums, EOF rules, overlap, limits, line numbers in
  errors); zip limits, zip-slip, nested zip; LTE classification (bundle, modem zip, hex, 1-entry / 3-entry zips,
  wrong names); cache LRU eviction, corrupt entry, atomic write, `--no-cache`; catalog against the local HTTP
  fixture; PIB vectors; workflows with GMock `IJLinkApi`/`IProgrammer`: exact call sequence of program
  (reset → halt → download → verify → reset go/halt), family mismatch, protected chip, out-of-range image, UICR
  rule and PIB preservation, cancel between chunks, recover + nRF91 image; nRF Util locator precedence and version
  parsing.
- **Session (TSan):** flash while RTT attached (pause, re-attach, no auto-reconnect during the operation),
  `internal.busy`, cancel (result `cancelled`), close during an operation joins within the operation's chunk time;
  cross-target operations (ADR-0057): console on `app`, flash `lte` → phases `SwitchingTarget` … `Restoring`, RTT
  re-attached on `app` after success, failure and cancel; failed restore → `Disconnected`; every operation on an
  `attachRtt = false` session; `LteBundle` 1/2 → 2/2, modem failure skips step 2, cancel in each step;
  `plannedPhases` match the emitted phases for every kind.
- **Fake library integration:** real loader + fake flash model; verify-mismatch injection → `firmware.verify_failed`;
  protected state → `target.protected`; recover clears it.
- **CLI end-to-end:** `flash FILE --json` progress + result; flash by ID via the HTTP fixture (`HIO_CATALOG_URL`
  test-only override); missing file → exit 2; bad hex → 7; 404 → 7; network down → 8; wrong family → 6;
  `erase --all` without TTY and without `--yes` → 2; `lte flash` bundle with fake nRF Util; nRF Util missing → 4;
  Ctrl+C during programming → 130 and the fake probe is released.
- **Sanitizers / static analysis / translations:** ASan+UBSan, TSan, clang-tidy, clang-format, `.ts` freshness (as M0).
- **HIL (CHESTER + J-Link 8.x):** as G1; PIB unchanged after `flash` and `erase` (read before/after); speed recorded.
- **Manual:** the CLI checks on Linux, Windows and macOS, done once by the user before Done.

## Risks and open points

- **Flash-loader UICR behaviour** (ADR-0035): if the J-Link loader erases UICR when only code sectors are written,
  Phase 0 fails → new ADR (e.g. own NVMC sector erase).
- **J-Link Nordic AP handling** differs between J-Link versions (auto-unlock dialogs, nRF91x1 hardware APPROTECT):
  dialogs stay suppressed (M1); behaviour pinned by HIL on the minimum J-Link version (ADR-0029).
- **nRF Util JSON output stability** across versions: parsing is tolerant (unknown fields ignored, progress
  optional); the minimum version is pinned (ADR-0042).
- **nRF Util and our library copy**: nRF Util loads its own J-Link library; our session must have the probe
  closed while it runs (Phase 0.7).
- **Windows antivirus** scanning the library copy or extracted zips during long operations.
- **Catalog browsing and Cloud token storage** postponed (open-questions #15); the GUI flash wizard (M4) needs a decision.

## Open findings (independent review round 1, 2026-10-12)

Review of 7f371c6 (debug 516/516; stress repeats clean). Decided 2026-10-12: fix R1–R10 (R10 = confirm in Phase 0, keep the destructive commands gated by G1), R11 keep the TSan skip + comment in `tsan.supp`. R1 is fixed on `v2/m2-flashing` only; M1 gets it when merged (M1 HIL must use the fixed code).

| # | Severity | Finding | Suggested fix |
|---|---|---|---|
| R1 | high (plausible) | No `JLINK_SetHookUnsecureDialog` hook (`jlink_probe.cpp`): on a protected chip the library's "unsecure (erase)?" dialog takes its default answer in batch mode, which may mass-erase the chip incl. the PIB without the ADR-0038 confirmation. pylink installs a hook answering NO by default. Since M1. | Declare/resolve the hook, install a `noexcept` C hook returning NO after Open; fake-library test; Phase 0/HIL check. |
| R2 | medium | `identify()` checks APPROTECT on every family's CTRL-AP; an absent AP (e.g. AP4 on nRF52840) reads 0 → "protected, run recover" on an unprotected but unreadable chip, or on the wrong connector instead of `target.wrong_family`. `runRecover` proceeds without a family; `recover()` uses `families.front()`. | Check only the expected family's CTRL-AP, verify the CTRL-AP IDR first, return the family; map absent/mismatched IDR to `target.wrong_family` / `target.connect_failed`. |
| R3 | medium | `download()` error paths: the deadline path returns without `EndDownload` (library left in download mode); the `WriteMem` failure path calls `EndDownload`, which programs the partial buffer. | Close download mode in a defined way (reset + reconnect), document, test both paths. |
| R4 | medium | `waitForOperation` can hang: `runningOperation` is cleared only when `OperationFinishedEvent` is drained; a dying worker or `close()` during a wait leaves it set; Ctrl+C only cancels. | `close()` clears it with a synthesized `cancelled` result; the wait also ends on Stopped/`internal.unexpected` and after a bounded grace period after cancel. |
| R5 | medium | Code-only erase writes 0xFF over all flash through the loader; if the loader turns that into a chip erase, UICR/PIB are lost. Unconfirmed (Phase 0.3). | Read the PIB before/after and fail loudly if it changed; keep G1 `EraseCodeKeepsThePibAndPibDecodes` as a release gate. |
| R6 | low | Progress labels: any non-"erase" loader action switches to Programming for good; the real loader reports "Compare" first → no Erasing phase. | Map Compare/Erase/Program/Verify explicitly; test with Compare first. |
| R7 | low | `pib read`, `uicr read`, `reset` skip the family check: wrong connector → `pib.invalid` or the other MCU is reset. Not recorded as a deviation. | Run `identifyTarget` first (or record the deviation). |
| R8 | low | `HIO_CATALOG_URL` honoured in release builds (plan: test-only) and allows http redirects. | Compile it in only for test builds (or accept https only). |
| R9 | low | Default `SessionOptions::nrfUtil` searches nothing; only the CLI fills it (the M3 GUI must call `defaultLocatorOptions()`). | Default to `defaultLocatorOptions()`. |
| R10 | low | Recover/APPROTECT register values, 12-digit `--serial-number`, 4 KiB UICR read unconfirmed on hardware. | Phase 0 before any destructive command ships (already listed). |
| R11 | note | TSan exclusion of the 12 QNetworkAccessManager tests judged acceptable (catalog has no threads of ours). | Keep; add a pointer comment in `tools/sanitizers/tsan.supp`. |

**Fixes of review round 1 (2026-10-12, uncommitted on top of 7f371c6)**

| # | Fix | Tests |
|---|---|---|
| R1 | `JLINK_SetHookUnsecureDialog` declared (`__stdcall` on Win32) and resolved; `JLinkProbe::open()` installs a `noexcept` hook answering `DLG_BUTTON_NO` right after Open in every open mode, and fails with `probe.open_failed` if the library rejects it. Fake library: a protected chip asks the hook on connect (no hook / YES = mass erase, NO = connect fails). Phase 0 item 0.9. | `Probe.InstallsTheUnsecureDialogHookAfterOpen`, `Probe.OpenFailsWithoutTheUnsecureDialogHook`, `FakeLibrary.UnsecureDialogHookAnswersNo`, `FakeLibrarySession.ProtectedChipIsNotErasedByConnecting` |
| R2 | `identify()` trusts only a CTRL-AP whose IDR (bank 0xF, 0xFC) matches; APPROTECTSTATUS is read only on the expected family; `ChipInfo::family` names the family that answered; the workflow maps another family to `target.wrong_family` and an unreadable, unprotected chip to `target.connect_failed`. `recover()` checks the expected CTRL-AP IDR before ERASEALL (`target.wrong_family` / `target.connect_failed`); `runRecover` stops when the family is not confirmed. | `Programmer.AbsentCtrlApsReadZeroAndAreNotProtected`, `Programmer.OtherFamilysCtrlApNamesThatFamily`, `Programmer.UnreadableUnprotectedChipIsNotProtected`, `Programmer.RecoverChecksTheCtrlApIdentification`, `Workflow.UnreadableChipIsNotReportedAsProtected`, `Workflow.OtherFamilysCtrlApIsTheWrongConnector`, `Workflow.RecoverNeedsTheExpectedFamily` |
| R3 | A download that fails (`WriteMem`) or exceeds its deadline before `EndDownload` is abandoned by closing the probe (no `EndDownload`, which would program the partial buffer); the session reopens and reconnects in its restore step. **Amended by ADR-0109** (Phase 0 item 0.10): the close still programs the buffer, so the abandon is reported as an interrupted flash (`target.program_failed`, "may be partly written"). | `Programmer.WriteFailureAbandonsTheDownload`, `Programmer.DeadlineAbandonsTheDownload` |
| R4 | `close()` ends a running operation with a synthesized `cancelled` result; a worker that dies (Stopped + `internal.unexpected`) ends it with that error (`operationFinished` emitted); `waitForOperation()` picks up results delivered before or after `close()` and gives up `cancelGrace` (default 90 s) after a cancel request. | `Operations.WaitEndsWhenTheWorkerDies`, `Operations.WaitEndsWhenTheSessionIsClosed`, `Operations.WaitGivesUpAfterTheCancelGrace` |
| R5 | Code-only erase reads the PIB before and after; a change fails with `target.erase_failed` ("… also changed its Product Information Block") and an `@` log line. G1 `EraseCodeKeepsThePibAndPibDecodes` is a release gate. | `Workflow.CodeEraseThatChangesThePibFails` |
| R6 | Loader actions mapped explicitly: Compare/Erase → Erasing, Program/Verify → Programming, others keep the phase. | `Programmer.LoaderActionsMapToPhases` |
| R7 | Reset (protected allowed) and memory reads (`pib read`, `uicr read`) run the family check first. | `Workflow.ResetAndReadMemoryCheckTheFamily` |
| R8 | `HIO_CATALOG_URL` is compiled in only with the CMake option `HIO_TEST_OVERRIDES` (default = `HIO_BUILD_TESTS`; shipped builds must set it OFF); redirects are followed only to https. | `Catalog.RedirectsOnlyToHttps`; the CLI catalog e2e tests skip without the option |
| R9 | `SessionOptions::nrfUtil` is optional; nullopt = `nrfutil::defaultLocatorOptions()` with the user's settings (resolved in `open()`). *Amended by ADR-0114:* this snapshot is used only without `SessionOptions::nrfUtilProvider`; a provider is evaluated per modem / LTE bundle operation on the owner thread. | `NrfUtilLocatorDefault.UsesTheEnvironmentAndThePath`, `Operations.NrfUtilProviderIsCalledPerOperationOnTheOwnerThread`, `Operations.WithoutAProviderTheSnapshotOfOpenIsUsed` |
| R10 | All hardware-unconfirmed values are listed under "To confirm in Phase 0" (unsecure hook, CTRL-AP IDR, abandoned download, recover registers, nRF Util serial format, 4 KiB UICR). | — |
| R11 | TSan skip kept; pointer comment added to `tools/sanitizers/tsan.supp`. | — |

## Open findings (independent review round 2, 2026-10-12)

Review of 7f371c6 + the round-1 fixes (debug 535/535). No new high findings. Decided 2026-10-12: fix all R12–R17 (R12 incl. the packaging rule in plan 0005);
finding n = R(11+n).

| # | Severity | Finding | Suggested fix |
|---|---|---|---|
| R12 | medium | R8 incomplete: `HIO_TEST_OVERRIDES` defaults to `HIO_BUILD_TESTS`, which the `base` preset turns ON for every `*-release` preset, so release builds honour `HIO_CATALOG_URL`; a stale cache keeps it ON after `-DHIO_BUILD_TESTS=OFF`. | Default OFF; enable explicitly in test/CI presets; `FATAL_ERROR` for Release without an explicit opt-in; packaging rule in plan 0005. |
| R13 | medium | After `waitForOperation` gives up (`cancelGrace`), `close()` joins the worker (CLI exit not bounded) and discards the queued `OperationFinishedEvent`: a successful recover/flash or a serious error (e.g. R5 PIB changed) is reported as `cancelled`, exit 130. | `close()` drains remaining events after `join()` and uses a real `OperationFinishedEvent` before synthesizing Cancelled; `runOperation` prefers that result. |
| R14 | low (plausible) | An operation started after the worker died is never finished (`internal.busy` forever, wait hangs); a full event queue can drop the death event. | Remember worker death; `startOperation` refuses with `internal.unexpected`. |
| R15 | low | The masked CTRL-AP IDR check (`0x0FFFFFFF`) can't tell nRF52 from nRF91 (values differ only in bits 31:28); the AP index is the real discriminator; the fake library never models absent APs reading 0. | Document AP index as discriminator (or compare per-family IDR); fake scenario with absent APs reading 0; real IDRs in Phase 0. |
| R16 | low | Abandoning a download by closing the probe (R3) is unverified: if `JLINKARM_Close` flushes the buffer, a partial image is programmed. | Explicit Phase 0 item: write N chunks, abandon, read back. |
| R17 | low | R5: a failed PIB read after the erase yields a generic connect error; the user can't tell whether the PIB survived. | Wrap as `target.erase_failed` "PIB could not be checked after the erase"; log the before-bytes. |

**Fixes of review round 2 (2026-10-12, uncommitted on top of 7f371c6)**

| # | Fix | Tests |
|---|---|---|
| R12 | `HIO_TEST_OVERRIDES` defaults OFF; only the Debug presets (debug, asan, tsan, coverage, tidy; Linux, macOS, Windows - the ones CI runs ctest with) set it ON. Configure fails (`FATAL_ERROR`) for Release/RelWithDebInfo/MinSizeRel with it ON - also from a stale cache - unless `HIO_ALLOW_TEST_OVERRIDES_IN_RELEASE=ON`. Rule added to `plan/0005-build-ci-release.md`. The CLI catalog e2e tests run in the Debug presets and skip in release ones. | configure checks: `linux-release` → OFF; `linux-release -DHIO_TEST_OVERRIDES=ON` → FATAL_ERROR; with the opt-in → configures |
| R13 | `close()` takes a real `OperationFinishedEvent` of the running operation from the queue after `join()` before synthesizing `cancelled`; `DeviceSession::finishedResult(id)` returns it; the CLI reports it after `close()` (real success / real error with its exit code). Documented: `cancelGrace` bounds only the wait, `close()` still joins the current step; a second Ctrl+C force-exits. | `Operations.CloseKeepsTheRealResultAfterTheWaitGaveUp` |
| R14 | The worker sets `workerFailed` before pushing its death event (so a full queue cannot hide it); `startOperation` then refuses with `internal.unexpected`; `close()` ends a running operation with `internal.unexpected`. | `Operations.WaitEndsWhenTheWorkerDies` (extended) |
| R15 | Code, profile and plan say that the masked IDR only proves a Nordic CTRL-AP and the AP index is the family discriminator; test renamed (`Programmer.CtrlApAtTheOtherFamilysIndexNamesThatFamily`); fake option `absentApsReadZero`; a connect failure is mapped through `identifyTarget` (protected or wrong family). | `FakeLibrarySession.AbsentApsReadingZeroAreTheWrongConnector` |
| R16 | Phase 0 item 0.10 (write N chunks, close without `EndDownload`, reopen, read back unchanged). Result 2026-10-16: the close programs the buffer → ADR-0109. | — |
| R17 | A PIB that cannot be read after the code erase → `target.erase_failed` "The Product Information Block of the %1 MCU could not be checked after the erase." (cs translated), the old PIB bytes in the log and `detail`. | `Workflow.PibUnreadableAfterTheCodeEraseFails` |

## Open findings (independent review round 3, 2026-10-12)

Review of 7f371c6 + the round-1/2 fixes: no new high or medium findings. Decided 2026-10-12: fix R18–R20, then commit M2; no further review round before Phase 0.

| # | Severity | Finding | Suggested fix |
|---|---|---|---|
| R18 | low | R12 guard bypassable by a manual configure: case-sensitive `MATCHES` (`-DCMAKE_BUILD_TYPE=release`), multi-config generators (empty `CMAKE_BUILD_TYPE`); `HIO_ALLOW_TEST_OVERRIDES_IN_RELEASE` stays in the cache. Presets/CI unaffected (all single-config Ninja). | `string(TOUPPER)`; per-config define `$<$<CONFIG:Debug>:…>` or refuse multi-config; non-sticky opt-in (or a warning on every configure). |
| R19 | low | R17 error keeps the default suggestion "…try again": a second erase reads the already wiped PIB as "before" and reports success. | Own suggestion: do not erase again; run `pib read`, compare with the logged bytes, contact support. |
| R20 | note | A flash cancelled late (after the wait's grace, session stopping) skips `reset(Go)` and leaves the CPU halted. | Do `reset(Go)` for a cancelled halted operation even when the session is stopping. |

**Fixes of review round 3 (2026-10-12, uncommitted on top of 7f371c6)**

| # | Fix | Tests |
|---|---|---|
| R18 | The build type is compared case-insensitively (`string(TOUPPER)`); the define is generated per configuration (`$<$<NOT:$<CONFIG:Release,RelWithDebInfo,MinSizeRel>>:HIO_TEST_OVERRIDES>`, used by the targets as `${HIO_TEST_OVERRIDES_DEFINE}`), so multi-config generators never put it into a release configuration (chosen over refusing multi-config: no generator is excluded); `HIO_ALLOW_TEST_OVERRIDES_IN_RELEASE` is read once, warned about and removed from the cache (not sticky). | configure checks: `linux-release` → OFF; `-DCMAKE_BUILD_TYPE=release -DHIO_TEST_OVERRIDES=ON` → FATAL_ERROR; opt-in works once and the next configure fails again; Ninja Multi-Config: no define in Release |
| R19 | "PIB could not be checked after the erase" has its own suggestion: do not erase again, run `hio <profile> <target> pib read`, compare with the bytes in Details, contact HARDWARIO support if they differ (cs translated). | `Workflow.PibUnreadableAfterTheCodeEraseFails` (suggestion asserted) |
| R20 | A cancelled operation that left the CPU halted is reset with go also when the session is stopping. | `Operations.CloseDuringVerifyResetsTheCpuWithGo` |
