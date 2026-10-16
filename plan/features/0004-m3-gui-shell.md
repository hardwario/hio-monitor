# 0004 — M3 GUI shell: theme, components, device tabs, probe picker, device bar

- **Status:** Draft
- **Milestone:** M3 (plan 1 of 3, ADR-0059; followed by [0005](0005-m3-console.md) and [0006](0006-m3-flash-settings.md))
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0002, ADR-0003, ADR-0004, ADR-0018, ADR-0025, ADR-0027, ADR-0031, ADR-0043, ADR-0044,
  ADR-0047, ADR-0048, ADR-0050, ADR-0051, ADR-0052, ADR-0053, ADR-0058, ADR-0059, ADR-0060, ADR-0061, ADR-0063
- **Depends on:** 0001 (M0). 0002 (M1, with the ADR-0053 / ADR-0058 amendments applied) for real sessions; until
  M1 lands, the shell runs against the fake J-Link library.
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

> **Pending amendment from M4 ([0007](0007-m4-wizard-framework-diagnostics.md), [0009](0009-m4-connect-and-troubleshooting.md),
> [0010](0010-m4-flash-wizard.md); applied when they are approved — then this plan needs re-approval):** components
> WizardDialog, WizardStepList, CheckList, CommandBlock, Illustration, HelpMenu (ADR-0066); the ErrorPanel's
> "Help me fix this" is shown only for registered wizards and opens them; the rail's Help item opens the Help menu
> (F1 still opens the overlay); `DeviceTabViewModel::openWizard(id)` and
> `ensureSession(targetId, attachRtt, connectTarget = true)`; the Choose-device empty state's disabled button
> becomes [Guide me] (Connect wizard) plus the link "Flash firmware with a guide".

> **Pending amendment from M5 ([0012](0012-m5-ble-gui-pairing.md), ADR-0081, ADR-0083; applied when 0012 is approved
> — then this plan needs re-approval):** the Choose-device page gets a J-Link | Bluetooth switch and a BLE device
> list; `DeviceTabViewModel` gains `transport`, `consoleSession()`, `openBle()`, `bleSession()`; BLE tabs have a BLE
> device bar and title; `ui/tabs` entries gain `transport`; settings `ui/lastTransport`, `ble/*`.


> **Pending amendment from M6 (ADR-0105, specified in [0014](0014-m6-mcp-gui.md)):** an "MCP" indicator on the rail
> above Settings; an "AI" badge on tabs opened by an AI assistant (not restored); busy page id `ai` for AI
> operations; `AppViewModel::openTab` overload for programmatic opening.
## Context

M0–M2 deliver the backend (J-Link sessions, RTT console, flashing, catalog download) and the CLI. The GUI is still
the empty M0 window. The old app (branch `main`, QML) is the behavioural reference for the workflows users depend on,
and it has pain points that M3 fixes:
- a right-hand tool panel that changes per page;
- no settings, no light theme and no global shortcuts;
- a fixed 1024 × 680 minimum window size;
- only one device.

M3 is split into three plans (ADR-0059). **This plan builds the application shell** that the console page (0005) and
the flash and settings pages (0006) plug into:
- the visual foundation;
- the device tabs;
- the add-device flow;
- the device bar;
- single-instance handling;
- the shortcut framework;
- the **page-extension contract**.

## Goals / Non-goals

**Goals**
- Visual foundation:
  - Material Dense + `Theme` singleton (ADR-0044);
  - light / dark / system theme (ADR-0047);
  - bundled fonts (ADR-0048);
  - Material Symbols icon subset (ADR-0060);
  - base components (HButton, HToolButton, ErrorPanel, WarningBanner, Toast, ConfirmDialog, EmptyState,
    ShortcutOverlay, PhaseStepper).
- `ErrorInfo` gadget: every `hio::Error` is shown inline in an `ErrorPanel` with the category's primary action.
  Toasts are only for success and info.
- Application shell (ADR-0043, ADR-0061):
  - window and geometry;
  - device tabs (≤ 8) with restore (Disconnected);
  - per-tab rail and page area;
  - global Settings and Help entry points;
  - responsive rail collapse.
- Single GUI instance (ADR-0058, GUI side).
- Add-device flow: a live probe picker (ADR-0050) that shows probes used by another tab or by another process.
- Device bar: connection states (non-blocking open, ADR-0053) and the per-tab J-Link options popover with
  per-probe memory (ADR-0052).
- Shortcut registry + F1 overlay (ADR-0051).
- Page-extension contract for 0005/0006 (§ Design), with a placeholder page so this plan is testable on its own.
- EN + CS for all shell texts, and accessibility per the design system § 6.

**Non-goals**
- Console page (0005), Flash and Settings pages (0006). Until 0006 lands, "Open Settings" opens a placeholder page
  that shows the J-Link library path read-only.
- Wizards and the working "Help me fix this" button (M4). In M3 the button is shown disabled with "Coming soon"
  whenever an error has a `wizard`.
- J-Link firmware update button (M4), BLE page (M5), MCP server and consent UI (M6), update check (M7).
- Detaching tabs into separate windows, user-remappable shortcuts, several GUI instances.

## User experience

Screens are drawn in the mockups (`plan/gui/mockups/`, with light/dark and EN/CS toggles). This section is normative.

### Start and single instance (ADR-0058)

- Launching the app while it already runs brings the existing window to the front. The second process exits.
- If the running instance does not answer within 2 s, a dialog says: "HARDWARIO Monitor is already running but does
  not respond." [Quit] [Start anyway].

### Window and tabs (ADR-0043, ADR-0061)

- Title "HARDWARIO Monitor". The minimum size is 960 × 600 and the default is 1280 × 800. The geometry is remembered.
- **First start** opens one tab in the *Choose device* state.
- **Later starts** restore the tab list (profile, target, probe SN) **without connecting**. Each restored tab shows
  "Disconnected — Connect" (setting `ui/restoreTabs`, default on). A restored tab whose probe is missing offers
  "Choose another probe" next to the error.
- **Tab bar:** a state dot plus the title `CHESTER app · 683012345`. "+" (Ctrl+T) opens a new tab. At 8 tabs "+" is
  disabled with the tooltip "At most 8 devices can be open."
- **Closing a tab** (Ctrl+W, ×, middle-click) disconnects it.
  - If an operation is running (any page reports busy), the user confirms first, e.g. "Flashing is in progress.
    Cancel it and close the tab?" with the buttons [Keep flashing] [Cancel and close]. The text comes from the
    busy page.
  - Closing the last tab leaves one empty *Choose device* tab.
- **Quitting** while an operation runs asks the same question.
- **Rail:** the pages registered for a tab (Console, Flash), each with an icon and a label. Below 1100 px window width
  it shows icons only, with the labels as tooltips. A page can show a busy badge. Settings (Ctrl+,) and Help (F1)
  sit at the bottom of the rail and are global.

### Choose device (new tab)

- The probe list refreshes every 2 s while visible. Each row shows the J-Link model, the SN, the firmware date and,
  when the probe is used elsewhere:
  - "In use (tab 2)" — selecting it switches to that tab;
  - "In use by another program (hio, PID 1234)" — the row is disabled (ADR-0058).
- Below the list are the device (CHESTER) and target (Application MCU `app` / LTE modem MCU `lte`) pickers, plus
  **Connect**.
- Exactly one free probe → it is preselected.
- **No probes:** the empty state says "No J-Link probe found — Connect the J-Link to USB and to the device.",
  followed by a check list:
  - ✓/✗ J-Link Software found (version).
  - "Is the USB cable connected?"
- **J-Link library missing:** an ErrorPanel shows `dependency.jlink_not_found` with "Open Settings" (path override).

### Device bar

`[Probe J-Link EDU Mini · 683012345] [Target: app ▾] State text … [Connect | Disconnect | Cancel] [⋯]`

- **Connection states:**
  - *Disconnected*: "Disconnected", with a Connect button.
  - *Connecting*: "Connecting…" with a spinner and Cancel (non-blocking open, ADR-0053).
  - *Connected*: "Connected · 3.30 V", with a Disconnect button.
  - *Disconnected with error*: an ErrorPanel at the top of the page area, plus a state text that names the cause
    (e.g. "Target not powered").
  - *Auto-reconnecting*: "Reconnecting… (target not powered)".
  - *Busy*: the text the busy page provides (e.g. "Flashing…"). The probe is shown as busy.
- The ⋯ popover holds speed, RTT latency, auto-reconnect, reset on open and "Save console to log file"
  (ADR-0052), with the note "Changes apply on the next connect."
- The target picker is disabled while connected.

### Shortcuts (ADR-0051)

- The full map is in ADR-0051. Shortcuts are registered by the pages through the registry (§ Design).
- The F1 overlay lists all registered shortcuts, grouped by page. Tooltips show them, e.g. "Find (Ctrl+F)".

### Errors

- All backend errors are shown through `ErrorPanel` with their texts. The primary action per category is:
  - Probe / Target → Reconnect;
  - Firmware → Choose another file;
  - Dependency → Open Settings;
  - others → Retry or Dismiss.
- A page can override the primary action.

### Texts

The English source texts are as above. Czech is translated in Qt Linguist as part of done (ADR-0027). The mockups
carry draft Czech texts for layout checks.

## CLI

**None.** M3 adds no backend capability. Probe listing in the picker = `hio jlink list` (incl. `inUseBy`, ADR-0058).
Tabs, theme and shortcuts are GUI-only.

## Design

### Modules

All new code lives in `app/gui` (QtQuick allowed). There is no change to `libs/*`; the backend amendments are
already applied in 0002 and 0003.

```
app/gui/
  src/main.cpp                      (M0) + single instance, style setup, font loading, ProbeScanner/registry ownership
  src/single_instance.*             QLockFile + QLocalServer/QLocalSocket (ADR-0058)
  src/settings_store.*              QObject over hio::Settings with valueChanged(key) (shared by all pages)
  src/viewmodels/
    app_view_model.*                (M0) + theme, tabs, pages, shortcuts overlay state
    page_registry.*                 page descriptors (contract for 0005/0006)
    shortcut_registry.*             id → sequence, scope, description; duplicate check
    device_tab_list_model.*         QAbstractListModel of DeviceTabViewModel
    device_tab_view_model.*         owns one tab's session + page view models (Qt parent ownership)
    probe_picker_view_model.*
    connection_view_model.*         device bar
    error_info.*                    Q_GADGET wrapper of hio::Error for QML
  src/workers/
    probe_scanner.*                 owned std::jthread, periodic SessionRegistry::listProbes (ADR-0050)
  qml/
    Main.qml  Theme.qml(singleton)  Shortcuts.qml
    shell/  DeviceTabBar.qml NavRail.qml DeviceBar.qml AdvancedPopover.qml PageHost.qml
    pages/  ChooseDevicePage.qml PlaceholderPage.qml
    components/ HButton.qml HToolButton.qml ErrorPanel.qml WarningBanner.qml Toast.qml ConfirmDialog.qml
                EmptyState.qml ShortcutOverlay.qml PhaseStepper.qml (used by 0006, owned here)
  assets/fonts/ (LFS)  assets/icons/*.svg + README.md (ADR-0060)  qtquickcontrols2.conf
  tests/ tst_*.qml (Qt Quick Test)  viewmodels/*_test.cpp (GTest)
```

### Page-extension contract (ADR-0059; 0005 and 0006 implement it)

```cpp
namespace hio::gui {
struct PageDescriptor {                         // registered once at startup, before the first tab exists
    QString id;                                 // "console", "flash", "settings"
    const char* titleSourceText;                // QT_TRANSLATE_NOOP("Pages", "Console"), translated live
    QString icon;                               // icon file name, ADR-0060
    QUrl qml;                                   // page root component, gets `tab` and `vm` properties
    int order;
    bool global = false;                        // true: one instance for the app (Settings), shown at the rail bottom
    // Creates the page view model; parent = tab (or AppViewModel for global pages) — Qt parent ownership.
    std::function<QObject*(DeviceTabViewModel* tab /*nullptr for global*/)> createViewModel;
};
struct SettingsSectionDescriptor {              // a section of the global Settings page (rendered by 0006)
    QString id;                                 // "general", "console", "jlink", …
    const char* titleSourceText;
    QUrl qml;                                   // section component; binds to SettingsStore keys of its plan
    int order;
};
class PageRegistry {                            // owned by AppViewModel
public:
    void add(PageDescriptor);                   // duplicate id → assert in debug, ignored + logged in release
    void addSettingsSection(SettingsSectionDescriptor);
    [[nodiscard]] std::span<const PageDescriptor> pages() const;
    [[nodiscard]] std::span<const SettingsSectionDescriptor> settingsSections() const;
};

class DeviceTabViewModel : public QObject {    // QML_UNCREATABLE; parent = DeviceTabListModel
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)   // ChoosingDevice, Disconnected, Connecting, Connected, Busy
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(ProbePickerViewModel* picker READ picker CONSTANT)
    Q_PROPERTY(ConnectionViewModel* connection READ connection CONSTANT)
    Q_PROPERTY(QObjectList pageViewModels READ pageViewModels CONSTANT)   // in PageRegistry order
    Q_PROPERTY(QString currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(QString busyPage READ busyPage NOTIFY busyChanged)       // "" when idle
    Q_PROPERTY(QString busyText READ busyText NOTIFY busyChanged)
public:
    // --- contract for pages (C++ only) ---
    [[nodiscard]] hio::device::DeviceSession* session() const;        // nullptr while choosing / disconnected
    // Opens a session for `targetId` when none exists (Flash uses attachRtt=false; ADR-0057). No-op when one exists.
    [[nodiscard]] Status ensureSession(std::string_view targetId, bool attachRtt);
    [[nodiscard]] const hio::device::DeviceProfile& profile() const;
    [[nodiscard]] QString consoleTargetId() const;
    // One log for all sessions of this tab (ADR-0063); passed as SessionOptions::consoleLog on every open.
    [[nodiscard]] const std::shared_ptr<hio::device::ConsoleLog>& consoleLog() const;
    // Busy: one page at a time. The tab state becomes Busy, the rail shows the page's badge, and closing/quitting
    // asks with `confirmText` ("Flashing is in progress. Cancel it and close the tab?"). `cancel` is called on
    // [Cancel and close]; the tab closes when the page then calls clearBusy().
    void setBusy(const QString& pageId, QString busyText, QString confirmText, std::function<void()> cancel);
    void clearBusy(const QString& pageId);
    void showToast(QString text);               // queued in AppViewModel (≤ 3 visible)
signals:
    void sessionChanged();                      // pages reconnect their signal handlers to the new session
};

class SettingsStore : public QObject {          // owned by AppViewModel; GUI thread only
    Q_OBJECT
public:
    [[nodiscard]] QVariant value(QAnyStringView key) const;
    [[nodiscard]] Status setValue(QAnyStringView key, const QVariant&);   // validates against the key table; sync()
    void registerKeys(std::span<const SettingKey>);                       // each plan registers its keys + validators
    [[nodiscard]] bool readOnly() const;        // settings.newer_schema (ADR-0025)
signals:
    void valueChanged(const QString& key);      // emitted for writes from this process
};

class ShortcutRegistry : public QObject {       // owned by AppViewModel; read by Shortcuts.qml and the F1 overlay
    Q_OBJECT
public:
    // scope: "global" or a page id; the same sequence twice in one scope, or in a page scope and global → error
    [[nodiscard]] Status add(QString id, QKeySequence, QString scope, const char* descriptionSourceText);
    Q_INVOKABLE QString sequenceText(const QString& id) const;   // for tooltips
};
}
```

- The tab owns `std::unique_ptr<hio::device::DeviceSession> session_` and the tab's
  `std::shared_ptr<ConsoleLog>` (ADR-0063). Pages never own or close the session; they connect to its signals on
  `sessionChanged`. Disconnect = `session_->close()` and release; the log stays.
- Settings is a global page (`global = true`). 0006 registers it. Until then 0004 ships `PlaceholderPage.qml` for
  the "settings" id.
- The test page (`tests/qml/TestPage.qml` + a fake VM) exercises the contract: registration, busy, close
  confirmation, session changes and shortcut registration.

### Other view models (sketch)

```cpp
class AppViewModel : public QObject {          // QML_SINGLETON (M0) — additions
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)   // "system"|"light"|"dark"
    Q_PROPERTY(DeviceTabListModel* tabs READ tabs CONSTANT)
    Q_PROPERTY(int currentTab READ currentTab WRITE setCurrentTab NOTIFY currentTabChanged)
    Q_PROPERTY(bool railCompact READ railCompact NOTIFY railCompactChanged)    // window width < 1100
    Q_INVOKABLE void newTab();  Q_INVOKABLE void closeTab(int index, bool force = false);
    Q_INVOKABLE bool canQuit();  // false + signal quitConfirmationNeeded(text) while a tab is busy
};

class ConnectionViewModel : public QObject {
    Q_PROPERTY(QString probeLabel ...) Q_PROPERTY(QString targetId ...) Q_PROPERTY(QString stateText ...)
    Q_PROPERTY(double targetVolts ...) Q_PROPERTY(hio::gui::ErrorInfo error ...)   // invalid when none
    Q_PROPERTY(int speedKhz ...) Q_PROPERTY(int latencyMs ...) Q_PROPERTY(bool autoReconnect ...)
    Q_PROPERTY(bool resetOnOpen ...) Q_PROPERTY(bool logToFile ...)
    Q_INVOKABLE void connectDevice(); Q_INVOKABLE void disconnectDevice(); Q_INVOKABLE void reconnect();
};

class ProbePickerViewModel : public QAbstractListModel {
    enum Roles { SerialRole = Qt::UserRole + 1, ProductRole, FirmwareRole, InUseTabRole /*-1 none*/, InUseOtherRole /*"hio, PID 1234" or ""*/ };
    Q_PROPERTY(QString profileId ...) Q_PROPERTY(QString targetId ...) Q_PROPERTY(int selectedRow ...)
    Q_PROPERTY(hio::gui::ErrorInfo libraryError ...) Q_PROPERTY(QString libraryVersion ...)
    Q_INVOKABLE void connectSelected();
};

struct ErrorInfo {                             // Q_GADGET, value type `errorInfo`
    Q_PROPERTY(bool valid ...) Q_PROPERTY(QString code ...) Q_PROPERTY(QString message ...) Q_PROPERTY(QString cause ...)
    Q_PROPERTY(QString suggestion ...) Q_PROPERTY(QString detail ...) Q_PROPERTY(QString wizard ...)   // kebab id or ""
    Q_PROPERTY(int primaryAction ...)          // Reconnect, ChooseFile, OpenSettings, Retry, Dismiss
};
```

The Reset ▾ / Reconnect actions shown in the console toolbar belong to 0005. The device bar offers only Connect /
Disconnect / Cancel and the ⋯ popover.

### Data flow and threading

- **GUI main thread:** all ViewModels, the `DeviceSession` façades, `SessionRegistry`, `SettingsStore`,
  `ShortcutRegistry` and `SingleInstance`.
- **Session workers** are owned by each `DeviceSession` (ADR-0031). The GUI never calls the JLink library.
  `open()` is non-blocking (ADR-0053). The device bar follows `connectionChanged`; Cancel during *Connecting* =
  `close()`.
- **`ProbeScanner`:**
  - It is one owned `std::jthread` for the application, owned by `AppViewModel`.
  - It runs `SessionRegistry::listProbes` every 2 s while any picker is visible (the visibility count is set from
    the GUI thread through an atomic). In-process "in use" data is added on the GUI thread, because the registry is
    GUI-owned; the worker reports only the probes and the lock owners.
  - Results arrive by queued `QMetaObject::invokeMethod`.
  - Its stop token is honoured; it is joined on destruction.
- **Single instance:** a `QLocalServer` on the GUI thread. One message is handled (`activate\n`) and anything else
  is ignored. Each read is bounded to 64 bytes.
- **Settings:** `SettingsStore` is the only settings writer in the GUI and calls `sync()` after each write
  (ADR-0058). Pages react to `valueChanged`.

### Error cases (GUI-originated, this plan)

| Situation | Code | Message / cause / suggestion | Wizard |
|---|---|---|---|
| 9th tab | `internal.session_limit` (existing) | prevented by disabling "+" | — |
| Probe used by another process | `probe.in_use_by_other_process` (0002) | shown in the picker row and, on connect, in the ErrorPanel | — |
| Restored tab list invalid | (no error) | logged; start with one Choose device tab | — |
| Settings value invalid | `usage.invalid_argument` | inline at the control, not saved | — |

### Settings keys added (ADR-0025)

| Key | Type | Default |
|---|---|---|
| `ui/theme` | string | `system` |
| `ui/windowGeometry` | string (base64) | — |
| `ui/restoreTabs` | bool | `true` |
| `ui/tabs` | string (JSON `{current, tabs:[{profile,target,sn}]}`, max 8) | — |
| `jlink/speedKhz`, `rtt/latencyMs`, `rtt/autoReconnect`, `rtt/resetOnOpen` | int/int/bool/bool | profile values |
| `probes/<sn>/{speedKhz,latencyMs,autoReconnect,resetOnOpen}` | per probe, LRU max 32 | — |

The `schemaVersion` bump and its migration (a no-op for new keys) are documented. The `console/*` and `flash/*` keys
are registered by 0005 and 0006 through `SettingsStore::registerKeys`.

### Limits

| Item | Limit |
|---|---|
| Device tabs | ≤ 8 (registry) |
| Remembered probes | 32 |
| Toasts | 3 visible |
| Probe scan interval | 2 s, only while a picker is visible |
| Single-instance message | 64 bytes, 2 s timeout |

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Window organisation | left rail + device bar / top tabs / console-centric | Left rail + device bar | ADR-0043 |
| Devices per window | one / device tabs | Device tabs (≤ 8) | ADR-0043 |
| Tabs vs rail | device tabs on top, rail per tab / rail on top, tabs inside | Device tabs on top, rail per tab | ADR-0043 |
| Qt Quick Controls style | Basic + own / Material / Fusion-native | Material (Dense) + Theme singleton | ADR-0044 |
| Themes | light+dark+system / dark only | Light + Dark + System; palette proposed (brand colours: open question 16) | ADR-0047 |
| Fonts | Inter + JetBrains Mono / system / Inter + Ubuntu Mono | Inter + JetBrains Mono (LFS) | ADR-0048 |
| Add device | picker in new tab / dialog | Picker in new tab, 2 s scan | ADR-0050 |
| Shortcuts | fixed map / remappable | Fixed map + F1 overlay | ADR-0051 |
| J-Link options | Advanced popover / settings only | Advanced popover, per-probe memory | ADR-0052 |
| Blocking `open()` | non-blocking open / GUI opener thread | Non-blocking open (applied in 0002) | ADR-0053 |
| App instances | single GUI + probe locks / single GUI / no coordination | Single GUI instance + cross-process probe locks (backend) | ADR-0058 |
| M3 plan structure | 3 plans / 2 plans / one | 3 plans with a page-extension contract | ADR-0059 |
| Icons | Material Symbols / Lucide / Phosphor | Material Symbols SVG subset | ADR-0060 |
| Restore tabs at start | restore Disconnected / restore + connect / no | Restore Disconnected, setting default on | ADR-0061 |
| Responsive breakpoints | 1100 / 1000 / 960 × 600; 800 × 560; no rail collapse | Rail icons < 1100 px, split stacks < 1000 px, min 960 × 600 | ADR-0061 |

## Implementation steps

Phase A — foundation
- [ ] A1 Prerequisite check: the 0002 amendments (ADR-0053, ADR-0058) are implemented in M1 (otherwise stop and
  report)
- [ ] A2 Material Dense style setup, `qtquickcontrols2.conf`, `Theme.qml` tokens (both themes), font loading (LFS +
  licences), icon subset + README (ADR-0060)
- [ ] A3 Components: HButton, HToolButton, ErrorPanel, WarningBanner, Toast, ConfirmDialog, EmptyState,
  ShortcutOverlay, PhaseStepper + Qt Quick Tests
- [ ] A4 `ErrorInfo` gadget + mapping of category → primary action
- [ ] A5 `SettingsStore` (key table, validation, `valueChanged`, read-only mode) + `SingleInstance` (ADR-0058) + tests

Phase B — shell
- [ ] B1 `PageRegistry` (pages + settings sections), `ShortcutRegistry` (duplicate check), `PageHost`, placeholder
  and test pages
- [ ] B2 `DeviceTabListModel`, `DeviceTabViewModel` (session ownership, `ensureSession`, busy API), tab bar, rail
  (compact < 1100 px), tab restore, close/quit confirmation
- [ ] B3 `ProbeScanner` worker + `ProbePickerViewModel` + ChooseDevicePage (empty, missing library and in-use states)
- [ ] B4 `ConnectionViewModel` + DeviceBar + Advanced popover + per-probe settings
- [ ] B5 F1 overlay and the global shortcuts of ADR-0051 (Ctrl+T, Ctrl+W, Ctrl+Tab, Ctrl+,, F1, …)
- [ ] B6 Accessibility names and reduced motion; Czech translation of the shell texts, `.ts` fresh; screenshots of
  the shell in both themes and both languages

## Tests and verification

- **ViewModel unit tests (GTest, fake `DeviceSession` via the fake J-Link library from 0002):**
  - tab lifecycle and the 8-tab limit;
  - restore without connect (missing probe, invalid list);
  - the tab's `ConsoleLog` survives Disconnect → Connect and a change of probe (ADR-0063);
  - connection state text for every `ConnectionEvent`, incl. Connecting → Cancel → `close()`;
  - picker in-use states (another tab; another process via a held `ProbeLock`);
  - busy API: close and quit confirmation, cancel callback, `clearBusy`;
  - page registry and shortcut duplicates;
  - `SettingsStore` validation and read-only mode;
  - single instance: a second instance activates the first; a hung first instance → dialog.
- **Qt Quick Tests:**
  - every component in light and dark;
  - ErrorPanel rendering of all fields and the disabled wizard button;
  - the shortcut map has no duplicates;
  - layout at 960 × 600 and at 1099 px width in Czech has no overlapping or clipped buttons (geometry assertions).
- **`qmllint`** (`all_qmllint`) is clean; no logic in QML (review checklist: no JS beyond bindings / one-line
  handlers).
- **Sanitizers:** ASan+UBSan and TSan builds run the ViewModel tests and a scripted GUI smoke test (offscreen
  platform).
- **HIL (CHESTER + J-Link):**
  - two tabs with two probes;
  - unplug/replug power and auto-reconnect shown in the device bar;
  - `hio chester app console` on the probe of an open tab → exit 5, and the picker shows "In use by another program".
- **Manual on Linux, Windows, macOS (by the user, before Done):**
  - all shell mockup scenarios;
  - high-DPI 150 %;
  - keyboard-only walkthrough;
  - screen reader spot check (Orca / Narrator / VoiceOver);
  - live theme switching;
  - a second launch focuses the first window.

## Risks and open points

- **Material Dense density** may still be too roomy for a console tool. The wrapper components allow tightening the
  paddings without changing the style.
- **The page-extension contract** must be stable before 0005/0006 start (ADR-0059). Changes need an amendment to this
  plan.
- **Single instance on Windows / Flatpak:** `QLocalServer` names and sandboxing (open question 7) are verified in M7.
- **The palette** is a proposal (no brand guide, open question 16). Colour changes after review update only the
  design system and `Theme.qml`.
