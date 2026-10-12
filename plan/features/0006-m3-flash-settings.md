# 0006 — M3 flash and settings pages

- **Status:** Draft
- **Milestone:** M3 (plan 3 of 3, ADR-0059)
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0016, ADR-0025, ADR-0027, ADR-0036, ADR-0037, ADR-0038, ADR-0040, ADR-0047, ADR-0049,
  ADR-0054, ADR-0055, ADR-0057, ADR-0059, ADR-0060, ADR-0062
- **Depends on:** [0004](0004-m3-gui-shell.md) (shell, page-extension contract, `SettingsStore`), 0003 (M2 with the
  ADR-0057 / ADR-0062 amendments applied: operations with a target, `flashLteBundle`, `reset` as an operation)
- **Can run in parallel with:** [0005](0005-m3-console.md)
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

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
- Flash wizard and "Help me fix this" (M4), J-Link firmware update button (M4).
- PIB / UICR read in the GUI (CLI only in M2; candidate for M4 diagnostics).

## User experience

### Flash page

Per ADR-0055 (kind first) and ADR-0049 (progress, result, maintenance). The cards are:

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
  - [Erase application] (code only) — no confirmation.
  - [Erase everything…] (app all / lte erase) and [Recover…] — ConfirmDialog (ADR-0038), e.g. "Erase everything on
    the application MCU?" • Firmware (flash) • UICR configuration • **Product Information Block (serial number,
    hardware version …)** — "This cannot be undone." [Cancel] [Erase everything].
  - Maintenance acts on the MCU of the selected kind (application firmware → app; LTE / modem → lte). The section
    title names it: "Maintenance — application MCU".
- **Not connected is fine.** Flash calls `tab.ensureSession(kindTarget, attachRtt = false)` when the tab has no
  session. If the tab's console is attached to another MCU of the same probe, the operation carries its target
  (ADR-0057): RTT pauses, the probe switches MCU and runs the operation, then it returns and RTT re-attaches. The
  stepper then also shows "Switching MCU" and "Restoring console".

### Settings page (global, Ctrl+,)

Changes apply immediately (no OK/Cancel). Invalid input shows an inline error and is not saved.

| Section | Items | Owner |
|---|---|---|
| General | Language: System / English / Čeština (live) · Theme: System / Light / Dark (live) · Reopen tabs at start | 0006 |
| Console | Font size (8–24) · Show timestamps · Wrap long lines · Save console to log files (default on) · Log folder [Open] · Retention info "Last 50 files, max. 200 MB" | 0005 (section component) |
| J-Link | Library: Automatic (detected: `/opt/SEGGER/JLink/libjlinkarm.so` 8.10) / Custom path [Browse…] · Default speed (kHz) · RTT latency (ms) · Auto-reconnect · Reset on connect | 0006 |
| nRF Util | Path: Automatic (detected 7.x / "not found — needed only for LTE modem firmware") / Custom [Browse…] | 0006 |
| About | Version, build info, Qt, J-Link and nRF Util versions [Copy] · Licences (Qt LGPL, fonts OFL, Material Symbols Apache 2.0, miniz, replxx, …) · Links: documentation, firmware catalog, source | 0006 |

Read-only settings file (`settings.newer_schema`): a banner says "Settings were created by a newer version and are
read-only." and all controls are disabled.

### Shortcuts (ADR-0051)

Ctrl+Enter (Flash, scope `flash`), Ctrl+O (Browse…, scope `flash`), Ctrl+, (Settings, global — registered by 0004).

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

```cpp
class FlashViewModel : public QObject {
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
  `operationFinished`. `FlashViewModel` keeps the `OperationId` and ignores the events of other operations. On
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
| Custom J-Link path invalid | `dependency.jlink_load_failed` (existing) | shown inline in Settings | FirstRunSetup |
| Flash while another operation runs | `internal.busy` (existing) | Flash disabled, the bar says "Another operation is running" | — |

All backend errors from 0003 are shown through `ErrorPanel` with their texts. The Firmware category's primary
action is "Choose another file".

### Settings keys added (registered via `SettingsStore::registerKeys`, ADR-0025)

| Key | Type | Default |
|---|---|---|
| `flash/recent` | string (JSON list, max 10 entries: `{kind, source: path \| "catalog:<id>", time}`) | — |
| `flash/lastKind` | string `app` \| `lte` \| `modem` | from the tab's target |
| `jlink/libraryPath`, `nrfutil/path` | string (existing keys from M1/M2; edited here) | — |

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
- [ ] D0 Prerequisite check: 0004 is Done; 0003 implemented with the ADR-0057 / ADR-0062 amendments (otherwise stop
  and report)
- [ ] D1 `FirmwareLoader` worker + catalog download integration + `RecentSourcesModel`
- [ ] D2 `FlashViewModel` + kind cards, prerequisites, source card filtered by kind, kind-mismatch hints, options,
  sticky Flash bar
- [ ] D3 PhaseStepper wiring (`plannedPhases`, step/steps), cancel, result card, busy integration (`setBusy` /
  `clearBusy`, toast)
- [ ] D4 Maintenance section + ConfirmDialogs (ADR-0038)

Phase E — settings
- [ ] E1 `SettingsViewModel` + SettingsPage (General, J-Link, nRF Util, About, contributed sections; live language
  and theme; read-only mode) + dependency detection
- [ ] E2 Accessibility; Czech translation, `.ts` fresh; screenshots of the Flash and Settings pages in both themes and
  both languages

## Tests and verification

- **ViewModel unit tests (GTest, fake `DeviceSession` via the fake J-Link library and fake nRF Util from 0003):**
  - flash state machine (load → flash → progress → success / fail / cancel) for each kind, incl. the LTE bundle
    (step 1/2 → 2/2, modem failure);
  - cross-target flash from a tab whose console is on `app` (phases incl. SwitchingTarget / Restoring);
  - flash from a disconnected tab (`ensureSession(..., attachRtt = false)`);
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
