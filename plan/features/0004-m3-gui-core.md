# 0004 — M3 GUI core

- **Status:** Draft
- **Milestone:** M3
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0002, ADR-0003, ADR-0004, ADR-0018, ADR-0025, ADR-0027, ADR-0031, ADR-0037, ADR-0038,
  ADR-0040, **ADR-0043 – ADR-0057** (new; 0055–0057 from the mockup review)
- **Depends on:** 0001 (M0), 0002 (M1, amended by ADR-0053), 0003 (M2, amended in § Design / "Backend amendments")
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

## Context

M0–M2 deliver the backend (J-Link sessions, RTT console, flashing, catalog download) and the CLI. The GUI is still
the empty M0 window. The old app (branch `main`, QML) is the behavioural reference for the workflows users depend on.
These must be kept:

1. The shell and the log are visible side by side.
2. Command history (Up/Down, Ctrl+R) survives restarts.
3. Log search (next/previous) and filter.
4. Pause, clear, copy, batch files and opening the log file.
5. Flashing by catalog ID or a local `.hex`.

Its pain points must be fixed:

- A right-hand tool panel that changes per page.
- Search mode that takes over the view and pauses silently; search vs. filter is unclear.
- A manual detach before flashing, and no real progress.
- No settings, no light theme and no global shortcuts.
- A fixed 1024 × 680 minimum window size.
- Only one device.

## Goals / Non-goals

**Goals**
- Application shell (ADR-0043):
  - device tabs (≤ 8);
  - per-tab rail (Console, Flash), device bar and per-page toolbar;
  - global Settings and Help.
- Add-device flow with a live probe picker (ADR-0050) and per-tab J-Link options (ADR-0052).
- Console page:
  - shell + log split;
  - virtualised views (ADR-0045);
  - bottom Search | Filter bar with level chips, like the old app (ADR-0056, matching per ADR-0046);
  - Follow / Pause, clear, copy, save view, open log file / folder;
  - command history with Ctrl+R;
  - batch file runner;
  - reset / reconnect;
  - dropped-lines and warning banners.
- Flash page (ADR-0055, ADR-0049, ADR-0054, ADR-0057):
  - choose what to flash first (application / LTE / modem firmware), then file / catalog ID / recent, options, sticky Flash bar; phase progress and Cancel, result card;
  - maintenance actions (Reset, Erase, Erase all, Recover) with ADR-0038 confirmation.
- Settings page:
  - language (live), theme (live), console appearance;
  - J-Link library path and defaults, nRF Util path;
  - log-file folder; About and licences.
- Theme and components:
  - Material Dense + `Theme` singleton (ADR-0044);
  - light / dark / system theme (ADR-0047);
  - bundled fonts (ADR-0048);
  - shortcuts and F1 overlay (ADR-0051).
- Inline `ErrorPanel` for every `hio::Error`, toasts only for success/info, empty states that show the next step.
- EN + CS, accessibility per the design system § 6.

**Non-goals (later milestones)**
- Wizards and the working "Help me fix this" button (M4). In M3 the button is shown disabled with "Coming soon"
  whenever an error has a `wizard`.
- "Copy diagnostic report" (M4).
- J-Link firmware update button (M4).
- BLE page (M5), MCP server and consent UI (M6), update check (M7).
- Catalog browsing (open question 15).
- Detaching tabs into separate windows.
- User-remappable shortcuts.
- Console for the LTE target (as in M1/M2).

## User experience

Screens are drawn in the mockups (`plan/gui/mockups/`, light/dark and EN/CS toggles); this section is normative.

### Window and tabs

- Title "HARDWARIO Monitor"; minimum size 960 × 600, default 1280 × 800; geometry is remembered.
- **First start** opens one tab in the *Choose device* state.
- **Later starts** restore the tab list (profile, target, probe SN) **without connecting**; each restored tab shows
  "Disconnected — Connect" (setting `ui/restoreTabs`, default on).
- **Tab bar:** a state dot plus the title `CHESTER app · 683012345`. "+" (Ctrl+T) opens a new tab; it is disabled
  at 8 tabs with the tooltip "At most 8 devices can be open."
- **Closing a tab** (Ctrl+W, ×, middle-click) disconnects it.
  - If an operation is running, the user confirms first: "Flashing is in progress. Cancel it and close the tab?" with
    the buttons [Keep flashing] [Cancel and close].
  - Closing the last tab leaves one empty *Choose device* tab.
- **Quitting** while an operation runs asks the same question.

### Choose device (new tab)

- The probe list refreshes every 2 s while visible. Each row shows the J-Link model, the SN, the firmware date and an
  "In use (tab 2)" badge when another tab has it.
- Below the list are the device (CHESTER) and the target (Application MCU `app` / LTE modem MCU `lte`) pickers,
  plus **Connect**.
- Exactly one free probe → it is preselected.
- Selecting a probe that is in use switches to its tab.
- **No probes:** the empty state reads "No J-Link probe found — Connect the J-Link to USB and to the device.",
  followed by a check list:
  - ✓/✗ J-Link Software found (version).
  - "Is the USB cable connected?"
- **J-Link library missing:** an ErrorPanel shows `dependency.jlink_not_found` with "Open Settings" (path override).

### Device bar

`[Probe J-Link EDU Mini · 683012345] [Target: app ▾] State text … [Connect | Disconnect | Cancel] [⋯]`

- **Connection states:**
  - *Disconnected*: "Disconnected" with a Connect button.
  - *Connecting*: "Connecting…" with a spinner and Cancel (non-blocking open, ADR-0053).
  - *Connected*: "Connected · 3.30 V" with a Disconnect button.
  - *Disconnected with error*: an ErrorPanel at the top of the page, plus a state text that names the cause
    (e.g. "Target not powered").
  - *Auto-reconnecting*: "Reconnecting… (target not powered)".
- The ⋯ popover holds speed, RTT latency, auto-reconnect, reset on open and "Save console to log file" (ADR-0052),
  with the note "Changes apply on the next connect."
- The target picker is disabled while connected.

### Console page

- **Toolbar** (left → right):
  - Reset ▾ (Reset / Reset and halt) · Reconnect (F4) · Batch… (Ctrl+B) · Clear ▾ (Shell / Log / Both, Ctrl+K =
    focused view) · Save view… (Ctrl+S);
  - spacer;
  - Follow toggle · Pause (Ctrl+P) · ⋯ (Open log file, Open log folder, Show timestamps, Wrap lines, Swap panes,
    Stacked/side-by-side).
- **Split:** Shell (left) | Log (right), resizable; the ratio is remembered (`console/splitRatio`). Below 1000 px
  width the split stacks vertically.
- **Shell view:** echoed commands `> cmd` in accent colour, then device output. The input row `> [_____] [Send]` sits
  under the shell view.
  - Enter sends; Up/Down walk the history; Ctrl+R opens the history popup.
  - While disconnected or flashing the input is disabled with a placeholder: "Not connected" / "Flashing — the
    console resumes automatically".
- **Log view:** columns time (optional), level tag (D I W E, coloured) and text.
- **Search bar at the bottom of the log** (ADR-0056, like the old app), from left to right:
  - a **Search | Filter** switch;
  - the field with Aa and .\* toggles;
  - in Search mode: "3 / 128", ↑ ↓, ×; in Filter mode: "Showing X of Y lines", Clear filter;
  - the level chips D I W E.

  Ctrl+F opens it in Search mode and Ctrl+L in Filter mode. An active filter stays visible as a chip while the bar is in
  Search mode.
- **Shell search:** Ctrl+F in the shell shows a Search-only bar directly above the command input. Esc hides it.
- **Banners:**
  - "312 lines were dropped because the device sent data faster than it could be displayed." [Dismiss]
  - `probe.firmware_outdated` warning: "The J-Link firmware is outdated." [Details] (the update button comes in M4).
  - Console file write failure (`io.write_failed`): "The console log file could not be written; logging to file is
    off for this session."
- **Batch** opens a file dialog (`.txt`, any).
  - It sends one non-empty, non-`#` line every 200 ms; the progress chip shows "Batch 12 / 40 [Stop]".
  - It stops on disconnect.
  - Limits: 1 MiB file, 10 000 commands, otherwise an error.
- **Empty states:**
  - Not connected and no lines: "Not connected. Connect to see the device console." [Connect]
  - Connected and nothing yet: "Waiting for data from the device…"

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
   - Modem: the note "Modem update takes 1–3 minutes. The console is disconnected meanwhile."
3. **Options** — "Halt the CPU after flashing" (not for modem), and "Allow writing UICR" with a warning, only when the
   image has UICR records (ADR-0036).
4. **Sticky bottom bar** — a summary ("Application firmware · app.hex") and **[Flash]** (Ctrl+Enter). While Flash is
   disabled, the bar says why (e.g. "Choose a firmware file", "nRF Util was not found").

- **Running:** the cards are replaced by the PhaseProgress (phases from `OperationProgress::phases`), the percentage,
  bytes, the elapsed time and [Cancel]. Cancel shows "Cancelling…" until the operation ends.
  - The Console rail item shows a busy badge.
  - The device bar shows "Flashing…".
- **Result:**
  - Success: "Firmware flashed and verified in 14 s." [Open console] [Flash again]. A toast is also shown when the user
    is on another tab.
  - Failure: an ErrorPanel (e.g. `firmware.verify_failed`) with [Try again].
  - Cancel: "Flashing was cancelled. The device may not start until you flash it again." [Flash again].
- **Maintenance** (collapsible, collapsed by default):
  - [Reset] [Reset and halt] — no confirmation.
  - [Erase application] (code only) — no confirmation.
  - [Erase everything…] (app all / lte erase) and [Recover…] — ConfirmDialog (ADR-0038), e.g. "Erase everything on
    the application MCU?" • Firmware (flash) • UICR configuration • **Product Information Block (serial number,
    hardware version …)** — "This cannot be undone." [Cancel] [Erase everything].
  - Maintenance acts on the MCU of the selected kind (application firmware → app; LTE / modem → lte). The section
    title names it: "Maintenance — application MCU".
- **Not connected is fine.** Flash opens a session for the kind's target with `attachRtt = false` when the tab has no
  session. If the tab's console is attached to another MCU of the same probe, the operation carries its target
  (ADR-0057): RTT pauses, the probe switches MCU and runs the operation, then it returns and RTT re-attaches. The
  stepper then also shows "Switching MCU" and "Restoring console". The device bar shows the probe as busy.

### Settings page (global, Ctrl+,)

Changes apply immediately (no OK/Cancel). Invalid input shows an inline error and is not saved.

| Section | Items |
|---|---|
| General | Language: System / English / Čeština (live) · Theme: System / Light / Dark (live) · Reopen tabs at start |
| Console | Font size (8–24) · Show timestamps · Wrap long lines · Save console to log files (default on) · Log folder [Open] · Retention info "Last 50 files, max. 200 MB" |
| J-Link | Library: Automatic (detected: `/opt/SEGGER/JLink/libjlinkarm.so` 8.10) / Custom path [Browse…] · Default speed (kHz) · RTT latency (ms) · Auto-reconnect · Reset on connect |
| nRF Util | Path: Automatic (detected 7.x / "not found — needed only for LTE modem firmware") / Custom [Browse…] |
| About | Version, build info, Qt, J-Link and nRF Util versions [Copy] · Licences (Qt LGPL, fonts OFL, icons Apache 2.0, miniz, …) · Links: documentation, firmware catalog, source |

Read-only settings file (`settings.newer_schema`): a banner says "Settings were created by a newer version and are
read-only." and all controls are disabled.

### Shortcuts

ADR-0051 map; the F1 overlay lists them, and the tooltips show them ("Find (Ctrl+F)").

### Texts

English source texts as above. Czech is translated in Qt Linguist as part of done (ADR-0027). The mockups carry draft
Czech texts for layout checks.

## CLI

**None.** M3 adds no backend capability. Every GUI action maps to existing CLI commands of M1/M2:
- console → `chester app console`;
- flash / erase / recover / reset → `chester app|lte …`;
- probe list → `jlink list`.

GUI-only concerns (tabs, theme, find/filter, batch pacing in the view) have no CLI equivalent. The CLI console
already has its own batch/history (ADR-0033).

## Design

### Modules

All new code lives in `app/gui` (QtQuick allowed). No change to `libs/*` except the backend amendments below.

```
app/gui/
  src/main.cpp                      (M0) + style setup, font loading, ProbeScanner/registry ownership
  src/viewmodels/
    app_view_model.*                (M0) + theme, tabs, shortcuts overlay state
    device_tab_list_model.*         QAbstractListModel of DeviceTabViewModel
    device_tab_view_model.*         owns one tab's state + child VMs (Qt parent ownership)
    probe_picker_view_model.*
    connection_view_model.*         device bar
    console_view_model.*            shell + log channels, input, history, batch
    console_line_model.*            QAbstractListModel over ConsoleLog (ADR-0045)
    find_filter_engine.*            time-sliced search/filter over a channel (pure logic, unit-tested)
    flash_view_model.*
    settings_view_model.*
    error_info.*                    Q_GADGET wrapper of hio::Error for QML
  src/workers/
    probe_scanner.*                 owned std::jthread, periodic listProbes (ADR-0050)
    firmware_loader.*               owned worker: HEX/zip parse + LTE detection off the GUI thread
    batch_runner.*                  QTimer-driven, bounded, owned by ConsoleViewModel
  qml/
    Main.qml  Theme.qml(singleton)  Shortcuts.qml
    shell/  DeviceTabBar.qml NavRail.qml DeviceBar.qml AdvancedPopover.qml
    pages/  ChooseDevicePage.qml ConsolePage.qml FlashPage.qml SettingsPage.qml
    console/ ConsoleView.qml ConsoleRow.qml ShellInput.qml HistoryPopup.qml ConsoleSearchBar.qml
    flash/  KindCards.qml SourceCard.qml OptionsCard.qml FlashBar.qml PhaseProgress.qml ResultCard.qml MaintenanceSection.qml
    components/ HButton.qml HToolButton.qml ErrorPanel.qml WarningBanner.qml Toast.qml ConfirmDialog.qml
                EmptyState.qml ShortcutOverlay.qml
  assets/fonts/ (LFS)  assets/icons/*.svg  qtquickcontrols2.conf
  tests/ tst_*.qml (Qt Quick Test)  viewmodels/*_test.cpp (GTest)
```

### ViewModel interfaces (sketch, `hio::gui`)

```cpp
class AppViewModel : public QObject {          // QML_SINGLETON (M0) — additions
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)   // "system"|"light"|"dark"
    Q_PROPERTY(DeviceTabListModel* tabs READ tabs CONSTANT)
    Q_PROPERTY(int currentTab READ currentTab WRITE setCurrentTab NOTIFY currentTabChanged)
    Q_INVOKABLE void newTab();  Q_INVOKABLE void closeTab(int index, bool force = false);
    Q_INVOKABLE bool canQuit();  // false + signal quitConfirmationNeeded() while an operation runs
};

class DeviceTabViewModel : public QObject {    // QML_UNCREATABLE; parent = DeviceTabListModel
    Q_PROPERTY(State state READ state NOTIFY stateChanged)   // ChoosingDevice, Disconnected, Connecting, Connected, Busy
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(ProbePickerViewModel* picker ...) Q_PROPERTY(ConnectionViewModel* connection ...)
    Q_PROPERTY(ConsoleViewModel* console ...)    Q_PROPERTY(FlashViewModel* flash ...)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)   // Console=0, Flash=1
    // owns std::unique_ptr<hio::device::DeviceSession> session_ (nullptr while choosing / disconnected)
};

class ConnectionViewModel : public QObject {
    Q_PROPERTY(QString probeLabel ...) Q_PROPERTY(QString targetId ...) Q_PROPERTY(QString stateText ...)
    Q_PROPERTY(double targetVolts ...) Q_PROPERTY(hio::gui::ErrorInfo error ...)   // invalid when none
    Q_PROPERTY(int speedKhz ...) Q_PROPERTY(int latencyMs ...) Q_PROPERTY(bool autoReconnect ...) Q_PROPERTY(bool resetOnOpen ...)
    Q_INVOKABLE void connectDevice(); Q_INVOKABLE void disconnectDevice(); Q_INVOKABLE void reconnect();
    Q_INVOKABLE void reset(bool halt);
};

class ConsoleViewModel : public QObject {
    Q_PROPERTY(ConsoleLineModel* shell ...) Q_PROPERTY(ConsoleLineModel* log ...)
    Q_PROPERTY(bool inputEnabled ...) Q_PROPERTY(QString inputDisabledReason ...)
    Q_PROPERTY(quint64 droppedLines ...) Q_PROPERTY(bool batchRunning ...) Q_PROPERTY(int batchDone ...) Q_PROPERTY(int batchTotal ...)
    Q_INVOKABLE void send(const QString& line);
    Q_INVOKABLE QString historyPrevious(); Q_INVOKABLE QString historyNext();
    Q_INVOKABLE QStringList historySearch(const QString& text, int max = 50);
    Q_INVOKABLE void runBatch(const QUrl& file); Q_INVOKABLE void stopBatch();
    Q_INVOKABLE void clear(int channelMask); Q_INVOKABLE void saveView(int channel, const QUrl& file);
    Q_INVOKABLE void openLogFile(); Q_INVOKABLE void openLogFolder();
};

class ConsoleLineModel : public QAbstractListModel {   // one channel, filtered view
    enum Roles { TextRole = Qt::UserRole + 1, LevelRole, DirectionRole, TimeRole, SeqRole, MatchesRole, SelectedRole };
    Q_PROPERTY(bool following ...) Q_PROPERTY(bool paused ...) Q_PROPERTY(int newLinesWhileNotFollowing ...)
    Q_PROPERTY(int totalLines ...) Q_PROPERTY(int visibleLines ...)
    // filter
    Q_PROPERTY(QString filterText ...) Q_PROPERTY(bool filterRegex ...) Q_PROPERTY(bool filterCase ...)
    Q_PROPERTY(int levelMask ...) Q_PROPERTY(QString filterError ...)
    // find
    Q_PROPERTY(QString findText ...) Q_PROPERTY(bool findRegex ...) Q_PROPERTY(bool findCase ...)
    Q_PROPERTY(int matchCount ...) Q_PROPERTY(int currentMatch ...) Q_PROPERTY(int currentMatchRow ...)
    Q_PROPERTY(QString findError ...) Q_PROPERTY(bool searching ...)
    Q_INVOKABLE void findNext(); Q_INVOKABLE void findPrevious();
    Q_INVOKABLE void select(int row, bool extend); Q_INVOKABLE void selectAll(); Q_INVOKABLE void copySelection(bool withTime);
};

class FlashViewModel : public QObject {
    Q_PROPERTY(SourceState sourceState ...)  // None, Loading, Downloading, Ready, Invalid
    Q_PROPERTY(QString sourceSummary ...) Q_PROPERTY(qreal downloadProgress ...) Q_PROPERTY(bool hasUicr ...)
    Q_PROPERTY(Kind kind READ kind WRITE setKind NOTIFY kindChanged)   // AppFirmware, LteFirmware, ModemFirmware
    Q_PROPERTY(QString kindHint ...) Q_PROPERTY(hio::gui::ErrorInfo prerequisite ...)   // e.g. nRF Util missing
    Q_PROPERTY(QString targetId ...)        // derived from kind Q_PROPERTY(bool halt ...) Q_PROPERTY(bool allowUicr ...)
    Q_PROPERTY(bool canFlash ...) Q_PROPERTY(QString cannotFlashReason ...)
    Q_PROPERTY(OpState opState ...)          // Idle, Running, Cancelling, Succeeded, Failed, Cancelled
    Q_PROPERTY(QVariantList phases ...) Q_PROPERTY(int currentPhase ...) Q_PROPERTY(qreal progress ...)
    Q_PROPERTY(QString progressText ...) Q_PROPERTY(hio::gui::ErrorInfo error ...)
    Q_PROPERTY(QAbstractListModel* recent ...)
    Q_INVOKABLE void loadFile(const QUrl&); Q_INVOKABLE void loadCatalogId(const QString&);
    Q_INVOKABLE void flash(); Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset(bool halt); Q_INVOKABLE void eraseCode();
    Q_INVOKABLE QString describeEraseAll(); Q_INVOKABLE void eraseAll();   // QML shows ConfirmDialog first
    Q_INVOKABLE QString describeRecover();  Q_INVOKABLE void recover();
};

struct ErrorInfo {                             // Q_GADGET, value type `errorInfo`
    Q_PROPERTY(bool valid ...) Q_PROPERTY(QString code ...) Q_PROPERTY(QString message ...) Q_PROPERTY(QString cause ...)
    Q_PROPERTY(QString suggestion ...) Q_PROPERTY(QString detail ...) Q_PROPERTY(QString wizard ...)   // kebab id or ""
};
```

`SettingsViewModel` exposes one typed property per Settings item. Writes go through `Settings` (ADR-0025) and are
validated.

### Data flow and threading

- **GUI main thread:** all ViewModels, `DeviceSession` façades, `SessionRegistry`, `ConsoleLog`, `CommandHistory`
  and `FirmwareCatalog` (as defined in M1/M2).
- **Session workers** are owned by each `DeviceSession` (ADR-0031). The GUI never calls the JLink library.
- **`ProbeScanner`:**
  - It is one owned `std::jthread` for the application, owned by `AppViewModel`.
  - It runs `listProbes` every 2 s while any picker is visible.
  - Results arrive by queued `QMetaObject::invokeMethod`.
  - Its stop token is honoured; it is joined on destruction.
- **`FirmwareLoader`:**
  - It is one owned `std::jthread` per application, with a bounded queue of capacity 4; the newest request replaces
    older pending ones.
  - It parses HEX/zip and classifies LTE input. The result is `std::shared_ptr<const FirmwareImage>` (immutable) or an
    `hio::Error`, delivered by a queued call.
  - This implements the "GUI will move parsing to a worker" note of 0003.
- **`linesReceived` batches** are appended by `ConsoleLineModel`, which coalesces UI updates to one per 50 ms
  (QTimer). Eviction from `ConsoleLog` maps to row removal.
- **Find/filter** run time-sliced on the GUI thread: 5 000 lines per slice, re-queued with
  `QTimer::singleShot(0)`. A new query cancels the old one by bumping its generation counter.
- **Command history:**
  - There is one `CommandHistory` per (profile, target), shared by all tabs with that pair and owned by
    `AppViewModel`.
  - The M1 file path and limits are unchanged; there is no concurrent writing of one history file.
- **Batch runner:** a `QTimer` (200 ms) on the GUI thread calls `session.sendLine`. Queue-full (`internal.queue_full`)
  pauses the batch and retries on the next tick.

### Backend amendments (to be applied to 0002 / 0003 before their implementation)

1. **ADR-0053:** non-blocking `DeviceSession::open()` plus the CLI helper `waitUntilConnected()`.
2. **Concrete operation types** (0003 left them undefined):

   ```cpp
   enum class OperationKind { Flash, EraseCode, EraseAll, Recover, Modem, Reset, ReadMemory };
   struct OperationProgress {
       OperationKind kind;
       std::span<const ProgramPhase> plannedPhases;   // static per kind+target, for the stepper
       ProgramPhase phase;
       std::uint64_t done, total;                     // total 0 = indeterminate
       std::chrono::milliseconds elapsed;
   };
   struct OperationResult {
       OperationKind kind;
       Status status;                                 // error incl. code/wizard; cancelled → `internal.cancelled`
       bool cancelled;
       std::chrono::milliseconds duration;
       std::optional<ChipInfo> chip;                  // identified part (shown in the result card)
       std::vector<std::byte> data;                   // ReadMemory only (≤ 64 KiB)
   };
   ```

   New error code `internal.cancelled` ("The operation was cancelled.", no wizard). The CLI already maps Ctrl+C to
   exit 130.
3. **ADR-0057:** every operation (`flash`, `erase`, `recover`, `reset`, `readMemory`, `updateModem`) takes
   `std::string_view targetId`. The worker pauses RTT, switches the probe to that MCU, runs the operation, then
   switches back and re-attaches RTT (also on failure / cancel). New `ProgramPhase` values: `SwitchingTarget`,
   `Restoring`.
4. **`FlashOptions`** stays as is. `DeviceSession::flash()` must also work when the session was opened with
   `attachRtt = false`, which the GUI uses for a tab that only flashes.

### Error cases (GUI-originated)

| Situation | Code | Message / cause / suggestion | Wizard |
|---|---|---|---|
| Batch file unreadable | `io.read_failed` | "The batch file could not be read." / missing or no permission / "Choose another file." | — |
| Batch file > 1 MiB or > 10 000 commands | `usage.invalid_argument` | "The batch file is too large (max. %1 commands)." / — / "Split it into smaller files." | — |
| Invalid catalog ID | (inline validator, not an error) | "The ID has 32 characters 0–9 a–f." | — |
| Save view / settings path not writable | `io.write_failed` | "The file could not be saved." / folder not writable / "Choose another folder." | — |
| Custom J-Link path invalid | `dependency.jlink_load_failed` (existing) | shown inline in Settings | FirstRunSetup |
| 9th tab | `internal.session_limit` (existing) | prevented by disabling "+" | — |

All backend errors from 0002/0003 are shown through `ErrorPanel` with their texts. The primary action per category
is:
- Probe / Target → Reconnect;
- Firmware → Choose another file;
- Dependency → Open Settings;
- others → Retry or Dismiss.

### Settings keys added (ADR-0025)

| Key | Type | Default |
|---|---|---|
| `ui/theme` | string | `system` |
| `ui/windowGeometry` | string (base64) | — |
| `ui/restoreTabs` | bool | `true` |
| `ui/tabs` | string (JSON list of `{profile,target,sn}`, max 8) | — |
| `console/fontSize` | int 8–24 | 10 |
| `console/showTimestamps` | bool | `false` |
| `console/wrapLines` | bool | `false` |
| `console/splitRatio` | int 20–80 (%) | 50 |
| `console/logToFile` | bool | `true` |
| `flash/recent` | string (JSON list, max 10 entries; a path or `catalog:<id>`) | — |
| `jlink/speedKhz`, `rtt/latencyMs`, `rtt/autoReconnect`, `rtt/resetOnOpen` | int/int/bool/bool | profile values |
| `probes/<sn>/{speedKhz,latencyMs,autoReconnect,resetOnOpen}` | per probe, LRU max 32 | — |

The `schemaVersion` bump and its migration (no-op for new keys) are documented.

### Limits

| Item | Limit |
|---|---|
| Device tabs | ≤ 8 (registry) |
| Lines per channel | 100 000 (backend); the model mirrors it |
| Find/filter slice | 5 000 lines |
| Model UI updates | ≤ 20/s |
| Copy | ≤ 100 000 lines / 16 MiB |
| Displayed line | ≤ 4 KiB |
| Batch file | ≤ 1 MiB / 10 000 commands, 200 ms pacing |
| Recent sources | 10 |
| Remembered probes | 32 |
| Toasts | 3 |
| FirmwareLoader queue | 4 |
| History popup | 50 results |

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Window organisation | left rail + device bar / top tabs / console-centric | Left rail + device bar | ADR-0043 |
| Devices per window | one / device tabs | Device tabs (≤ 8) | ADR-0043 |
| Tabs vs rail | device tabs on top, rail per tab / rail on top, tabs inside | Device tabs on top, rail per tab | ADR-0043 |
| Qt Quick Controls style | Basic + own / Material / Fusion-native | Material (Dense) + Theme singleton | ADR-0044 |
| Console rendering | ListView + C++ model / TextEdit | Virtualised ListView | ADR-0045 |
| Find / filter | find bar + filter / combined field | Find bar + separate filter (matching, Follow) | ADR-0046 |
| Search position (review) | bottom find, filter top / one bottom bar with Search \| Filter / both rows at bottom | One bottom bar like the old app | ADR-0056 |
| Themes | light+dark+system / dark only | Light + Dark + System; palette proposed | ADR-0047 |
| Fonts | Inter + JetBrains Mono / system / Inter + Ubuntu Mono | Inter + JetBrains Mono (LFS) | ADR-0048 |
| Flash page | single form / wizard | Single form + in-place progress | ADR-0049 |
| Add device | picker in new tab / dialog | Picker in new tab, 2 s scan | ADR-0050 |
| Shortcuts | fixed map / remappable | Fixed map + F1 overlay | ADR-0051 |
| J-Link options | Advanced popover / settings only | Advanced popover, per-probe memory | ADR-0052 |
| Blocking `open()` | non-blocking open / GUI opener thread | Non-blocking open (amends 0002) | ADR-0053 |
| Catalog in GUI | ID only / browser now | ID field + recent + web link | ADR-0054 |
| Flash page first step (review) | firmware kind cards / target MCU / operation first | Kind cards: app / LTE / modem | ADR-0055 |
| Flashing another MCU than the console's | operation carries target / retarget tab / second session | Operation carries its target | ADR-0057 |
| Icons | Material Symbols SVG subset (Apache 2.0) / other set | *to confirm in review* | — |
| Restore tabs at start (not connected) | yes / no | *to confirm in review* (proposed: yes, setting) | — |
| Responsive breakpoints (rail collapse < 1100 px, stacked split < 1000 px, min 960 × 600) | — | *to confirm in review* | — |

## Implementation steps

Phase A — foundation
- [ ] A1 Backend amendments landed in 0002/0003 (ADR-0053, ADR-0057, operation types, `internal.cancelled`) — prerequisite
- [ ] A2 Material Dense style setup, `qtquickcontrols2.conf`, `Theme.qml` tokens (both themes), font loading (LFS + licences), icon subset
- [ ] A3 Components: HButton, HToolButton, ErrorPanel, WarningBanner, Toast, ConfirmDialog, EmptyState, ShortcutOverlay + Qt Quick Tests
- [ ] A4 `ErrorInfo` gadget + mapping of category → primary action

Phase B — shell
- [ ] B1 `DeviceTabListModel`, `DeviceTabViewModel`, tab bar, rail, page switching, tab restore, close/quit confirmation
- [ ] B2 `ProbeScanner` worker + `ProbePickerViewModel` + ChooseDevicePage (empty / missing library states)
- [ ] B3 `ConnectionViewModel` + DeviceBar + Advanced popover + per-probe settings

Phase C — console
- [ ] C1 `ConsoleLineModel` over `ConsoleLog` (coalesced append, eviction, follow / new-lines counter, pause) + GTest
- [ ] C2 `FindFilterEngine` (plain/regex/case, level mask, time slicing, cancellation) + GTest on 100 000 lines
- [ ] C3 ConsoleView, ConsoleRow, selection + copy, ConsoleSearchBar (log: Search | Filter + levels; shell: Search only), save view
- [ ] C4 ShellInput, history (shared per profile/target), HistoryPopup, BatchRunner, banners (dropped, warnings)

Phase D — flash
- [ ] D1 `FirmwareLoader` worker + catalog download integration + recent list
- [ ] D2 FlashViewModel + kind cards, prerequisites, source card filtered by kind, kind-mismatch hints, options, sticky Flash bar
- [ ] D3 PhaseProgress, cancel, result card, console busy integration
- [ ] D4 Maintenance section + ConfirmDialogs (ADR-0038)

Phase E — settings & polish
- [ ] E1 SettingsViewModel + SettingsPage (all sections, live language/theme, read-only mode)
- [ ] E2 Shortcuts (ADR-0051) + duplicate test + F1 overlay
- [ ] E3 Responsive layout (breakpoints), accessibility names/announcements, reduced motion
- [ ] E4 Czech translation complete, `.ts` fresh; screenshots of every page in both themes and both languages

## Tests and verification

- **ViewModel unit tests (GTest, fake `DeviceSession` via the fake J-Link library from 0002/0003):**
  - tab lifecycle and limits;
  - restore without connect;
  - connection state text for every `ConnectionEvent`;
  - console append / eviction / follow counters;
  - find/filter correctness and time slicing (100 000 lines, each slice < 8 ms in Release);
  - invalid regex;
  - copy limits;
  - batch pacing and queue-full;
  - flash state machine (load → flash → progress → success / fail / cancel);
  - confirmation description texts;
  - settings validation and read-only mode.
- **Qt Quick Tests:**
  - every component in light and dark;
  - ErrorPanel rendering of all fields and the disabled wizard button;
  - ConsoleSearchBar keyboard behaviour (Ctrl+F / Ctrl+L, Enter / Shift+Enter, Esc twice, mode switch keeps queries);
  - shortcut map has no duplicates;
  - layout at 960 × 600 in Czech has no overlapping or clipped buttons (geometry assertions).
- **`qmllint`** (`all_qmllint`) is clean; no logic in QML (review checklist: no JS beyond bindings / one-line handlers).
- **Performance (fake library, 5 000 lines/s into both channels for 10 min):**
  - the GUI stays responsive (< 16 ms frames on the reference laptop);
  - RSS stays bounded (< 400 MB);
  - dropped-lines banner when forced.
- **Sanitizers:** ASan+UBSan and TSan builds run the ViewModel tests and a scripted GUI smoke test (offscreen
  platform).
- **HIL (CHESTER + J-Link):**
  - two tabs with two probes;
  - console and flash in one tab while the other streams;
  - unplug/replug power and auto-reconnect;
  - flash app hex, LTE bundle, cancel mid-flash;
  - erase all + recover with confirmations.
- **Manual on Linux, Windows, macOS (by the user, before Done):**
  - all mockup scenarios;
  - high-DPI 150 %;
  - keyboard-only walkthrough;
  - screen reader spot check (Orca / Narrator / VoiceOver);
  - live language and theme switching.

## Risks and open points

- **Material Dense density** may still be too roomy for a console tool. The wrapper components allow tightening
  paddings without changing the style.
- **Row-based selection** (ADR-0045) differs from free text selection. If users complain, a selectable text overlay for
  a row range can be added later.
- **Time-sliced search on the GUI thread** is bounded per slice but adds load during heavy streaming. Moving it to a
  worker over a snapshot is a fallback (needs an ADR).
- **Shared history per profile/target** across tabs: commands from two devices of the same type are interleaved.
  This is accepted for M3; per-probe history is a possible later setting.
- **Backend amendments (A1)** must land before M1/M2 implementation finishes. Otherwise M3 starts with a refactor.
- **The palette** is a proposal (no brand guide). Colour changes after review only update the design system.
