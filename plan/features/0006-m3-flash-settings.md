# 0006 — M3 flash and settings pages

> **Amended by the 0004 review (S2/S3/S5 of 2026-10-12; applied 2026-10-16):** `DeviceTabViewModel::ensureSession()`
> opens the tab's console target (with RTT when that target has an RTT console); operations carry their own target
> (ADR-0057), so `ensureSession(kindTarget, attachRtt = false)` of the first version of this plan is replaced by
> `ensureSession()` + an operation with `targetId`. Since 0007 the signature is `ensureSession(bool connectTarget =
> true)` (`false` opens a probe-only session for wizards); the Flash page uses the default. Pages hold the session
> only as `QPointer` or re-read it on `sessionChanged()`. The shell owns the console markers ("— paused: … —",
> "— resumed —"); this plan never appends markers. F4 is a global shell shortcut only; page shortcut descriptions use
> the "Shortcuts" translation context. See plan 0004, "Fixes of review round 1".

- **Status:** In progress (implementation started 2026-10-16 on `v2/m4-wizards`; approved 2026-10-16 by Matej Strnad, with the amendment from the 0004 review and the 0007/0008/0010 amendments applied)
- **Milestone:** M3 (plan 3 of 3, ADR-0059)
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0016, ADR-0025, ADR-0027, ADR-0036, ADR-0037, ADR-0038, ADR-0040, ADR-0047, ADR-0049,
  ADR-0054, ADR-0055, ADR-0057, ADR-0059, ADR-0060, ADR-0062, ADR-0067, ADR-0114
- **Depends on:** [0004](0004-m3-gui-shell.md) (shell, page-extension contract, `SettingsStore`), 0003 (M2 with the
  ADR-0057 / ADR-0062 amendments applied: operations with a target, `flashLteBundle`, `reset` as an operation)
- **Can run in parallel with:** [0005](0005-m3-console.md)
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

> **Amended by ADR-0114 (2026-10-16, review F5):** the tab's session gets an nRF Util options provider that reads the
> `SettingsStore` when a modem update or an LTE bundle is requested. A path changed in Settings (or by the setup
> wizard) therefore reaches an open session.

> **Amended by 0007, 0008 and 0010 (approved 2026-10-16; applied in this text):** the source
> part of `FlashViewModel` moves into a reusable child `FirmwareSourceViewModel` (also used by the flash wizard,
> 0010); the Flash page header gets the ghost button "Guide me" (flash wizard, 0010); the J-Link and nRF Util
> settings sections get the link "Run the setup wizard" (0008). In the error table, `FirstRunSetup` is the C++
> enumerator name; QML/JSON use the kebab id `first-run-setup` (ADR-0067, 0007).


> **Pending amendment from M6 ([0014](0014-m6-mcp-gui.md)):** contributed Settings section "AI assistants (MCP)".
## Context

Flashing by catalog ID or by a local `.hex` is the second core workflow of the old app. Its problems:
- the user had to detach the console manually;
- there was no real progress;
- there was no family check;
- there was no way to flash the LTE MCU or the modem.

The old app also had no settings page at all. M2 delivers the backend and the CLI for flashing. This plan adds:
- the Flash page, per tab;
- the global Settings page, which also hosts the settings sections of the other M3 plans.

## Goals / Non-goals

**Goals**
- Flash page (ADR-0055, ADR-0049, ADR-0054, ADR-0057), registered as page `flash`:
  - choose what to flash first (application / LTE / modem firmware), then a file, a catalog ID or a recent entry,
    then the options; a sticky Flash bar;
  - phase progress (`plannedPhases`, step 1/2 for LTE bundles) with Cancel, and a result card;
  - maintenance actions (Reset, Erase, Erase all, Recover) with ADR-0038 confirmation;
  - works whether or not the tab is connected, and for another MCU than the console's (ADR-0057).
- `FirmwareLoader` worker: HEX/zip parsing and LTE classification off the GUI thread.
- Settings page (global, Ctrl+,), registered as page `settings` with `global = true`:
  - General: language (live), theme (live), reopen tabs at start;
  - J-Link: library path and defaults; nRF Util path;
  - log-file folder;
  - About and licences;
  - hosts the sections contributed by other plans (the Console section from 0005).
- EN + CS, accessibility per the design system § 6.

**Non-goals**
- Catalog browsing (open question 15); the GUI uses the ID field only (ADR-0054).
- Flash wizard and "Help me fix this" (M4), J-Link firmware update button (M4). *Amended by 0010 (2026-10-16):*
  this plan only adds the entry points ("Guide me", "Run the setup wizard"); the wizards are 0008 and 0010.
- PIB / UICR read in the GUI (CLI only in M2; candidate for M4 diagnostics).

## User experience

### Flash page

Per ADR-0055 (kind first) and ADR-0049 (progress, result, maintenance). *Amended by 0010 (2026-10-16):* the page
header has a small ghost button **"Guide me"** that opens the flash wizard (`tab.openWizard("flash-firmware")`, 0004).
The cards are:

1. **What do you want to flash?** — three radio cards. The default comes from the tab's target.

   | Kind | MCU | Accepts |
   |---|---|---|
   | **Application firmware** | Application MCU (nRF52840) | `.hex` or catalog ID |
   | **LTE firmware** | LTE MCU (nRF91) | `.hex`, LTE bundle `.zip` or catalog ID |
   | **LTE modem firmware** | Modem core via nRF Util | `mfw_nrf91*.zip` only |

   Prerequisites are shown inline in this card as soon as a kind is chosen. For example: "nRF Util was not found. It is
   needed only for LTE modem firmware." [Set path in Settings] [How to install ↗].
2. **Firmware file** — a drop zone and [Browse…], with the file dialog filtered to the kind.
   - The field "or firmware ID from the catalog" (not for modem): 32 hex digits, with the inline validator "The ID has
     32 characters 0–9 a–f". [Download] shows progress in the card.
   - **Recent**: 10 items filtered to the kind (name, type, date; × removes).
   - After loading, a summary appears: "app.hex · Intel HEX · 412 kB · 0x00000–0x64F20" / "LTE bundle (application +
     modem 1.3.7)".
   - A file of another kind (classified per ADR-0040) shows a hint: "This is LTE modem firmware — switch to LTE modem
     firmware?" [Switch]. The kind never changes silently.
   - Modem and LTE bundle: the note "Modem update takes 1–3 minutes. The console is disconnected meanwhile."
3. **Options** — "Halt the CPU after flashing" (not for modem), and "Allow writing UICR" with a warning, only when the
   image has UICR records (ADR-0036).
4. **Sticky bottom bar** — a summary ("Application firmware · app.hex") and **[Flash]** (Ctrl+Enter). While Flash is
   disabled, the bar says why (e.g. "Choose a firmware file", "nRF Util was not found", "Another operation is
   running").

- **Running:** the cards are replaced by the PhaseStepper (phases from `OperationProgress::plannedPhases`, with
  "Step 1 of 2" for LTE bundles), the percentage, the bytes, the elapsed time and [Cancel]. Cancel shows
  "Cancelling…" until the operation ends.
  - The Flash rail item shows a busy badge (`setBusy("flash", "Flashing…", "Flashing is in progress. Cancel it and
    close the tab?", cancel)`).
  - The device bar shows "Flashing…".
- **Result:**
  - Success: "Firmware flashed and verified in 14 s." plus the identified chip [Open console] [Flash again]. A toast is
    also shown when the user is on another tab.
  - Failure: an ErrorPanel (e.g. `firmware.verify_failed`) with [Try again].
  - Cancel (`cancelled`): "Flashing was cancelled. The device may not start until you flash it again." [Flash again].
- **Maintenance** (collapsible, collapsed by default):
  - [Reset] [Reset and halt] — no confirmation.
  - [Erase application] (code only) — no confirmation. *Review F2 (2026-10-16):* only where the profile's
    `defaultErase` is code only (CHESTER `app`); the LTE MCU is always erased completely behind the confirmation.
  - [Erase everything…] (app all / lte erase) and [Recover…] — ConfirmDialog (ADR-0038), e.g. "Erase everything on
    the application MCU?" • Firmware (flash) • UICR configuration • **Product Information Block (serial number,
    hardware version …)** — "This cannot be undone." [Cancel] [Erase everything].
  - Maintenance acts on the MCU of the selected kind (application firmware → app; LTE / modem → lte). The section
    title names it: "Maintenance — application MCU".
- **Not connected is fine.** Flash calls `tab.ensureSession()` (*amended by the 0004 review*: the tab's console
  target, with RTT when it has a console) when the tab has no working session, then starts the operation with the
  kind's `targetId`. If the tab's console is attached to another MCU of the same probe, the operation carries its
  target (ADR-0057): RTT pauses, the probe switches MCU and runs the operation, then it returns and RTT re-attaches.
  The stepper then also shows "Switching MCU" and "Restoring console". The console markers ("— paused: Flashing… —",
  "— resumed —") are added by the shell, never by this page.

### Settings page (global, Ctrl+,)

Changes apply immediately (no OK/Cancel). Invalid input shows an inline error and is not saved.

| Section | Items | Owner |
|---|---|---|
| General | Language: System / English / Čeština (live) · Theme: System / Light / Dark (live) · Reopen tabs at start | 0006 |
| Console | Font size (8–24) · Show timestamps · Wrap long lines · Save console to log files (default on) · Log folder [Open] · Retention info "Last 50 files, max. 200 MB" | 0005 (section component) |
| J-Link | Library: Automatic (detected: `/opt/SEGGER/JLink/libjlinkarm.so` 8.10) / Custom path [Browse…] · Default speed (kHz) · RTT latency (ms) · Auto-reconnect · Reset on connect · link "Run the setup wizard" (*amended by 0008, 2026-10-16*) | 0006 |
| nRF Util | Path: Automatic (detected 7.x / "not found — needed only for LTE modem firmware") / Custom [Browse…] · link "Run the setup wizard" (*amended by 0008, 2026-10-16*) | 0006 |
| About | Version, build info, Qt, J-Link and nRF Util versions [Copy] · Licences (Qt LGPL, fonts OFL, Material Symbols Apache 2.0, miniz, replxx, …) · Links: documentation, firmware catalog, source | 0006 |

*Amended by 0008 (2026-10-16):* "Run the setup wizard" opens `first-run-setup` (0008), text link only. Saving a path in
the setup wizard invalidates the cached `dependency_status` so this page updates.

Read-only settings file (`settings.newer_schema`): a banner says "Settings were created by a newer version and are
read-only." and all controls are disabled.

### Shortcuts (ADR-0051)

Ctrl+Enter (Flash, scope `flash`), Ctrl+O (Browse…, scope `flash`), Ctrl+, (Settings, global — registered by 0004).
*Amended by the 0004 review:* the page shortcut descriptions use `QT_TRANSLATE_NOOP("Shortcuts", …)`; F4 and
Ctrl+Shift+D are global shell shortcuts and are not registered by this page.

### Texts

The English source texts are as above. Czech is translated in Qt Linguist as part of done (ADR-0027).

## CLI

**None.** The page maps to `hio chester app|lte flash|erase|reset|recover` (M2). Settings are shared with the CLI
through the INI file (ADR-0025); the CLI reads `jlink/libraryPath` and `nrfutil/path`.

## Design

### Modules

```
app/gui/
  src/viewmodels/
    flash_view_model.*              registered as page "flash"; parent = DeviceTabViewModel
    firmware_source_view_model.*    source part of the Flash page, also used by the flash wizard (amended by 0010)
    recent_sources_model.*          QAbstractListModel over `flash/recent`
    settings_view_model.*           registered as global page "settings"
    dependency_status.*             J-Link / nRF Util detection for Settings, About and flash prerequisites
  src/workers/
    firmware_loader.*               owned worker: HEX/zip parse + LTE classification off the GUI thread
  qml/
    pages/FlashPage.qml SettingsPage.qml
    flash/ KindCards.qml SourceCard.qml OptionsCard.qml FlashBar.qml ResultCard.qml MaintenanceSection.qml
    settings/ GeneralSection.qml JLinkSection.qml NrfUtilSection.qml AboutSection.qml
  tests/ tst_flash*.qml tst_settings*.qml  viewmodels/flash_*_test.cpp settings_*_test.cpp
```

### ViewModel interfaces (sketch, `hio::gui`)

*Amended by 0010 (2026-10-16):* `FlashViewModel` keeps its API, but its source part (kind-filtered loading, catalog
download, recent list, summary, kind hint, `hasUicr`, `allowUicr`) lives in a child `FirmwareSourceViewModel`
(parent = the `FlashViewModel`; the flash wizard creates its own instance). All instances share the app's
`FirmwareLoader` worker and the `flash/recent` key. `SourceCard.qml` binds to `FirmwareSourceViewModel`.

```cpp
class FirmwareSourceViewModel : public QObject {   // amended by 0010; child of FlashViewModel or of the flash wizard
    Q_PROPERTY(Kind kind ...)                       // set by the owner; filters loading and the recent list
    Q_PROPERTY(SourceState sourceState ...) Q_PROPERTY(QString sourceSummary ...) Q_PROPERTY(qreal downloadProgress ...)
    Q_PROPERTY(bool hasUicr ...) Q_PROPERTY(bool allowUicr ...)
    Q_PROPERTY(QString kindHint ...) Q_PROPERTY(int suggestedKind ...)
    Q_PROPERTY(QAbstractListModel* recent ...)
    Q_INVOKABLE void loadFile(const QUrl&); Q_INVOKABLE void loadCatalogId(const QString&); Q_INVOKABLE void loadRecent(int row);
};

class FlashViewModel : public QObject {             // source properties and invokables forward to `source`
    Q_PROPERTY(FirmwareSourceViewModel* source READ source CONSTANT)   // amended by 0010
    Q_PROPERTY(Kind kind READ kind WRITE setKind NOTIFY kindChanged)   // AppFirmware, LteFirmware, ModemFirmware
    Q_PROPERTY(QString targetId ...)                                   // derived from kind: "app" | "lte"
    Q_PROPERTY(SourceState sourceState ...)  // None, Loading, Downloading, Ready, Invalid
    Q_PROPERTY(QString sourceSummary ...) Q_PROPERTY(qreal downloadProgress ...) Q_PROPERTY(bool hasUicr ...)
    Q_PROPERTY(QString kindHint ...) Q_PROPERTY(int suggestedKind ...)  // mismatch hint + [Switch]
    Q_PROPERTY(hio::gui::ErrorInfo prerequisite ...)                    // e.g. nRF Util missing
    Q_PROPERTY(bool halt ...) Q_PROPERTY(bool allowUicr ...)
    Q_PROPERTY(bool canFlash ...) Q_PROPERTY(QString cannotFlashReason ...)
    Q_PROPERTY(OpState opState ...)          // Idle, Running, Cancelling, Succeeded, Failed, Cancelled
    Q_PROPERTY(QVariantList phases ...) Q_PROPERTY(int currentPhase ...) Q_PROPERTY(int step ...) Q_PROPERTY(int steps ...)
    Q_PROPERTY(qreal progress ...) Q_PROPERTY(QString progressText ...) Q_PROPERTY(QString resultText ...)
    Q_PROPERTY(hio::gui::ErrorInfo error ...)
    Q_PROPERTY(QAbstractListModel* recent ...)
    Q_INVOKABLE void loadFile(const QUrl&); Q_INVOKABLE void loadCatalogId(const QString&); Q_INVOKABLE void loadRecent(int row);
    Q_INVOKABLE void flash(); Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset(bool halt); Q_INVOKABLE void eraseCode();
    Q_INVOKABLE QString describeEraseAll(); Q_INVOKABLE void eraseAll();   // QML shows ConfirmDialog first
    Q_INVOKABLE QString describeRecover();  Q_INVOKABLE void recover();
};

class SettingsViewModel : public QObject {    // one typed property per item; writes via SettingsStore (validated)
    Q_PROPERTY(QString language ...) Q_PROPERTY(QString theme ...) Q_PROPERTY(bool restoreTabs ...)
    Q_PROPERTY(QString jlinkLibraryMode ...) Q_PROPERTY(QString jlinkLibraryPath ...) Q_PROPERTY(QString jlinkDetected ...)
    Q_PROPERTY(int defaultSpeedKhz ...) Q_PROPERTY(int defaultLatencyMs ...) Q_PROPERTY(bool autoReconnect ...) Q_PROPERTY(bool resetOnConnect ...)
    Q_PROPERTY(QString nrfutilMode ...) Q_PROPERTY(QString nrfutilPath ...) Q_PROPERTY(QString nrfutilDetected ...)
    Q_PROPERTY(bool readOnly ...) Q_PROPERTY(QVariantList sections ...)   // incl. sections contributed by other plans
    Q_PROPERTY(QString aboutText ...) Q_INVOKABLE void copyAbout();
};
```

Operation mapping (ADR-0057, ADR-0062; all on the tab's session, `targetId` from the kind):

| Kind / action | Session call |
|---|---|
| Application firmware, `.hex` / ID | `flash("app", image, {halt, allowUicr})` |
| LTE firmware, `.hex` / ID | `flash("lte", image, …)` |
| LTE firmware, bundle `.zip` | `flashLteBundle("lte", bundle, …)` |
| LTE modem firmware | `updateModem("lte", zip)` |
| Reset / Reset and halt | `reset(target, Go \| Halt)` |
| Erase application / Erase everything | `erase(target, Code \| All)` (lte default: All, per profile) |
| Recover | `recover(target)` |

### Data flow and threading

- **GUI main thread:** `FlashViewModel`, `SettingsViewModel`, `FirmwareCatalog` and `FirmwareCache` (M2).
- **`FirmwareLoader`:**
  - It is one owned `std::jthread` for the application, with a bounded queue of capacity 4; the newest request
    replaces older pending ones.
  - It parses HEX/zip and classifies LTE input. The result is `std::shared_ptr<const FirmwareImage>` or
    `std::shared_ptr<const LteBundle>` (immutable) or an `hio::Error`, delivered by a queued call with the request
    generation (stale results are dropped).
  - This implements the "GUI will move parsing to a worker" note of 0003.
- **Catalog download:** `FirmwareCatalog::download` on the GUI thread (QNetworkAccessManager). The downloaded
  bytes are parsed by `FirmwareLoader`.
- **Operations:** progress and results arrive through the session's queued `operationProgress` /
  `operationFinished`. `FlashViewModel` keeps the `OperationId` and ignores the events of other operations. It holds
  the session only as `QPointer` and reconnects on `sessionChanged` (*amended by the 0004 review*). On
  `sessionChanged` during an operation (should not happen, because the tab is busy) the result is reported as
  failed with the session's error.
- **Busy:** `setBusy` when the operation starts, `clearBusy` in `operationFinished` (also on failure and cancel).
- **Dependency detection** (`LibraryLocator`, `NrfUtilLocator` + version probe) runs on the `FirmwareLoader`
  thread (it can take up to 10 s). Results are cached until a path setting changes.

### Error cases (GUI-originated)

| Situation | Code | Message / cause / suggestion | Wizard |
|---|---|---|---|
| Invalid catalog ID | (inline validator, not an error) | "The ID has 32 characters 0–9 a–f." | — |
| File of another kind | (hint, not an error) | "This is LTE modem firmware — switch to LTE modem firmware?" | — |
| Settings path not writable / invalid | `io.write_failed` / `usage.invalid_argument` | inline at the control | — |
| Custom J-Link path invalid | `dependency.jlink_load_failed` (existing) | shown inline in Settings | FirstRunSetup (C++ enumerator; QML/JSON `first-run-setup`, ADR-0067; *amended by 0007, 2026-10-16*) |
| Flash while another operation runs | `internal.busy` (existing) | Flash disabled, the bar says "Another operation is running" | — |

All backend errors from 0003 are shown through `ErrorPanel` with their texts. The Firmware category's primary
action is "Choose another file".

### Settings keys added (registered via `SettingsStore::registerKeys`, ADR-0025)

| Key | Type | Default |
|---|---|---|
| `flash/recent` | string (JSON list, max 10 entries: `{kind, source: path \| "catalog:<id>", time}`) | — |
| `jlink/libraryPath`, `nrfutil/path` | string (existing keys from M1/M2; edited here) | — |

*Changed by the user's decision of 2026-10-16 (review F4):* there is no `flash/lastKind` key. The Flash page's default
kind is the tab's target (ADR-0055); the last kind chosen in a tab is remembered per tab, in memory only, for the
session.

### Limits

| Item | Limit |
|---|---|
| Recent sources | 10 |
| FirmwareLoader queue | 4 |
| Progress updates shown | ≤ 10/s (backend coalesces to 100 ms) |
| Files accepted | the M2 limits (HEX 64 MiB, zip limits, download 32 MiB) |

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Flash page | single form / wizard | Single form + in-place progress | ADR-0049 |
| Catalog in GUI | ID only / browser now | ID field + recent + web link | ADR-0054 |
| Flash page first step (review) | firmware kind cards / target MCU / operation first | Kind cards: app / LTE / modem | ADR-0055 |
| Flashing another MCU than the console's | operation carries target / retarget tab / second session | Operation carries its target | ADR-0057 |
| Session operation API | concrete types; bundle one op / caller sequences; reset op / keep both | Bundle = one operation, reset = operation, cancel = `cancelled` | ADR-0062 |
| Settings sections of other plans | page hosts contributed sections / 0006 owns all | Contributed sections (Console by 0005) | ADR-0059 |

## Implementation steps

Phase D — flash
- [x] D0 Prerequisite check: 0004 is Done; 0003 implemented with the ADR-0057 / ADR-0062 amendments (otherwise stop
  and report) — 0003 is implemented with both amendments (operations with `targetId`, `flashLteBundle`, `reset` as
  an operation; Phase 0 GO for the application MCU). 0004 is implemented incl. review rounds 1/2 but still "In
  progress" (its HIL and manual checks by the user are open); the coordinator started this plan on that basis.
- [x] D1 `FirmwareLoader` worker + catalog download integration + `RecentSourcesModel`; `FirmwareSourceViewModel`
  (amended by 0010; if this plan is implemented first, 0010 W1 does the extraction as a refactoring)
- [x] D2 `FlashViewModel` + kind cards, prerequisites, source card filtered by kind, kind-mismatch hints, options,
  sticky Flash bar; header "Guide me" (amended by 0010; wired to the wizard by 0010 W4)
- [x] D3 PhaseStepper wiring (`plannedPhases`, step/steps), cancel, result card, busy integration (`setBusy` /
  `clearBusy`, toast)
- [x] D4 Maintenance section + ConfirmDialogs (ADR-0038)

Phase E — settings
- [x] E1 `SettingsViewModel` + SettingsPage (General, J-Link, nRF Util, About, contributed sections; live language
  and theme; read-only mode) + dependency detection; "Run the setup wizard" links (amended by 0008; wired by 0008 S4)
- [x] E2 Accessibility; Czech translation, `.ts` fresh; screenshots of the Flash and Settings pages in both themes and
  both languages

## Tests and verification

- **ViewModel unit tests (GTest, fake `DeviceSession` via the fake J-Link library and fake nRF Util from 0003):**
  - flash state machine (load → flash → progress → success / fail / cancel) for each kind, incl. the LTE bundle
    (step 1/2 → 2/2, modem failure);
  - cross-target flash from a tab whose console is on `app` (phases incl. SwitchingTarget / Restoring);
  - flash from a disconnected tab (`ensureSession()` + an operation with `targetId`; amended by the 0004 review);
  - kind mismatch hints for every ADR-0040 class;
  - catalog ID validation; download progress and errors via the local HTTP fixture;
  - recent list (bounded, per kind, removal);
  - confirmation description texts (erase all, recover; app and lte);
  - `internal.busy` → Flash disabled with the reason;
  - settings validation, live language/theme, read-only mode, dependency detection results.
- **Qt Quick Tests:** Flash page and Settings page in light and dark; layout at 960 × 600 in Czech (geometry
  assertions); ConfirmDialog keyboard behaviour (Esc = Cancel, the destructive button is not the default).
- **`qmllint`** clean; no logic in QML.
- **Sanitizers:** ASan+UBSan and TSan run the ViewModel tests and the scripted flash smoke test (offscreen).
- **HIL (CHESTER + J-Link):**
  - flash an app hex and an ID; LTE bundle; modem zip;
  - cancel mid-flash;
  - flash `lte` while the console streams from `app` in the same tab, then the console resumes;
  - erase all + recover with confirmations;
  - one tab flashes while another tab streams.
- **Manual on Linux, Windows, macOS (by the user, before Done):** the flash and settings mockup scenarios, drag and
  drop of files, keyboard-only walkthrough, live language switching, nRF Util detection.

## Risks and open points

- **Cross-target operations** depend on the M2 hardware spike results for switching the probe between MCUs on one
  connector (ADR-0057). If the spike fails, the Flash page falls back to "disconnect the console first", which needs a
  new ADR.
- **Long modem updates** (1–3 min) with the probe released: the device bar must make clear that the console is
  paused, not lost.
- **Catalog browsing** stays out (open question 15). Users copy IDs from the web catalog.

## Implementation notes

Recorded by the implementing agent (2026-10-16, branch `v2/m4-wizards`, on top of e0b2a23). Status stays
"In progress": Done needs the HIL steps and the manual checks by the user. The open points at the end need a
decision by the user.

### Modules (as built)

- `src/workers/firmware_loader.*` — the owned `std::jthread` (D1): loads files (`classifyLteInput`, ADR-0040),
  detects the J-Link library (locate + one short-lived library copy for the version, ADR-0030) and nRF Util (locate +
  version probes). Queue of 4; the newest request of the same requester and kind replaces its pending one; results by
  generation through one coalesced queued call (the DiagWorker pattern).
- `src/pages/firmware_services.*` — **not in the module sketch**: one object (Qt child of `AppViewModel`) that owns
  what the Flash page, the Settings page and the flash wizard (0010) share: the loader, the firmware cache and the
  catalogs (per profile), `DependencyStatus`, the 0006 setting keys and the shell hooks (language switch, "is this
  tab's Flash page on screen"). It keeps 0006 out of `AppViewModel` (0005 is implemented in parallel).
- `src/pages/flash_settings_pages.*` — registration; `registerBuiltInPages()` calls it instead of the Flash and
  Settings placeholders (the Console placeholder stays for 0005).
- `src/viewmodels/` `flash_view_model.*`, `firmware_source_view_model.*` (amended by 0010), `recent_sources_model.*`,
  `settings_view_model.*`, `dependency_status.*`.
- QML: `pages/FlashPage.qml`, `pages/SettingsPage.qml`, `flash/` (FlashCard, KindCards, SourceCard, OptionsCard,
  FlashBar, ProgressCard, ResultCard, MaintenanceSection), `settings/` (SettingsRow, GeneralSection, JLinkSection,
  NrfUtilSection, AboutSection).
- Tests: `tests/viewmodels/flash_view_model_test.cpp` (FlashTest, LoaderTest), `settings_view_model_test.cpp`
  (SettingsTest), `tst_flash.qml`, `tst_settings.qml`, `tst_screenshots_flash.qml` (only with HIO_SCREENSHOT_DIR);
  `tst_shell.qml` now checks the real Settings page. The fake nRF Util is used through `HIO_NRFUTIL`.

### Contract and shared files

- Sessions: `tab.ensureSession()` (console target, RTT when it has a console; `connectTarget = true`, 0007) and then
  the operation with the kind's `targetId`; the session is held as `QPointer` and re-read on `sessionChanged`. Flash
  operations use the tab's connection options, i.e. **automatic speed by default** (ADR-0090, ADR-0111).
- Busy: `setBusy("flash", "Flashing…" / "Updating the modem…" / "Erasing…" / "Recovering…" / "Resetting…", …)`;
  the page never adds console markers.
- Settings sections: General 10, J-Link 30, nRF Util 40, About 90; **0005's Console section should use order 20**.
  The Settings page renders each section's title (from the registry) above the section component; a section reaches
  its view model itself (`AppViewModel.globalPageViewModel("settings")`), so contributed sections need no property.
- Keys registered here: `flash/recent` and — the first GUI registration of these M1/M2 keys —
  `jlink/libraryPath`, `nrfutil/path` (strings ≤ 4096; 0008 saves through them). (`flash/lastKind` was removed by
  the user's decision on review F4, 2026-10-16.)
- Shortcuts: `flash.start` Ctrl+Return and `flash.browse` Ctrl+O, scope `flash`, "Shortcuts" context.
- `ConnectionOptions` (connection_view_model.h) gained `storedSpeed()`, `applyStoredSpeed()`, `speedWarning()` and
  `speedToStore()`; the ⋯ popover and the Settings page both use them (no duplicated speed logic).

### Facts that postdate the plan

- **ADR-0090 / ADR-0111:** Settings → J-Link → "Default speed (kHz)": an "Automatic speed" switch (on by default) and
  the kHz box, disabled while automatic and keeping the last fixed value (4000 kHz when none was stored; not persisted
  while automatic, as in the popover); `jlink/speedKhz` stores 0 for automatic; the ADR-0110 warning below 125 kHz.
- **ADR-0109:** a flash that fails with `target.program_failed` shows "Flashing was interrupted. The firmware may be
  partly written: flash it again." above the ErrorPanel (which shows the ADR-0109 cause and suggestion); [Retry]
  flashes again.
- **ADR-0057 Phase 0:** whenever the kind's MCU is not the tab's console target (LTE or modem firmware on an `app`
  tab, application firmware on an `lte` tab) the kind card shows, before starting, a warning that on CHESTER-M the
  other MCU is reachable only through its own connector (move the cable, switch the tab's target). Flash stays
  enabled (other CHESTER variants were not checked); the backend then fails early with `target.wrong_family`. This
  resolves the first item of "Risks and open points" as recorded in ADR-0057.
- **nRF Util not installed (this machine):** for modem firmware and for LTE bundles (their step 1/2 is the modem) the
  missing nRF Util is shown inline with [Set path in Settings] [How to install ↗] and Flash is disabled ("nRF Util was
  not found"); a too old nRF Util or a missing device command shows its message + suggestion. Settings → nRF Util
  shows "Not found — needed only for LTE modem firmware" with [How to install ↗] [Check again].
- **H3/H5** need nothing in the GUI. **ADR-0112:** no new binary files (icons, fonts unchanged).

### Local choices (minor)

All confirmed by the user 2026-10-16, except the initial kind (reversed, see below); the "Erase application" and
Recent items were changed by the review fixes (F1, F2, F7).

- Detection runs lazily (Settings page shown, modem firmware or an LTE bundle chosen) and is cached; a change of
  `jlink/libraryPath` / `nrfutil/path` (also from the setup wizard) checks again.
- Custom paths are checked **before** they are saved (J-Link: located and loaded; nRF Util: located): an invalid path
  shows the error inline and is not saved. An nRF Util that exists but is too old is saved (the status shows why it
  cannot be used).
- UICR (ADR-0036): when the image has UICR records, Flash is disabled until "Allow writing UICR" is checked ("The
  firmware writes the UICR: allow it in Options"); the option is reset for every new image.
- An Intel HEX loaded as modem firmware suggests the tab's MCU (application or LTE firmware).
- ~~Initial kind: `flash/lastKind` when stored, otherwise the tab's target.~~ **Reversed by the user 2026-10-16
  (review F4):** the initial kind is always the tab's target (ADR-0055); a kind chosen in the tab is kept for the
  tab's lifetime, in memory only (also when the tab's target changes later); `flash/lastKind` and its registration
  are removed.
- Recent: an entry is added when a file is loaded or a catalog ID downloaded and the source fits the kind (not when a
  kind change makes a loaded source fit, review F7); 10 entries for all kinds together; × removes it from the stored
  list; the type shown is "HEX file" / "Zip archive" / "Catalog ID".
- "Erase application" (`erase(target, Code)`) is offered only where the profile's `defaultErase` is `Code` (CHESTER
  `app`); for the LTE MCU (`defaultErase` `All`) it is hidden and refused, as `hio chester lte erase` has no code-only
  variant (review F2). "Erase everything…" is `erase(target, All)`. The confirmations of erase everything and
  recover list the PIB on every MCU (review F1, ADR-0038).
- Maintenance actions show the progress card and a result card with [Done]; Cancel is offered except for Reset.
- `describeEraseAll()` / `describeRecover()` return the ConfirmDialog texts as a map (title, body, items, consequence,
  acceptText) instead of the sketch's string; `requestConfirmation()` / `confirm()` keep the flow in C++.
- Result: success [Open console] [Flash again] (back to the form, the file stays loaded) and a toast when this tab's
  Flash page is not on screen; cancel shows the text and [Flash again]; failure shows the ErrorPanel with Retry
  (flash), "Choose another file" (Firmware category) or Dismiss (maintenance), plus [Back]. Start and result are
  announced to screen readers.
- The page content is null-safe: the PageHost resets `vm` to null while a closed tab's page is torn down, and Loader
  items are deleted after the tab's view models.

### Open points for the user

Decided by the user 2026-10-16: (1) accepted as a recorded deviation (option a); (2) option (a), the log folder stays
in 0005's Console section; (3) D0 stays as noted; (4) fixed in the 0004 worktree (`v2/m3-gui`), comes in through a
later merge.

1. **Catalog download parsing (deviation from § Data flow, accepted 2026-10-16):** the plan says the downloaded bytes are parsed by the
   `FirmwareLoader`; the M2 `FirmwareCatalog` parses (and validates) the HEX itself inside `download()` and the cache
   lookup, on the GUI thread. Implemented as M2 provides it. Options: (a) accept (a CHESTER image of 1–2 MB of HEX
   text parses in tens of ms; the 32 MiB download cap bounds the worst case); (b) amend 0003/libs/firmware: a catalog
   option that returns the raw bytes, parsed by the loader; (c) move the catalog to its own thread with an event loop.
2. **"Log-file folder"** is listed in the Goals of this plan, but the table gives the log folder to 0005's Console
   section. Nothing was added here. Options: (a) leave it to 0005; (b) add the application log folder (hio logs) to
   About with [Open].
3. **D0:** 0004 is not formally Done (its HIL/manual checks are open).
4. **TSan flake in the Qt Quick test harness (0004, not caused by 0006):** under parallel load the probe scanner can
   log (`ProbeEnumerator::load`) while QtQuickTest destroys its `QuickTestResult` at process end; TSan reports Qt
   Test's uninstrumented log buffer (`tst_gui.tst_shell`, once `tst_already_running`). `tst_flash` / `tst_settings`
   avoid it by ending with a configured tab (no picker scan). Options: (a) the same in `tst_shell`; (b) stop the
   scanner before the test result is destroyed (test runner); (c) a `tsan-qml.supp` entry for libQt6QuickTest.

### Verification (2026-10-16)

- linux-debug: 788/788 tests pass; linux-asan: 788/788; linux-tsan: 775 tests (QNetworkAccessManager tests left out
  as in libs/firmware), all of 0006's pass repeatedly; 1 flaky 0004 test (open point 4).
- `linux-tidy`: zero findings; `all_qmllint`: clean; `check_translations`: up to date, 0 unfinished Czech entries;
  clang-format-21 clean; `tools/check-lfs.sh --untracked` passed.
- Screenshots (light/dark × EN/CS, not committed): Flash kind selection, app hex, catalog ID validator, wrong
  connector, LTE bundle with nRF Util missing, modem, progress, cancelling, cancelled, success, interrupted flash,
  maintenance, erase-all and recover confirmations; Settings (whole page, custom J-Link path + slow speed, 960 × 600).
- Open for Done: the HIL list above (with the user's confirmation per destructive step) and the manual checks.

## Open findings (independent review round 1, 2026-10-16)

| ID | Severity | Where | Finding | Suggested fix | Status (2026-10-16) |
|---|---|---|---|---|---|
| F1 | high | `flash_view_model.cpp` `describeErase` | LTE "Erase everything…" / "Recover…" confirmations omit the Product Information Block (listed only when `pibErasableWithUicr`), although ERASEALL/recover erase it on both MCUs; the CLI names it for both (ADR-0038) | List the PIB for erase-all and recover on every target; test both targets | **Fixed:** the PIB is listed on every MCU. Tests `FlashTest.ConfirmationTexts` (app, lte, modem kind), `tst_flash::test_lteMaintenance` |
| F2 | medium | `flash_view_model.cpp`, `MaintenanceSection.qml` | "Erase application" runs a code-only erase on the LTE MCU without confirmation, contrary to the plan's mapping (lte default: All per profile) and the CLI; Phase 0 is GO only for the app MCU | Follow the profile's `defaultErase` (user decision 2026-10-16) | **Fixed:** new property `eraseCodeAvailable` (`defaultErase == Code`); [Erase application] is hidden for the LTE MCU and `eraseCode()` refuses there, matching the mapping table and `hio chester lte erase` (erase-all only). Tests `FlashTest.TheLteMcuHasNoCodeOnlyErase`, `tst_flash::test_lteMaintenance` |
| F3 | medium | `KindCards.qml` | Re-clicking the selected kind card un-checks it visually (checkable button vs early return in `setKind`) | `checkable: false` or `ButtonGroup`; QML test | **Fixed:** `checkable: false` (checked follows the view model). Test `tst_flash::test_clickOnTheSelectedKindKeepsIt` (mouse and Space) |
| F4 | medium | `flash_view_model.cpp` | Initial kind comes from a global persisted `flash/lastKind`, overriding the tab's target (ADR-0055: default = tab's target, last kind per tab for the session) → app image can be flashed to the LTE MCU on variants where both are reachable | Per-tab in-memory last kind; default = tab's target (user decision 2026-10-16) | **Fixed:** `flash/lastKind` and its registration removed; default = tab's target, the tab's choice kept in memory. Tests `FlashTest.KindDefaultsFromTheTabTarget`, `FlashTest.TheLastKindIsRememberedPerTabInMemoryOnly`; `tst_flash` `init()` checks the default |
| F5 | medium | `device_session.cpp`, `DeviceTabViewModel::openSession` | A new nRF Util path from Settings does not reach an already open session; Flash is enabled but the modem update fails with `dependency.nrfutil_not_found` | Resolve the nRF Util locator options when the modem update starts | **Fixed (user decision 2026-10-16: option (a), ADR-0114):** `SessionOptions::nrfUtilProvider`, called on the owner thread by `updateModem()` / `flashLteBundle()`; the options travel in the operation command; empty provider = the snapshot of `open()`. `DeviceTabViewModel::openSession` passes a provider over its `SettingsStore`; the CLI is unchanged. Tests `Operations.NrfUtilProviderIsCalledPerOperationOnTheOwnerThread`, `Operations.WithoutAProviderTheSnapshotOfOpenIsUsed`, `FlashTest.AChangedNrfUtilPathReachesAnOpenSession` (a setting, not `HIO_NRFUTIL`; it fails without the provider) |
| F6 | low/medium | `FlashPage.qml` DropArea, `triggerErrorAction` | A drop while the result card is shown replaces the source; "Try again" then flashes another image | Disable drops on the result card; Retry re-runs the stored request | **Fixed:** the DropArea is enabled only on the form; Retry runs the stored flash request (image, MCU, options, name). Tests `FlashTest.RetryFlashesTheStoredRequestAfterASourceChange`, `tst_flash::test_flashShowsProgressAndResult` (drop area) |
| F7 | low/medium | `firmware_source_view_model.cpp` | Switching kind adds the loaded file to the new kind's Recent list | Add to Recent only on explicit load/download | **Fixed.** Test `FlashTest.AKindChangeDoesNotAddToRecent` |
| F8 | low | `firmware_loader.cpp` | A request dropped from the full queue never gets an answer (requester stuck in Loading/Checking) | Deliver a "dropped" outcome, or size the queue per (requester, kind) | **Fixed:** a dropped request is answered with `internal.queue_full` under its generation (results bounded by `maxResults` = 16). Test `LoaderTest.ARequestDroppedFromTheFullQueueIsAnswered` |
| F9 | low | `firmware_loader.cpp`, `nrfutil.cpp` | nRF Util version probes (up to 10 s each, not stoppable) block quit and HEX loading on the single loader thread | Stoppable probe (kill on stop) or separate worker | **Fixed (stoppable probe):** `nrfutil::checkVersions(executable, timeout, const std::stop_token& stop = {})` polls every 100 ms and kills the probe on stop (`cancelled`); the loader passes its stop token. HEX loading still waits behind a running probe (as decided: no second worker). Tests `NrfUtil.AVersionProbeStopsOnRequest`, `NrfUtil.AVersionProbeTimesOut`, `LoaderTest.ARequestDroppedFromTheFullQueueIsAnswered` (destructor < 5 s during a 30 s probe); fake nRF Util mode `slowversion` |
| F10 | low | `flash_view_model.cpp` | Dependency errors at operation time offer "Try again" instead of "Open settings" | `OpenSettings` for the Dependency category | **Fixed:** `errorAction` = OpenSettings for the Dependency category; `triggerErrorAction(OpenSettings)` returns to the form and emits `settingsRequested` (the page opens Settings). Test `FlashTest.ADependencyErrorOffersTheSettings` |
| F11 | low (plausible) | `flash_view_model.cpp` confirm | The confirmed maintenance action uses the target at confirm time, not the one shown in the dialog | Store the target with the pending action and refuse if it changed | **Fixed:** the pending confirmation stores the target and the profile; `confirm()` runs on that target and refuses (logged, nothing runs) when the kind's MCU or the profile changed. Test `FlashTest.AConfirmationIsRefusedWhenTheMcuChanged` |

Missing tests: session lost / tab closed mid-operation, Retry after a source change, queue overflow, nRF Util path change with an open
session, LTE confirmation texts with the PIB. Doc: `dependency_status.h` ownership comment is wrong.

**Status (2026-10-16):** added `FlashTest.ALostSessionEndsTheOperationAsFailed`,
`FlashTest.ClosingTheTabMidOperationCancelsAndCloses`, `FlashTest.RetryFlashesTheStoredRequestAfterASourceChange`,
`LoaderTest.ARequestDroppedFromTheFullQueueIsAnswered`, the LTE confirmation texts (C++ and QML) and
`FlashTest.AChangedNrfUtilPathReachesAnOpenSession` (F5). The `dependency_status.h` comment is corrected (a `std::unique_ptr` member of
`FirmwareServices`, no Qt parent).

### Question of the round-1 fixes (F5) — decided 2026-10-16: option (a), ADR-0114

Today `DeviceSession::open()` resolves `SessionOptions::nrfUtil` (nullopt → `defaultLocatorOptions()` with
`Settings::openDefault()`) once; the worker uses that snapshot for every later modem step. Options:

- (a) `SessionOptions` gets a provider `std::function<nrfutil::LocatorOptions()>`; `DeviceSession::updateModem()` /
  `flashLteBundle()` call it on the owner thread when the operation is requested and the options travel with the
  operation command to the worker (the provider never runs on the worker; the GUI passes the `FirmwareServices` /
  `SettingsStore` hook, the CLI keeps passing fixed options).
- (b) The operation request carries the options: `updateModem(target, zip, std::optional<nrfutil::LocatorOptions>)`
  and the same for `flashLteBundle` (nullopt = the session's options); the Flash page and the flash wizard pass them.
- (c) No API change: the default (nullopt) is resolved lazily on the worker when a modem step starts
  (`Settings::openDefault()` there). Smallest change, but the GUI's `SettingsStore` and the backend read the file
  independently (GUI tests use another settings file), and plan 0003 R9 ("resolved in `open()`") changes.

### Verification of the round-1 fixes (2026-10-16)

- After F5 / ADR-0114 (final run): linux-debug: 802 tests, 0 failed (12 HIL / corpus tests skipped); linux-asan:
  802, 0 failed; linux-tsan: 789 (the QNetworkAccessManager tests are left out as in libs/firmware), 0 failed; the new
  and changed tests (FlashTest, LoaderTest, NrfUtil, the two ADR-0114 Operations tests, tst_flash) repeated 5× under
  TSan and ASan without a failure. `FlashTest.AChangedNrfUtilPathReachesAnOpenSession` was checked to fail without
  the provider. The 0004 `tst_shell` TSan flake (open point 4) did not show up.
- `linux-tidy`: 0 findings; `all_qmllint`: clean; `update_translations`: 0 new texts; `check_translations`: up to
  date, 0 unfinished Czech entries; clang-format-21 clean; `tools/check-lfs.sh --untracked` passed.
- Screenshots re-taken (light/dark × EN/CS, not committed), incl. the new f15 (maintenance of the LTE MCU without
  "Erase application") and f16 (erase everything on the LTE MCU, with the PIB).
