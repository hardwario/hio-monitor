# 0003 — M2 Flashing: J-Link programmer, firmware images and catalog, nRF Util modem update, CLI

- **Status:** Draft
- **Milestone:** M2
- **Author:** Claude (with Matej Strnad), 2026-10-09
- **Related ADRs:** 0006, 0007, 0015, 0016, 0017, 0022, 0024, 0026, 0027, 0028–0032, 0034, 0035–0042, 0057, 0062
- **Depends on:** 0001 (M0 skeleton), 0002 (M1 J-Link & RTT: library loading, `JLinkProbe`, `DeviceSession`, fake library)

> **Amended by ADR-0057 and ADR-0062 (M3 planning, applied in this text):** every operation carries its
> target MCU. The `Operation*` types are concrete, an LTE bundle is one operation, and reset is an operation.
> Cancel reports the M0 code `cancelled`. Operations also work on sessions opened with `attachRtt = false`.

> **Pending amendment from M4 (ADR-0069, specified in [0007](0007-m4-wizard-framework-diagnostics.md) § Amendment;
> applied when 0007 is approved — then this plan needs re-approval):** `OperationKind::Diagnose`, `DiagnoseStep`,
> `OperationResult::checks`, the `diagnoseStep` signal.


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
Using J-Link 801234567 (J-Link EDU Mini), library 8.10a
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
  enum class OperationKind { Flash, EraseCode, EraseAll, Recover, Modem, LteBundle, Reset, ReadMemory };
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
  struct OperationResult {
      OperationKind kind;
      Status status;                                 // error incl. code/wizard; cancel → M0 code `cancelled`
      std::chrono::milliseconds duration{};
      std::optional<ChipInfo> chip;                  // identified part (shown by GUI/CLI)
      std::vector<std::byte> data;                   // ReadMemory only (≤ 64 KiB)
  };
  struct FlashOptions { bool halt = false; bool allowUicr = false; };

  [[nodiscard]] Result<OperationId> flash(std::string_view targetId, std::shared_ptr<const firmware::FirmwareImage>, FlashOptions);
  [[nodiscard]] Result<OperationId> flashLteBundle(std::string_view targetId, std::shared_ptr<const firmware::LteBundle>, FlashOptions);
  [[nodiscard]] Result<OperationId> erase(std::string_view targetId, EraseScope);
  [[nodiscard]] Result<OperationId> recover(std::string_view targetId);
  [[nodiscard]] Result<OperationId> updateModem(std::string_view targetId, std::filesystem::path modemZip);  // suspend → nRF Util → resume
  [[nodiscard]] Result<OperationId> reset(std::string_view targetId, ResetMode);   // replaces M1 `Status reset(bool)`
  [[nodiscard]] Result<OperationId> readMemory(std::string_view targetId, std::uint32_t address, std::size_t size);  // ≤ 64 KiB
  [[nodiscard]] Status cancel(OperationId);
  signals:
      void operationProgress(hio::device::OperationId, const hio::device::OperationProgress&);
      void operationFinished(hio::device::OperationId, const hio::device::OperationResult&);
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
5 min, erase-all 60 s, recover 60 s, memory op 5 s · nRF Util output 1 MiB per stream, version probe 10 s,
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
| New dependency miniz (MIT) | approve / reject | _pending (approval of this plan)_ | ADR-0041 |
| Operations on another MCU than the console's (M3 review) | operation carries target / retarget tab / second session | operation carries target | ADR-0057 |
| Operation types, LTE bundle, reset (M3 review) | one bundle operation / caller sequences; reset as operation / keep both | concrete types; bundle = one operation; reset = operation; cancel = `cancelled` | ADR-0062 |
| Minor defaults: flashing as a session operation with RTT paused and re-attached; nRF Util only while suspended; progress phases; FICR family check and LTE device-name selection; full read-back verify; group/subcommand `-n` handling; existing path wins over ID; `pib`/`uicr read` app-only; flash log in the console file + `hio.flash`; all bounds and timeouts above; `internal.busy` | accept / change | _pending_ | — |

## Implementation steps

Phase 0 — hardware spike (go/no-go; results recorded in this plan before Phase A starts)
- [ ] 0.1 Throw-away tool (not merged) on M1's `JLinkLibrary`; CHESTER with nRF52840 + nRF9151 (+ nRF9160 if available), J-Link 8.x on Linux.
- [ ] 0.2 Flash-loader download of a real CHESTER app hex keeps UICR and the PIB byte-for-byte (nRF52840, nRF9151).
- [ ] 0.3 `JLINK_EraseChip` erases flash + UICR on both; code-only erase method chosen.
- [ ] 0.4 CTRL-AP ERASEALL recovers a protected nRF52840 and nRF9151; nRF91 stays debuggable after the disable-AP-protect image + reset.
- [ ] 0.5 FICR part read works on both; LTE connect name strategy chosen.
- [ ] 0.5b Cross-target switching (ADR-0057): with the J-Link on one CHESTER connector, can the probe connect to
  the other MCU (app ↔ lte) without re-plugging? Record the wiring per CHESTER variant. If not, operations for the
  other target fail early with `target.wrong_family` (connector hint), and the GUI Flash page says so before
  starting → update ADR-0057 with the result.
- [ ] 0.6 Programming speed within 20 % of nrfjprog on the same image.
- [ ] 0.7 `nrfutil device program` of a modem zip with our session suspended (library copy kept loaded, probe closed); JSON progress format captured; modem zip markers recorded; tested versions → ADR-0042.
- [ ] 0.8 Go/no-go recorded; any failed criterion → stop, new ADR, update this plan, ask the user.

Phase A — `libs/firmware`
- [ ] A1 `FirmwareImage`, `parseIntelHex` + tests (corpus incl. CHESTER build outputs)
- [ ] A2 CPM: miniz (version + SHA-256 below); `ZipReader` + limit/zip-slip tests
- [ ] A3 `classifyLteInput` + tests
- [ ] A4 `FirmwareCache` (LRU, atomic) + tests; `FirmwareCatalog` + tests against a local `QTcpServer` HTTP fixture (200, 404, 500, timeout, oversize, invalid hex, cancel)

Phase B — J-Link programmer
- [ ] B1 ABI additions + `IJLinkApi` methods + fake-library symbols
- [ ] B2 `JLinkProgrammer` (identify, program, verify, eraseCode, eraseAll, recover, readMemory, reset) + GMock tests

Phase C — device
- [ ] C1 Profile extensions (`chester.cpp` app + lte), LTE connect strategy + tests
- [ ] C2 `FlashWorkflow` (range check, UICR rules, sequence), `EraseWorkflow`, `RecoverWorkflow` + embedded image, `LteBundleWorkflow`, `TargetSwitch` (ADR-0057) + tests
- [ ] C3 `decodePib` + test vectors (valid, erased, bad CRC/signature/size/version)
- [ ] C4 `DeviceSession` operations with `targetId` and the ADR-0062 types (incl. `reset` replacing M1's, `flashLteBundle`), `attachRtt`, `internal.busy`, RTT pause/re-attach + TSan tests
- [ ] C5 Error codes added to the `libs/core` table (+ `cs` translations)

Phase D — nRF Util
- [ ] D1 `NrfUtilLocator`, version checks (ADR-0042) + tests
- [ ] D2 `NrfUtilModemUpdater` + `tests/fake_nrfutil` (progress, failure, hang → timeout, cancel, output flood) + `ModemWorkflow`

Phase E — fake J-Link library
- [ ] E1 Flash memory model (flash/UICR/FICR per scenario, sector erase semantics, protection state, ERASEALL, write faults, verify mismatch injection)
- [ ] E2 Integration tests: flash with RTT attached (pause/re-attach), two sessions flashing concurrently

Phase F — CLI
- [ ] F1 `progress_ui` (TTY bars on stderr / plain lines / NDJSON), `confirm` (TTY prompt / `--yes`)
- [ ] F2 `chester app flash|erase|reset|recover`, `--nrfutil`, group/subcommand `-n`
- [ ] F3 `chester app pib read`, `uicr read`
- [ ] F4 `chester lte flash|erase|reset|recover`
- [ ] F5 CLI end-to-end tests (QProcess + fake library + fake nRF Util + local HTTP fixture)

Phase G — hardware and docs
- [ ] G1 HIL tests (opt-in `HIO_HIL=1`): flash by file and ID, verify, erase, erase --all, recover (app + lte), LTE bundle, PIB read
- [ ] G2 Manual checks on Linux, Windows, macOS (incl. nRF Util discovery on each)
- [ ] G3 Update `plan/0006-cli.md` statuses, `plan/0002-legacy-inventory.md` parity rows, roadmap M2 → Done

| Dependency | Version | SHA-256 |
|---|---|---|
| miniz | _tbd_ | _tbd_ |

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
