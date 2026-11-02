# 0028 — GUI feedback round 2: log file names, Save view, copy feedback, Batch help, Flash bar

- **Status:** Done (2026-11-02; hardware checks open). Approved 2026-11-02 by Matej Strnad with the recommended option of every decision D1–D22
- **Milestone:** GUI feedback round 2 (follow-up of [0021](0021-gui-feedback-round-1.md))
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0200 … ADR-0204 (new, Accepted); amended (amendment notes accepted): ADR-0128, ADR-0055,
  ADR-0174, ADR-0166; context: ADR-0003, ADR-0027, ADR-0048, ADR-0058, ADR-0083, ADR-0104, ADR-0138, ADR-0156,
  ADR-0167, ADR-0170, ADR-0175, ADR-0184, ADR-0186
- **Depends on:** [0021](0021-gui-feedback-round-1.md) (device info via `info show`, `ShellQuery`), [0025](0025-security-fixes-round-1.md)
  (console file `0600`, redaction, PlainText tooltips). Base branch `v2/security-fixes` (f8c32da); work branch
  `v2/gui-feedback-2` (worktree `../hio-monitor-0028`).
- **Numbering note:** plans 0026 and 0027 are taken by other sessions; ADR-0189 … ADR-0199 are reserved by another
  session, so this plan's ADRs start at ADR-0200. Plan 0029 (customer documentation on docs.hardwario.com) is a
  separate future plan and is only referenced here.
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md)

## Context

Customer feedback on the integrated GUI (2026-11, originally in Czech, translated):

| # | Note | Area |
|---|---|---|
| F1 | The console log file name should contain the firmware name and version: with one J-Link and many devices in a row, the files cannot be told apart. | Console file |
| F2 | "Save view…" ("Uložit výpis…") does nothing: the name is empty, and on Linux pressing Save with a folder selected just opens the folder. | Console |
| F3 | Settings → Console: in Czech "(odešle „help“)" there is a gap between "š" and "l" and the "l" looks reddish (dark theme screenshot). | Settings / rendering |
| F4 | "Batch…" has no explanation of what it does. | Console |
| F5 | Copying gives no feedback (the old app showed "Copied to clipboard"). | Whole app |
| F6 | On the Flash page the Flash button sits at the far right edge; on a large monitor it is hard to find. | Flash page |
| F7 | Customer documentation. | Out of scope → plan 0029 |

Decisions the user confirmed (2026-11-02) are in ADR-0200 … ADR-0204 and in § Decisions; the remaining rows were
decided on approval (2026-11-02): the recommended option of each.

### Facts checked in the code (base f8c32da)

- Console file name: `ConsoleFileSink::open` (`libs/device/src/console_file_sink.cpp` ≈ 201–231) builds
  `<profile>-<target>-<yyyyMMdd-HHmmss UTC>-<sn>.log` (J-Link) and `<profile>-ble-<stamp>-<sanitised id>.log` (BLE).
  The file is opened before any device output: J-Link `SessionWorker::openSink` (`session_worker.cpp` ≈ 238–253,
  called at connection start / probe-only promotion), BLE `BleConsoleHandler::openSink` (`ble_session.cpp` ≈ 167–187,
  on the first `onConnecting`). Retention `prune()` parses names with `sessionKeyOf` (≈ 134–169, takes the **first**
  `-YYYYMMDD-HHMMSS-` and requires the BLE prefix to **end with `-ble`**) and checks `ProbeLock` / `BleLock` owners.
  `OpenFiles` is keyed by path. `.partN` continuation at 10 MiB; explicit `--console-file` is rotated to `P.1`.
  Path changes reach the façade as `ConsoleFileChanged` / `BleConsoleFileChanged` (`reportConsoleFile`, compared per
  flush) → `DeviceSession::consoleFile()`.
- `info show` is queried on every Connected: GUI `DeviceTabViewModel::queryDeviceInfo` (owner thread,
  `ShellQueryQueue`), CLI `ConsoleLearning::queryInfo` (`app/cli/src/console_learning.cpp`) — **interactive
  `chester app console` only**. `chester app info` runs its own query. MCP sessions do not query.
- **Contradiction found:** real CHESTER firmware prints `firmware name: CHESTER Clime` and `firmware version: v3.0.0`
  (chester-sdk `subsys/ctr_info/ctr_info_shell.c`, unchanged since the firmware name was added in 2022; version from
  `git describe`, e.g. `v3.0.0-5-gabc1234`). `parseInfoShow` (`libs/device/src/device_info.cpp`) accepts only keys
  of `[A-Za-z0-9_-]` and looks up `fw-name` / `fw-version`, so on real hardware `hasFirmware()` is false and the
  0021 "In the device" feature, the command-tree cache key and anything built on it get no identity. All test
  fixtures (`jlink_e2e_test.cpp`, `console_learning_test.cpp`, the screenshot kit) use the invented `fw-name:`
  format. Fixing the parser is a prerequisite of F1 (decision D2).
- No code parses the Zephyr boot banner today.
- Save view: `ConsolePage.qml` ≈ 147–162 (`FileDialog` without `currentFolder` / `selectedFile`),
  `ConsoleViewModel::saveView` (`console_view_model.cpp` ≈ 659–682) accepts a non-local URL as `file.toString()`.
  Working pattern: `ReportDialog.qml` ≈ 176–185 + `ReportViewModel::defaultFolder()` (Documents) / `save()` (refuses
  non-local URLs **silently**).
- F3: `ConsoleSection.qml` ≈ 66–72, `i18n/hwio_cs.ts` ≈ 689–691: the translation is clean (no `&`, no markup).
  "odešle" also appears in the ShellInput placeholder (`hwio_cs.ts` ≈ 1559). UI font: bundled **static** Inter 4.1
  TTFs (`extras/ttf`, not the variable font; README in `app/gui/assets/fonts/`), registered in `fonts.cpp`. No text
  render type is set anywhere → Qt Quick default (distance-field `Text.QtRendering`).
- Batch: `BatchRunner` (`app/gui/src/workers/batch_runner.h`) — 200 ms interval, skips empty and `#` lines, UTF-8,
  `maxFileBytes` 1 MiB, `maxCommands` 10 000, `CommandHistory::maxEntryBytes` 4 096 per line. Batch is GUI-only
  (the CLI reads commands from stdin). `HToolButton`'s tooltip is only "label (shortcut)"; `HToolTip` wraps but has
  no maximum width.
- Help → Documentation already opens `https://docs.hardwario.com/chester/platform-connectivity/hardwario-monitor/`
  (`AppViewModel::documentationUrl`, `app_view_model.cpp` ≈ 1017–1022). ADR-0138's central `docsUrl(topic)` table
  does **not** exist yet.
- Copy sites: console `copySelection` (menu `ConsolePage.qml` ≈ 177–181; Ctrl+C `ConsoleView.qml` ≈ 328–329 →
  `copyRequested`) writes the clipboard itself and toasts only on truncation; `ErrorPanel.qml` ≈ 191–193 and
  `WarningBanner.qml` ≈ 144–146 call `AppViewModel.copyToClipboard` (no toast); `AboutSection.qml` ≈ 39–41
  (`SettingsViewModel::copyAbout` → "Copied."), ≈ 138–142 and ≈ 161–165 (QML calls `showToast("Copied.")`);
  `CommandBlock` → `WizardViewModel::copyText` → `copied()` → AppViewModel toast "Copied."; `ReportViewModel::
  copySummary` → "Summary copied.". The Toast column lives in the window content (`Main.qml`, `z: 20`); the wizard and
  report dialogs are modal `Dialog`s in the overlay layer, so a toast raised while they are open is probably
  covered by the dialog's dim (decision D21).
- Flash page: form column `Math.min(880, scroll.availableWidth - 2 * spacingXl)` centred in the `ScrollView`
  (`FlashPage.qml` ≈ 77–80); `FlashBar` (`FlashBar.qml` ≈ 24–68) is a full-width `RowLayout` with 24 px margins.
  `assets/icons/flash.svg` exists; `HButton` has `iconName`.

## Goals / Non-goals

**Goals**
- F1: console files named after the firmware once it is known, for J-Link sessions in GUI and CLI (BLE per D9);
  `info show` parsed correctly on real CHESTER firmware.
- F2: Save view with a prefilled name and a remembered folder; never a silent no-op.
- F3: find the cause of the "š l" gap and fix it at the right level; a cheap translation lint (pending D15).
- F4: an explanatory tooltip and a documentation link for Batch.
- F5: one "Copied to clipboard." toast for every copy action.
- F6: Flash button with the bolt icon, aligned with the form column.
- EN + CS for every new or changed string; CLI parity statement per item.

**Non-goals**
- Customer documentation content and hosting (plan 0029; it will supersede ADR-0138's GitHub `docs/` target).
- Renaming explicit `--console-file` paths; renaming files of finished sessions.
- MCP-opened sessions (recommended out of scope, D8).
- Batch in the CLI.
- Changing the console file *content* format beyond one marker line.

## User experience

### F1 — Console file names (ADR-0200)

- Session starts → file `chester-app-20261102-101500-683012345.log` (today's name).
- `info show` answers (silent query on Connected, as today) → the file is renamed to e.g.
  `chester-app-clime-v3.0.0-20261102-101500-683012345.log` (slug per D1) and gets a marker line
  `@ Firmware: CHESTER Clime v3.0.0` (D6). Stamp and probe SN are unchanged.
- "Open log file" / "Show in folder" (console page) and `hwio chester app console` (which prints the file path at the
  end) follow the new path.
- A different firmware or device later in the same session (reset after a flash, device swapped on the same probe)
  → per D3.
- Firmware not identified (no `info show`, timeout, unknown format, no app banner) → today's name.

### F2 — Save view (ADR-0201)

- Ctrl+S / "Save view…" opens the save dialog in the last folder a view was saved to (first time: Documents) with the
  name prefilled: `chester-app-clime-v3.0.0-20261102-101500-shell.txt` / `…-log.txt` (base per D1) or, without a
  known firmware, `chester-app-20261102-101500-shell.txt`. Stamp = time of opening the dialog (D12).
- Save writes the file, toast "The view was saved to <path>." (unchanged) and remembers the folder.
- A non-local location → error panel per D13.

### F3 — "odeš le" in Settings → Console

Investigation item (Phase C). User-visible result: Czech text renders without a gap or colour fringe. No string
change.

### F4 — Batch (ADR-0203)

Hovering / focusing "Batch…" shows (EN source; plain text, max 360 px wide):

> Run a batch file (Ctrl+B)
> Sends each line of a UTF-8 text file to the device as a shell command, one line every 200 ms. Empty lines and
> lines starting with # are skipped. At most 10 000 commands, 1 MiB per file and 4 096 bytes per line.

Documentation link per D17 (recommended: an info icon button right of "Batch…", tooltip "Batch files —
documentation", opens the URL from the central table, D18).

### F5 — Copy feedback (ADR-0202)

Every copy action shows the toast **"Copied to clipboard."**; nothing happens (no toast) when there is nothing to
copy. A copy cut by the console copy limit shows (D19) "Copied the first %n lines to the clipboard (copy limit)."
instead.

### F6 — Flash bar (ADR-0204)

The sticky bar keeps its full-width background and top border; its row (✓ icon, summary or reason, "Ctrl+Enter",
**[⚡ Flash]**) is laid out in the same centred column (≤ 880 px) as the form, so the button sits under the right edge
of the form. Catalog ID row unchanged.

## CLI

| Item | CLI |
|---|---|
| F1 | `hwio chester app console`: the generated console file is renamed the same way once `info show` answers (D7: the CLI uses the same backend watcher as the GUI; non-interactive mode too when a generated file is written). U3 (review round 1): at the end the console prints `Console file: <final path>` on stderr and `--json` adds a `consoleFile` event; the same for `ble console`. `hwio ble console` per D9. `hwio chester app command` (one-shot) and `hwio mcp` sessions: names unchanged (D8). `--console-file FILE`: never renamed. `hwio chester app info [--json]`: shows the real CHESTER fields after the parser fix (D2; JSON keys per D2). |
| F2 | none (GUI-only) |
| F3 | none (the translation lint, if accepted, is a CTest check) |
| F4 | none (Batch is GUI-only) |
| F5 | none |
| F6 | none |

Exit codes and `--json` shapes unchanged except the `app info` field keys (D2).

## Design

### F1 — Firmware identity → console file name

**Data (libs/device, new `firmware_identity.h`):**

```cpp
struct FirmwareIdentity {          // bounded: name ≤ 64, version ≤ 48 bytes (chester-sdk buffer sizes), serial ≤ 32
    std::string name;              // e.g. "CHESTER Clime"
    std::string version;           // e.g. "v3.0.0" or "v3.0.0-5-gabc1234"
    std::string deviceSerial;      // "serial number" of info show; "" when unknown (used only for D3, never in names)
    friend bool operator==(const FirmwareIdentity&, const FirmwareIdentity&) = default;
};
[[nodiscard]] std::optional<FirmwareIdentity> identityFromInfo(const DeviceInfo& info);   // needs name + version
[[nodiscard]] std::optional<FirmwareIdentity> identityFromBanner(std::string_view line);  // D10
// The file-name base shared by console files and Save view (D1):
[[nodiscard]] std::string consoleFileBase(std::string_view profileId, std::string_view targetId /* "ble" for BLE */,
                                          const std::optional<FirmwareIdentity>& identity);
[[nodiscard]] std::string fileSlug(std::string_view text, std::size_t maxBytes);     // [a-z0-9-], see below
[[nodiscard]] std::string versionSlug(std::string_view text, std::size_t maxBytes);
```

- `fileSlug`: ASCII letters lower-cased, digits kept, every other run of bytes (spaces, punctuation, non-ASCII) → one
  `-`, trimmed of `-`, cut to 40 bytes; empty → no identity. `versionSlug`: `[0-9A-Za-z+_-]` kept, `.` kept only when
  followed by a digit (so `.part` can never appear), other runs → `-`, cut to 32 bytes; empty → no identity.
  Version kept as printed (no `v` added).
- `consoleFileBase` (D1 recommended): `<profile>-<target>` + (identity ? `-<slug>-<version>` : ""), where `slug` is
  `fileSlug(name)` with a leading `<profile>-` removed when present ("CHESTER Clime" → `clime`;
  "hio-chester-push-fm" → `hio-chester-push-fm`). Result for J-Link: `chester-app-clime-v3.0.0`; BLE:
  `chester-ble-clime-v3.0.0`.

**`parseInfoShow` fix (D2 recommended):** keys may contain single spaces (`[A-Za-z0-9 _-]`, no leading/trailing
space, ≤ 64 bytes); the stored key is normalised (lower-case, spaces → `-`): `firmware name` → `firmware-name`.
`DeviceInfo::fwName()` returns `fw-name`, else `firmware-name`; `fwVersion()` likewise (`fw-version` /
`firmware-version`); `deviceSerial()` returns `serial-number`. The `(unset)` value of chester-sdk counts as missing.
Lines with more than one colon-separated word group still parse at the first `:`. Bounds unchanged (64 fields,
256-byte values). Fixtures gain the real CHESTER output (all 11 lines of `info show`).

**Who asks (D7 recommended): `DeviceInfoWatcher` (libs/device, new, owner thread, QObject).** Moves the logic that is
duplicated today in `DeviceTabViewModel::queryDeviceInfo` and `cli::ConsoleLearning::queryInfo` into the backend:

```cpp
class DeviceInfoWatcher : public QObject {   // owner thread of the session
public:
    DeviceInfoWatcher(ConsoleSession& session, ShellQueryQueue& queries, QObject* parent = nullptr);
    [[nodiscard]] const DeviceInfo& info() const noexcept;        // of the current attach, {} otherwise
signals:
    void infoChanged();                                            // GUI: deviceInfoChanged; CLI: learner start
};
```

On `connectionChanged(Connected)` it queues `info show` (as today: only an answer of this attach of this session
counts, review P3/P7); on the answer it updates `info()`, emits `infoChanged()` and calls
`session.setFirmwareIdentity(identityFromInfo(info), event.attach)`. On leaving Connected it clears `info()` but does
**not** clear the session's identity (the file keeps its name). The GUI tab and the CLI console both own one;
`DeviceTabViewModel::deviceInfo()` and the CLI's `CommandTreeLearner` start read from it. The "Now in the device"
re-query window of 0021 stays in the GUI (it calls `watcher.refresh()`).

**Session API (owner thread → session thread):**

- `ConsoleSession::setFirmwareIdentity(std::optional<FirmwareIdentity>, std::uint64_t attach)` — new pure virtual,
  `[[nodiscard]] Status`; errors `internal.queue_full` (logged by the watcher at warning; the name stays, no user
  message).
- `ConnectionEvent` gains `std::uint64_t attach = 0` — set by the J-Link worker / BLE handler on every Connected
  (monotonic per session). The session thread ignores an identity whose `attach` is not its current one (a stale
  answer after a reconnect to another device). Events can be dropped (ADR-0031), so the id travels in the event
  instead of being counted on the owner side.
- J-Link: `DeviceSession` posts `SetFirmwareIdentityCommand{identity, attach}` to the existing bounded command queue
  (like `SetRttAddressCommand`); the worker calls `m_sink->adoptIdentity(...)` and then `reportConsoleFile()`.
- BLE (D9): `BleSession` posts `ble::SetFirmwareIdentity{client, identity, attach}` to the BLE host; the handler does
  the same on the BLE thread.
- Banner fallback (D10): the J-Link worker / BLE handler feed each assembled shell **and** log line of the first
  5 s after Connected (at most 200 lines) to `identityFromBanner`; a hit is kept as the attach's *banner identity*.
  An `info show` identity always wins; the banner identity is applied only when the watcher reports "no identity"
  for that attach (`setFirmwareIdentity(nullopt, attach)` after an answer without firmware fields, a timeout or a
  refusal). `identityFromBanner` accepts only
  `*** Booting <name> v<semver…> ***` where `<name>` is not `Zephyr OS`, `Zephyr OS build`, `nRF Connect SDK` or
  `MCUboot` (case-insensitive), name ≤ 64 bytes; `*** Using Zephyr OS … ***` is ignored. Since a standard NCS image
  prints only the SDK banner, CHESTER firmware normally never hits it (HIL records what CHESTER prints).

**`ConsoleFileSink::adoptIdentity(const std::optional<FirmwareIdentity>&, time_point now)` (sink thread):**

- Explicit file (`--console-file`) → no-op. `nullopt` → no-op (an unidentified attach keeps the current name; the
  worker / BLE handler passes the attach's banner identity instead when it has one, D10).
- First identity of the sink (D4 recommended: rename): flush the buffer, close the file, compute the new base
  `<dir>/<consoleFileBase(...)>-<same stamp>-<same sn|id>.log`; rename every existing part of the session
  (`base.log`, `base.part2.log` … current, D5) to the new base with the same part numbers; update `OpenFiles` (remove
  old path, add new) and `m_base` / `m_current`; reopen the current file (append). Then write the marker
  `@ Firmware: <name> <version>` (D6; device serial never written). A rename failure (e.g. Windows: another program
  holds the file without `FILE_SHARE_DELETE`) → keep the old name for that part, continue writing, `qCInfo` once,
  no user-visible warning (D11). If the target name exists already (same second, same probe, same firmware) →
  append `-2` … `-9` before `.log`, else keep the old name.
- Same identity again (reconnect of the same device) → no-op.
- Different identity (D3 recommended: new file): write `@ Firmware changed to <name> <version>; continued in
  <new file name>`, close, open a **new** file with a new stamp (`now`, UTC) and the new identity's name, header as at
  open plus `@ Continues <old file name>`, then prune. The `.partN` counter restarts at 1.
- Retention: the new file of an identity change is "the newest file of the session key" for the cross-process lock
  rule (R45) — no change needed; the rename keeps mtime and key.

**`sessionKeyOf` (retention) rewrite:** strip `.partN`; J-Link: the name ends with `-<1..10 digits>` and the 16
characters before it are `YYYYMMDD-HHMMSS` preceded by `-` → `ProbeKey`, provided the second dash-separated token is
not `ble`; BLE: the second token is `ble` and the **first** `-YYYYMMDD-HHMMSS-` after it ends the prefix, the rest is
the sanitised id (ids may contain dashes; a slug or version can contain a stamp-like sequence only if a firmware
version does so — tolerated, the key would be wrong only for such a BLE file, which then counts as "not ours" for the
live rule). Old names parse unchanged (tests keep the old cases). Profile ids never contain `-` (static assert over
the profile registry).

**Header line:** unchanged at open (the identity is unknown then); the marker line documents it (D6).

**Threading:** no new threads. The watcher and `setFirmwareIdentity` run on the owner thread; rename, marker and
prune on the sink's thread (J-Link worker, BLE thread) — the only threads that touch the sink (ADR-0128). Cross-thread
only via the existing bounded command queue / BLE host post.

**Errors:** no new error codes. Rename/open failures of the reopened file use the existing `io.write_failed` path
(`sinkResult` → session continues without the file, warning shown as today).

**Bounds:** identity strings as above; banner scan ≤ 200 lines / 5 s per attach; collision suffix ≤ 9; no new
queues.

### F2 — Save view

- `ConsoleViewModel` (GUI, owner thread):
  - `Q_INVOKABLE QUrl saveViewFolder() const` — `console/saveViewFolder` if it names an existing directory, else
    Documents (`QStandardPaths::DocumentsLocation`).
  - `Q_INVOKABLE QString saveViewFileName(int channel) const` — `consoleFileBase(profile, target, identity)` +
    `-<yyyyMMdd-HHmmss UTC now>-shell.txt` / `-log.txt`; identity = the tab's last known identity of this session
    (D14: kept after a disconnect until another identity arrives; cleared when the tab switches probe/device).
  - `saveView(channel, url)`: non-local URL → D13; on success stores the folder (`QFileInfo(path).absolutePath()`)
    in `console/saveViewFolder` (string, default empty, registered in the GUI settings key list like
    `console/logToFile`).
- QML (`ConsolePage.qml`): `openFor(channel)` sets `currentFolder = vm.saveViewFolder()` and
  `selectedFile = folder + "/" + vm.saveViewFileName(channel)` immediately before `open()` (no logic beyond
  binding; mirrors `ReportDialog`).
- Error (D13 recommended): `io.write_failed` with message "The file could not be saved.", cause "Only folders on
  this computer can be used.", suggestion "Choose a folder on this computer." (translatable); the same check and texts
  in `ReportViewModel::save` instead of its silent return.

### F3 — "odeš le" investigation

Hypotheses (to be confirmed or rejected by Phase C):
1. **Distance-field rendering** (Qt Quick default `Text.QtRendering`): glyph metrics of a precomposed `š`
   (U+0161) rendered from the distance-field cache at 13 px with fractional scaling may round the advance
   differently; a bleeding neighbour in the glyph atlas could tint the `l`.
2. **Inter 4.1 static TTF features**: `ccmp` decomposition of `š` into `s` + combining caron and `mark` positioning,
   or a `kern` pair `scaron`+`l`, could give a wrong advance in Qt's HarfBuzz path for this font.
3. **Fractional scaling / subpixel AA**: with a non-integer device pixel ratio and `NativeRendering` paths
   (Material `Switch` label), LCD subpixel antialiasing could produce the red fringe on a thin vertical stem.
4. Font fallback: if `š` were missing from the loaded face, Qt would fall back to another family with a different
   advance (unlikely — Inter covers Czech; check `QFontDatabase::writingSystems`).

Reproduction harness (Qt Quick Test + grab, offscreen and xcb): the exact string, plus `š ľ č ř ž ď ť` before `l`,
`ť`, `ď`, `h` in 12/13/18 px, weights 400/600, at DPR 1.0, 1.25, 1.5, 2.0, for `Text.QtRendering`,
`Text.NativeRendering` and `Text.CurveRendering` (Qt 6.7+), light and dark. Measure the advance of `š` via
`TextMetrics` / `QFontMetricsF::horizontalAdvance` vs the font's hmtx value (fontTools on the build machine, not at
runtime) and save the grabs as artefacts (not committed). The fix is chosen at the narrowest level that removes the
defect (D16).

#### Phase C findings (2026-11-02, implementing agent) — D16 goes back to the user

Harness: `app/gui/tests/tst_text_rendering.qml` (runs in the default `ctest` offscreen; grabs only with
`HWIO_TEXT_GRABS=<dir>`, other device pixel ratios with `QT_SCALE_FACTOR`, real platforms with
`QT_QPA_PLATFORM=xcb|wayland`). Machine: Ubuntu 26.04, Qt 6.10.2, Inter 4.1 static TTFs, fontconfig `rgba=rgb`,
`lcdfilter=default` (a usual LCD desktop setting). Zoomed grabs (13 px, dark, "odešle"; not committed unless the
user wants them, Git LFS): `plan/features/0028-f3/*.png`.

| Hypothesis | Result |
|---|---|
| 2 Inter features (`ccmp`/`kern`/GPOS) | **Rejected.** `FontMetrics.advanceWidth`: `š` = `s` (13 px: 6.859 px, 18 px: 9.5 px, also 600 weight) and `šl` = `š` + `l`, `ešl` = `eš` + `l` exactly at 12/13/18 px, 400/600 — no kerning or composition gap. (fontTools is not installed on the build machine; the hmtx comparison was replaced by these Qt measurements.) |
| 4 Font fallback | **Rejected.** `š` has the advance of Inter's `s`; the glyph is Inter's. |
| 1 Distance-field rounding | **Rejected as the cause of a gap**: the geometry is identical in all render types. |
| 3 Subpixel antialiasing | **Confirmed.** On xcb and Wayland (DPR 1, 1.25, 1.5, 2) `Text.QtRendering` (the default: Qt Quick uses *subpixel* distance-field antialiasing when the platform font settings say LCD) and `Text.NativeRendering` draw every glyph with coloured LCD fringes: 16 000–27 000 coloured pixels in the harness text rows; the thin stem of `l` gets a blue left and an orange/red right fringe on the dark background, which reads as a gap after `š` and a reddish `l` (in a zoomed screenshot even more). `Text.CurveRendering` and `QtRendering` with `QSG_DISTANCEFIELD_ANTIALIASING=gray`: 0 coloured pixels, no visible gap. Offscreen (the CI platform) has no subpixel AA at all, which is why the tests never saw it. |

Conclusion: the defect is not specific to "odešle" or to the Czech text: every text of the app is drawn with LCD
subpixel antialiasing when the desktop asks for it; on a panel whose subpixel order differs from the setting
(BGR, rotated, scaled remote/virtual display) or in a zoomed screenshot the fringes look like gaps and colour. The
string itself is clean (C4 lint: 0 problems).

Fix levels (D16):

| Level | Fix | Effect |
|---|---|---|
| Per component | `renderType: Text.CurveRendering` on that one Switch label | Removes it only there; every other label keeps the fringes (inconsistent). |
| Font features | — | No font feature controls subpixel AA; `QFont::NoSubpixelAntialias` is a style strategy of the application font and is not used by `Qt.font()` in `Theme.qml` (would have to become a global font change). |
| **Global** | grayscale text AA for the whole app: `QSG_DISTANCEFIELD_ANTIALIASING=gray` set by `main()` before the window, or `QQuickWindow::setTextRenderType(CurveRendering)` | Removes it everywhere; text looks slightly softer on LCD panels with the right setting. |

Only a global change removes the defect, so per D16 the findings went back to the user (C3 was not started then).

**Decision U1 (2026-11-02, ADR-0205):** grayscale distance-field antialiasing for the whole app. **C3 result:**
`applyTextAntialiasing()` (`app/gui/src/fonts.cpp`) sets `QSG_DISTANCEFIELD_ANTIALIASING=gray` in `run()` before the
`QGuiApplication`, keeping a value already in the environment. With it the harness counts **0 coloured pixels** in the
`QtRendering` and `CurveRendering` rows on xcb and Wayland at DPR 1, 1.25, 1.5 and 2 (before: 16 000–27 000); the
harness asserts this whenever the variable is `gray` (and always offscreen). Before/after crops:
`plan/features/0028-f3/xcb-dpr1-dark-switch-label-{before,after}.png`, `xcb-dpr1-dark-qtrendering-after.png`,
`settings-console-cs-{dark,light}-xcb-{before,after}.png` (Settings → Console, Czech, from the
../screenshots-demo kit on xcb: `out-0028-xcb-before/`, `out-0028-xcb-after/`).

### F4 — Batch help

- `ConsoleViewModel::batchHelpText` (`Q_PROPERTY QString`, retranslated on language change) built with `tr()` from
  `BatchRunner::defaultInterval`, `maxCommands`, `maxFileBytes`, `CommandHistory::maxEntryBytes` (locale-formatted
  numbers).
- `HToolButton` gains `property string description` (default ""): the tooltip shows
  `shortcuts.tooltip(text, shortcutId)` and, when set, the description on the next lines; `Accessible.description`
  = description. `HToolTip` gains a maximum content width of 360 px (wrapping already on).
- Docs URL (D18 recommended): new `libs/core` `docs_urls.h`: `enum class DocsTopic { Monitor, ConsoleBatch };
  [[nodiscard]] QUrl docsUrl(DocsTopic, QStringView language);` — `Monitor` = today's
  `AppViewModel::documentationUrl()` (which then calls it), `ConsoleBatch` = the same page until plan 0029 defines the
  anchor and the Czech variants. This is the table ADR-0138 called for; plan 0029 changes only the table.
- Link presentation per D17 (recommended: `HToolButton { iconName: "info"; objectName: "batchHelpButton" }` right of
  Batch, tooltip "Batch files — documentation", `onClicked: page.vm.openBatchDocs()` → `QDesktopServices::openUrl`).

### F5 — Copy feedback

- `AppViewModel::copyToClipboard(const QString& text, const QString& toast = {})`: empty text → nothing; otherwise
  clipboard + `showToast(toast.isEmpty() ? tr("Copied to clipboard.") : toast)`. The only function in the GUI that
  writes the clipboard (a CTest grep check keeps `QGuiApplication::clipboard()` out of other files, except tests).
- Hooks: `DeviceTabViewModel` context gains `copyToClipboard` (like `showToast`); `ReportViewModel` and
  `WizardViewModel` hooks get a `copy` function instead of their own clipboard code (`WizardViewModel::copied()`
  signal removed).
- Sites: `ConsoleViewModel::copySelection` (0 lines → nothing; truncated → D19 text as the toast);
  `ErrorPanel` / `WarningBanner` unchanged in QML (they call `AppViewModel.copyToClipboard`); `AboutSection` QML
  drops its `showToast("Copied.")` calls; `SettingsViewModel::copyAbout` drops its toast; `ReportViewModel::
  copySummary` → "Copied to clipboard."; `CommandBlock` via the wizard hook.
- Removed strings: "Copied.", "Summary copied.", "Only the first %Ln lines were copied (copy limit)." (replaced).
- Toast over modal dialogs (D21 recommended): the Toast column is reparented to `Overlay.overlay` with a `z` above
  dialogs (it never takes focus or input: `enabled: false` on the container), verified by a Qt Quick test with the
  report dialog open.
- Native text fields (D20 recommended): no toast for the platform Ctrl+C in `TextField` / `TextArea`.

### F6 — Flash bar

- `FlashPage.qml` exposes the form column geometry it already computes (`columnX`, `columnWidth`, from
  `scroll.availableWidth`) and passes them to `FlashBar` (D22 recommended), so both line up exactly also when the
  vertical scroll bar is visible. `FlashBar`'s `RowLayout` uses `x: columnX`, `width: columnWidth` instead of
  `anchors.fill` with 24 px margins; background and border stay full width.
- Flash button: `iconName: "flash"`; `Accessible.name` unchanged ("Flash").
- Below 960 px nothing changes in practice (column = window − 48 px).

### Settings keys

| Key | Type | Default | Use |
|---|---|---|---|
| `console/saveViewFolder` | string (local path) | "" (= Documents) | F2 |

### Limits (new)

Identity name 64 B / version 48 B / serial 32 B; slug 40 B, version slug 32 B; banner scan 200 lines or 5 s per
attach; collision suffix ≤ 9; tooltip width 360 px.

## Decisions requiring user confirmation

Confirmed rows are recorded in ADR-0200 … ADR-0204 (Accepted with the plan's approval on 2026-11-02). The rows that
were pending show the recommended option in bold; the user accepted every recommended option on 2026-11-02.

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| C1 | F1 name format | `<fw-slug>-<fw-version>-<UTC stamp>-<probe SN>.log`, SN last, UTC; unidentified → today's name; explicit `--console-file` never renamed | Confirmed (2026-11-02) | ADR-0200 |
| C2 | F1 identity source | `info show` primary; boot banner only when it carries the app name+version, never the SDK banner | Confirmed (2026-11-02) | ADR-0200 |
| C3 | F2 Save view | prefilled name from the log-name base (`-shell.txt` / `-log.txt`), last folder persisted, first time Documents, ViewModel provides it, non-local URLs refused | Confirmed (2026-11-02) | ADR-0201 |
| C4 | F4 Batch | long plain-text tooltip + documentation link | Confirmed (2026-11-02) | ADR-0203 |
| C5 | F5 copy feedback | one toast "Copied to clipboard." on every copy, via `AppViewModel::showToast`; none when nothing to copy | Confirmed (2026-11-02) | ADR-0202 |
| C6 | F6 Flash bar | bolt icon + bar contents in the centred column, bar stays sticky | Confirmed (2026-11-02) | ADR-0204 |
| C7 | F7 docs | out of scope, plan 0029 (docs.hardwario.com) | Confirmed (2026-11-02) | — |
| D1 | Slug rule | (a) fw-name slug only: `clime-v3.0.0-…`/`chester-clime-v3.0.0-…`; **(b) `<profile>-<target>-` + fw-name slug without a leading profile word: `chester-app-clime-v3.0.0-…` (matches the user's example `chester-app-clime-v4.0.0-…`)**; (c) `<profile>-<target>-` + full slug `chester-app-chester-clime-…` | Recommended option accepted by user 2026-11-02 | ADR-0200 |
| D2 | Real CHESTER `info show` keys (`firmware name:`), parser prerequisite | **(a) accept keys with spaces, normalise to lower-case-dashed (`firmware-name`), `fwName()` reads `fw-name` then `firmware-name`; `app info --json` shows the normalised keys**; (b) send `info fw-name` / `info fw-version` separately; (c) leave (F1 would never trigger on CHESTER) | Recommended option accepted by user 2026-11-02 | ADR-0166 note |
| D3 | A different firmware/device later in the same session (flash + reset, device swapped) | **(a) new file with a new stamp when name, version or device serial changes; marker lines link the two files**; (b) rename the same file again (it then holds both firmwares under the last name); (c) keep the first name | Recommended option accepted by user 2026-11-02 | ADR-0200 |
| D4 | Rename vs delayed open | **(a) open as today, rename once identity is known**; (b) buffer in memory until identity or a timeout, then open (lines lost on a crash; no file for devices without a shell) | Recommended option accepted by user 2026-11-02 | ADR-0200 |
| D5 | `.partN` written before the identity arrived | **(a) rename all existing parts of the session**; (b) only the current part and later ones; (c) only later parts use the new name | Recommended option accepted by user 2026-11-02 | — |
| D6 | Header | **(a) marker line `@ Firmware: <name> <version>` (no device serial)**; (b) rewrite the header block (rewrite the file); (c) nothing | Recommended option accepted by user 2026-11-02 | — |
| D7 | Who queries / CLI scope | **(a) new backend `DeviceInfoWatcher` (libs/device) used by the GUI tab and by `chester app console` in interactive and non-interactive mode when a generated file is written; one-shot `app command` unchanged**; (b) keep the two existing query sites and forward from each (CLI non-interactive: no rename); (c) CLI uses the banner only | Recommended option accepted by user 2026-11-02 | — |
| D8 | MCP-opened sessions (`hwio mcp`, `--console-file-dir`) | **(a) out of scope: names unchanged (no extra silent `info show` in AI-driven sessions)**; (b) same watcher | Recommended option accepted by user 2026-11-02 | — |
| D9 | BLE sessions | **(a) included: same watcher, `ble::SetFirmwareIdentity` on the BLE thread, name `chester-ble-clime-v3.0.0-<stamp>-<id>.log`**; (b) J-Link only in 0028 | Recommended option accepted by user 2026-11-02 | ADR-0200 |
| D10 | Banner fallback implementation | **(a) strict parser as in Design (deny-list of SDK names; used only when `info show` gave no identity)**; (b) postpone until HIL shows a firmware that prints an app banner | Recommended option accepted by user 2026-11-02 | — |
| D11 | Rename failure (file held by another program, Windows) | **(a) keep the old name, info log only**; (b) user-visible warning; (c) retry on the next flush | Recommended option accepted by user 2026-11-02 | — |
| D12 | Save view stamp | **(a) UTC `yyyyMMdd-HHmmss`, same as the console file**; (b) local time | Recommended option accepted by user 2026-11-02 | — |
| D13 | Save view to a non-local URL | **(a) error panel `io.write_failed` "Only folders on this computer can be used.", and the same in `ReportViewModel::save` (no silent return)**; (b) silent refusal like today's report | Recommended option accepted by user 2026-11-02 | — |
| D14 | Save view identity after a disconnect | **(a) last identity of the session kept (the view still shows that firmware's output)**; (b) fallback name after disconnect | Recommended option accepted by user 2026-11-02 | — |
| D15 | Translation lint (F3 side-product) | **(a) CTest `translation_lint` over `i18n/*.ts` `<translation>`: lone `&` (not `&&`/entity), `<`, combining marks U+0300–036F, invisible U+200B–200F/U+2060/U+FEFF/U+00AD, placeholder sets (`%1`, `%n`, `%L1`) equal to the source**; (b) none | Recommended option accepted by user 2026-11-02 | — |
| D16 | F3 fix level (after Phase C) | font file (e.g. other Inter build) / font features (`QFont::setFeature`, Qt 6.7+, e.g. disable `kern` for UI text) / render type (`QQuickWindow::setTextRenderType(NativeTextRendering)` or `CurveRendering` globally) / per-component; **recommended: the narrowest fix that removes the defect; a global font or render-type change is brought back to the user with the Phase C grabs before it is implemented** | Recommended option accepted by user 2026-11-02 (narrowest fix; global change goes back to the user) | new ADR if global |
| D17 | Batch docs link presentation | **(a) info icon button right of "Batch…" (tooltip "Batch files — documentation", opens the docs)**; (b) "Batch files help" item in the toolbar ⋯ menu; (c) Help menu entry only. (A link inside a tooltip or the native file dialog cannot be clicked.) | Recommended option accepted by user 2026-11-02 | ADR-0203 |
| D18 | Docs URL source | **(a) central `docsUrl(DocsTopic, language)` table in `libs/core` now (ADR-0138's idea), `ConsoleBatch` → the existing Monitor docs page until plan 0029**; (b) hard-code the URL in `ConsoleViewModel` | Recommended option accepted by user 2026-11-02 | ADR-0203 |
| D19 | Truncated console copy | **(a) one toast replaces the generic one: "Copied the first %n lines to the clipboard (copy limit)."**; (b) two toasts | Recommended option accepted by user 2026-11-02 | ADR-0202 |
| D20 | Native Ctrl+C in text fields | **(a) no toast**; (b) toast via a key handler on every field | Recommended option accepted by user 2026-11-02 | ADR-0202 |
| D21 | Toast while a modal dialog (wizard, report) is open | **(a) toasts drawn above dialogs (overlay layer, never focusable)**; (b) leave (copy in dialogs gives no visible feedback) | Recommended option accepted by user 2026-11-02 | ADR-0202 |
| D22 | Flash bar alignment | **(a) page passes the column x/width so both match exactly (also with a scroll bar)**; (b) bar centres its own ≤ 880 px row (off by the scroll bar width) | Recommended option accepted by user 2026-11-02 | ADR-0204 |
| U1 | F3 fix (D16 after Phase C) | **grayscale distance-field antialiasing for the whole app: `QSG_DISTANCEFIELD_ANTIALIASING=gray` set in `app/gui` `main()` before the `QGuiApplication` exists, never overriding a value already in the environment**; C3 with before/after grabs (harness: 0 coloured pixels) and Settings → Console CS dark + light screenshots | Decided by user 2026-11-02 (review round 1) | ADR-0205 |
| U2 | Secrets of `info show` in the console file | **the values of `claim token` and `ble passkey` (also `claim-token` / `ble-passkey`) are written as `<redacted>` in every console file (GUI, CLI, BLE), from a silent query or typed by the user; other lines unchanged** — the ADR-0186 mechanism with a profile list of sensitive output keys | Decided by user 2026-11-02 (review round 1) | ADR-0186 amendment |
| U3 | CLI console file path | **`chester app console` and `ble console` print `Console file: <final path>` on stderr at the end (human mode) and add `consoleFile` to the `--json` output, when they wrote a console file** | Decided by user 2026-11-02 (review round 1) | — |

## Implementation steps

The implementing agent starts only after the plan is **Approved** and every pending row is decided. Each phase ends
with `linux-debug` build + `ctest`, `clang-format` and `linux-tidy` clean.

### Phase A — backend: identity and file naming (F1)

- [x] A1 `parseInfoShow` per D2: key normalisation, `fwName`/`fwVersion`/`deviceSerial`, `(unset)` = missing; real
      CHESTER `info show` fixture (11 lines, from chester-sdk `ctr_info_shell.c`); old `fw-name` fixtures kept; fuzz /
      mutation test extended.
- [x] A2 `firmware_identity.h/.cpp`: `FirmwareIdentity`, `identityFromInfo`, `identityFromBanner` (D10),
      `fileSlug`, `versionSlug`, `consoleFileBase` (D1) + tests (Czech diacritics, emoji, empty, `(unset)`,
      `v3.0.0-5-gabc1234-dirty`, `.part` in a version, length cuts at the limits).
- [x] A3 `sessionKeyOf` rewrite + static assert "profile ids contain no `-`"; tests: all old names, new J-Link and
      BLE names, `.partN`, stamp-like version, BLE id with dashes, `-2` collision suffix.
      *Implemented as `consoleFileSessionKey()` (public, for the tests); the profile-id rule is a `static_assert` next to
      the CHESTER profile id (the registry is not `constexpr`).*
- [x] A4 `ConsoleFileSink::adoptIdentity` (rename all parts D5, `OpenFiles` update, marker D6, identity change D3,
      collision suffix, failure D11; explicit file no-op) + tests with a temp directory (including a held-open file
      on Windows CI, retention after rename, cross-process live rule with a fake lock owner).
      *Windows held-file test: not run here (Linux); the rename-failure test uses a read-only directory (Unix).*
- [x] A5 `ConnectionEvent::attach`; `ConsoleSession::setFirmwareIdentity`; J-Link `SetFirmwareIdentityCommand` +
      worker handling + `reportConsoleFile()`; banner scan window in the worker (D10) — fake J-Link tests: rename
      reported through `consoleFile()`, stale attach ignored, identity change → new file.
- [x] A6 BLE (D9): `ble::SetFirmwareIdentity`, handler, banner scan — fake BLE backend tests.
- [x] A7 `DeviceInfoWatcher` (D7) + tests (answer of the current attach only, refusal, timeout, reconnect).

### Phase B — GUI and CLI wiring (F1)

- [x] B1 `DeviceTabViewModel` uses `DeviceInfoWatcher` (behaviour of `deviceInfo`, "Now in the device" and the
      command-tree learner unchanged) — ViewModel tests updated.
- [x] B2 CLI `ConsoleLearning` uses the watcher; `chester app console` creates it also non-interactively when a
      generated console file is written (D7); `ble console` (D9); final "Console file:" line shows the renamed path —
      CLI end-to-end tests with the fake J-Link shell answering the real CHESTER `info show`.
      *Review round 1 (U3): the final "Console file: <path>" line (stderr) and the `consoleFile` JSON event were added;
      `DeviceSession::close()` drains the last ConsoleFileChanged, so the printed name is the final one.*
- [x] B3 Plan notes: plan 0002 / 0011 / 0013 file-name paragraphs and `plan/0006-cli.md` updated ("amended by 0028").

### Phase C — F3 investigation and fix

- [x] C1 Reproduction harness (Qt Quick Test, `tst_text_rendering`, not part of the default `ctest` label set if it
      needs xcb): exact string + combinations × sizes × weights × DPR × render types × themes; grabs as CI artefacts;
      advance measurements vs the font's hmtx/kern/GPOS data (fontTools on the developer machine).
- [x] C2 Report the findings and the proposed fix level to the user (D16). **Stop here until D16 is decided.**
      *2026-11-02: findings reported (§ Phase C findings); only a global fix removes the defect, so D16 is back with
      the user. Phase C stops here.*
- [x] C3 Implement the decided fix (U1, ADR-0205) (+ ADR if global) and a regression check in the harness (advance of `š` within
      0.5 px of the font's value; no coloured pixels in grayscale rendering).
- [x] C4 Translation lint per D15 (CTest `translation_lint`, Python `-I` script under `tools/`), fix any findings.

### Phase D — GUI items F2, F4, F5, F6

- [x] D1 F2: `saveViewFolder`, `saveViewFileName`, `saveView` non-local refusal (D13, also `ReportViewModel::save`),
      folder persistence; `ConsolePage.qml` `openFor` — ViewModel tests + Qt Quick test (dialog properties set).
- [x] D2 F4: `batchHelpText`, `HToolButton.description`, `HToolTip` max width, `docs_urls.h` (D18),
      `AppViewModel::documentationUrl` via the table, info button (D17), `openBatchDocs` — tests.
- [x] D3 F5: `AppViewModel::copyToClipboard(text, toast)`, hooks for tab / report / wizard, all sites per Design,
      QML `showToast("Copied.")` calls removed, truncation text (D19), toast layer (D21), CTest grep check for
      `QGuiApplication::clipboard()` — ViewModel + Qt Quick tests.
- [x] D4 F6: FlashBar column alignment (D22), bolt icon — Qt Quick test.
- [x] D5 `plan/gui/design-system.md`: FlashBar, Toast (unified copy text, above dialogs), CommandBlock, HToolButton
      description.

### Phase E — translations, verification, documentation of the plan

- [x] E1 `update_translations`; Czech for every new string (e.g. "Zkopírováno do schránky.", "Uloženo …" unchanged,
      Batch help text, the docs tooltip, "Only folders on this computer can be used." …); `check_translations` with 0
      unfinished; obsolete strings removed.
- [ ] E2 Full verification (§ Tests and verification), screenshots, HIL; status → Done; roadmap updated. Done by the
      user's decision with the mock E2E below passed; still open and needing real hardware or another OS (HIL):
      the real CHESTER boot banner text (D10 record); real RTT and BLE timing (BlueZ); flashing a different app from
      the Flash page and reconnecting on real hardware; physically swapping two CHESTERs on one probe; Windows rename
      while the log is open in another program (D11); native save dialogs on Linux GTK/portal, Windows and macOS; F3
      on the reporter's OS/scaling.

## Review round 1 (2026-11-02)

User decisions U1–U3 are in § Decisions. Review findings (all to be fixed):

- [x] U1 `QSG_DISTANCEFIELD_ANTIALIASING=gray` in `main()` (not overriding the environment); C3 before/after grabs,
      harness regression check (0 coloured pixels on xcb/wayland with the fix), CS dark + light screenshots of
      Settings → Console (../screenshots-demo); ADR-0205.
- [x] U2 `TargetProfile::sensitiveOutputKeys` (CHESTER app: `claim token`, `ble passkey`); `ConsoleFileSink` writes
      `<key>: <redacted>` for shell output lines (also after a prompt) whose normalised key matches; J-Link and BLE
      tests; ADR-0186 amendment.
- [x] U3 `Console file: <path>` on stderr and a `consoleFile` JSON event at the end of `chester app console` and
      `ble console` (when a file was written); E2E tests; plan 0006 note.
- [x] R1 `FirmwareIdentity` comparison: the device serial counts only when both are non-empty; an empty new serial
      keeps the stored one. Only the serial differs → marker "Device changed; continued in …" (no serial)
      (new text). Tests. *User decision 2026-11-02: the marker is untranslated English like every other console
      file marker ("Device changed; continued in <file>" / "Device changed"); not in the `.ts` files.*
- [x] R2 Rename keeps the live file protected from another sink's prune in this process: the new path is added to
      `OpenFiles` before the old file is closed, the old one removed after the reopen. Test.
- [x] R3 Collision suffix free for every part (`.partN`) of the target name; missing source parts (pruned) are skipped
      silently without using up the one-time D11 log. Tests.
- [x] R5 CMake option `HWIO_REQUIRE_PYTHON` (default OFF) makes Python 3 REQUIRED; ON in the Linux CI job;
      `INVISIBLE` written with `\u` escapes.
- [x] R6 Save view's last identity follows the identity the session adopted (also the banner fallback): the worker /
      BLE handler report the adopted identity with the console file event; the tab takes it from the session. Test.
- [x] R7 ADR-0128 amendment reworded (BLE keys from the first stamp after `-ble-`, J-Link keys from the end).
- [x] R8 Tests: watcher refusal and timeout → `setFirmwareIdentity(nullopt)`; Save view D14 check after Disconnected;
      `--console-file` keeps its name (no rename); a runtime loop over `ProfileRegistry::all()` (no `-` in ids).
      ADR-0200 consequences: `FirmwareIdentity` lives in libs/core.

## Tests and verification

- **GTest (libs/device, libs/core):** parser on real and old `info show`; slug/version rules; `consoleFileBase`;
  `identityFromBanner` (accept app banner, reject `nRF Connect SDK`, `Zephyr OS`, `Zephyr OS build`, `MCUboot`,
  `Using Zephyr OS`); `sessionKeyOf` old + new names; sink rename (single file, parts, collision, failure, explicit
  no-op, marker text, identity change → new file, retention keeps live files after rename); worker/BLE handler with
  fakes (attach mismatch ignored, `consoleFile()` follows); `DeviceInfoWatcher`; `docsUrl`.
- **ViewModel tests (GUI):** Save view folder fallback and persistence, file name with/without identity, after a
  disconnect (D14), non-local refusal error text; every copy site shows exactly one "Copied to clipboard." toast and
  none for an empty selection; truncation text; `batchHelpText` numbers; tab device info via the watcher.
- **Qt Quick Tests:** Save dialog gets `currentFolder` and `selectedFile`; Batch tooltip text and width; info button
  opens the docs (intercepted `openUrl`); toast visible above the open report dialog (D21); FlashBar button right
  edge = form right edge at 960, 1280 and 2560 px wide, with and without a scroll bar; Czech at 960 × 600; light and
  dark.
- **CLI end-to-end:** `chester app console` (interactive via the replxx test harness and piped) with the fake shell →
  console file renamed to `chester-app-clime-v3.0.0-…-<sn>.log`, marker present, final path printed; `--console-file`
  not renamed; `app info --json` keys per D2; `ble console` with the fake BLE backend (D9).
- **CTest checks:** `translation_lint` (D15), clipboard grep check, `qml_plaintext_check` (new tooltips are
  `HToolTip`), `check_translations`.
- **Presets:** `linux-debug`, `linux-asan`, `linux-tsan` (rename on the worker while the owner reads
  `consoleFile()`), `linux-tidy` (zero findings), `all_qmllint`, clang-format, `tools/check-lfs.sh`.
- **Screenshots** (`../screenshots-demo` kit with the fake J-Link; its `chester.json` / `scenario.js` must answer
  `info show` in the **real** CHESTER format): Settings → Console in Czech (F3, before/after), console toolbar with the
  Batch tooltip and info button, Save dialog prefilled (Linux), a copy toast, Flash page at 1280 and 2560 px wide in
  light/dark and Czech.
- **HIL (CHESTER + J-Link, required for F1):** CHESTER Clime (or another catalog app): GUI connect → file renamed to
  the identity name within ~2 s of Connected, marker line, "Open log file" opens the renamed file; flash another
  application from the Flash page → per D3 a new file named after the new firmware; swap to a second CHESTER with the
  same firmware on the same probe → per D3 (device serial differs); record the exact boot banner CHESTER prints
  (D10); CLI `chester app console` interactive and piped; BLE console (D9) on Linux; Windows: rename while the log is
  open in Notepad++ / `Get-Content -Wait` (D11). F3 on the reporter's OS/scaling if known.
- **Manual:** Save view on Linux (GTK and portal dialogs), Windows and macOS: Save with the prefilled name writes the
  file; second save starts in the same folder.

## Mock end-to-end verification (2026-11-02)

Ran the real CLI binary `hwio` and the GUI test harness (tst_gui: real `Main.qml`, ViewModels, backend) against the
fake J-Link and fake BLE with isolated HOME/XDG. Kit: `../screenshots-demo` (`gen.py` options `--fw-name`,
`--fw-version`, `--serial`, `--probe-sn`, `--info real|refused|silent|noident`, `--banner`; `e2e-gui.sh`, `qml-e2e/`,
`cli-e2e/` with a pty driver and a reload variant of the fake J-Link that re-reads the scenario on each boot).
All PASS, no bugs found.

| Area | Result |
|---|---|
| CLI console, piped and interactive | Rename to `chester-app-clime-v3.2.0-<UTC>-<SN>.log` ~0.7 s after connect; `@ Firmware` marker; "Console file:" on stderr; `--json` `consoleFile` before `stopped`; secrets `<redacted>` in the file (silent query and the user's `info show`); explicit `--console-file` not renamed |
| Identity changes | Firmware change after reconnect → new file with markers; device-serial-only change → "Device changed; continued in …"; empty↔non-empty serial over 3 reconnects → one file |
| Banner fallback | App banner used when `info show` refused / silent / no fields; Zephyr OS / MCUboot / nRF Connect SDK banners rejected; `info show` wins over banner |
| Retention | 60 pre-filled files, two concurrent processes → 50 newest kept, all live files kept, non-`.log` files ignored |
| `ble console` | `chester-ble-clime-v3.0.0-<UTC>-<id>.log`, marker, redaction, "Console file:" line, `--json`, explicit file not renamed |
| GUI (harness) | Open log file → renamed file (358 ms after Connected); Save view prefill `Documents/…-log.txt`, write, last folder persisted across a simulated restart, identity kept after disconnect (D14); one "Copied to clipboard." toast; Flash button right edge = form right edge at 1280 and 2560 px |

Screenshots: `../screenshots-demo/out-0028-mock-e2e/` (untracked).

Limits: the real `hwio-monitor` binary was only smoke-started (no way to click Connect offscreen); the native Save
dialog was not exercised.

## Risks and open points

- **F3 environment unknown:** the reporter's OS, display scaling and Qt platform (X11/Wayland) are optional but
  useful; ask the user for them alongside the Phase C report. If Phase C cannot reproduce the defect, D16 falls back to
  the render-type comparison grabs only and the item stays open.
- **0021 identity never worked on real CHESTER** (D2): fixing the parser also changes the command-tree cache keys
  and the `app info --json` keys — existing caches simply miss once; `app info --json` consumers (none known) see new
  keys. 0021's HIL (still open) should be run after A1.
- **Secrets in `info show`:** the answer also contains `claim token` and `ble passkey`. The identity keeps only name,
  version and serial; the marker never writes the serial; `ShellQuery` logs its silent output at debug level only —
  the report redaction of ADR-0186 covers `token:` / `passkey:`; the A1 tests assert that the parsed `DeviceInfo`
  passed to the GUI is not logged at info level.
- **Rename races with external readers:** a user tailing the file sees it disappear under the old name; documented in
  the docs plan 0029. On Windows a reader without delete-sharing blocks the rename (D11).
- **Silent `info show` in non-interactive CLI** (D7a) sends one more command to the device than today in piped
  scripts; the query waits for an idle shell and holds user lines (ADR-0175), so ordering is preserved.
- **Banner formats differ between NCS/Zephyr versions**; the strict deny-list may reject a valid app banner or accept
  an unknown SDK banner — `info show` always wins.
- **Docs URL of Batch** points to the general Monitor page until plan 0029; ADR-0138's GitHub `docs/` target will be
  superseded by 0029.
- **Toast over dialogs** (D21): the overlay layer must not steal focus or clicks; covered by the Qt Quick test.
