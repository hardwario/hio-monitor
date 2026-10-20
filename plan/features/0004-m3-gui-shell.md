# 0004 — M3 GUI shell: theme, components, device tabs, probe picker, device bar

- **Status:** In progress (implementation started 2026-10-12; approved by Matej Strnad, 2026-10-12, without the pending M4/M5 amendments; implemented on `v2/m3-gui`, based on the M2 WIP commit 7f371c6); amended by 0007 (approved by Matej Strnad 2026-10-16 together with 0007–0010)
- **Milestone:** M3 (plan 1 of 3, ADR-0059; followed by [0005](0005-m3-console.md) and [0006](0006-m3-flash-settings.md))
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0002, ADR-0003, ADR-0004, ADR-0018, ADR-0025, ADR-0027, ADR-0031, ADR-0043, ADR-0044,
  ADR-0047, ADR-0048, ADR-0050, ADR-0051, ADR-0052, ADR-0053, ADR-0058, ADR-0059, ADR-0060, ADR-0061, ADR-0063, ADR-0066, ADR-0067, ADR-0069, ADR-0111
- **Depends on:** 0001 (M0). 0002 (M1, with the ADR-0053 / ADR-0058 amendments applied) for real sessions; until
  M1 lands, the shell runs against the fake J-Link library.
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

> **Amended by 0007–0010 (approved 2026-10-16; ADR-0066, ADR-0067, ADR-0069; applied in this text):** the component
> set gains WizardDialog, WizardStepList, CheckList, CommandBlock, Illustration and HelpMenu (built by 0007). The
> ErrorPanel's "Help me fix this" is enabled and shown only when the error's wizard is registered; it opens that
> wizard through the `WizardLauncher` with the error and the tab. The rail's Help item opens the Help menu (F1 /
> Ctrl+/ still open the shortcut overlay). `DeviceTabViewModel` gains `openWizard(id)` and
> `ensureSession(bool connectTarget = true)` for probe-only sessions (reconciled with S2; confirmed by the
> user 2026-10-16, see 0007 Implementation notes). The Choose-device empty state gets [Guide me] (Connect wizard, 0009)
> and the page gets the links "Not sure how to connect? Guide me" and "Flash firmware with a guide" (0010).
> `probe.in_use_by_other_process` opens Troubleshooting. The work is done in 0007 (C2, C5), 0009 (T4) and 0010 (W4).

> **Pending amendment from M5 ([0012](0012-m5-ble-gui-pairing.md), ADR-0081, ADR-0083; applied when 0012 is approved
> — then this plan needs re-approval):** the Choose-device page gets a J-Link | Bluetooth switch and a BLE device
> list; `DeviceTabViewModel` gains `transport`, `consoleSession()`, `openBle()`, `bleSession()`; BLE tabs have a BLE
> device bar and title; `ui/tabs` entries gain `transport`; settings `ui/lastTransport`, `ble/*`.

> **Pending amendment from M6 (ADR-0105, specified in [0014](0014-m6-mcp-gui.md)):** an "MCP" indicator on the rail
> above Settings; an "AI" badge on tabs opened by an AI assistant (not restored); busy page id `ai` for AI
> operations; `AppViewModel::openTab` overload for programmatic opening.

> **Pending amendment from M7 ([0015](0015-m7-packaging-release.md), [0016](0016-m7-update-check.md); applied when
> they are approved — then this plan needs re-approval):** the shell banner area hosts `UpdateBanner` (below error
> banners) and `CrashNoticeBanner` (ADR-0123); `setDesktopFileName("com.hardwario.monitor")`, window icon and the
> Windows AppUserModelID are set in `main()`; single instance on Windows is verified on the MSI install (Flatpak is
> deferred, ADR-0118).

> **Pending amendment from M8 ([0017](0017-m8-parity.md), ADR-0132):** the device bar shows the "J-Link firmware is
> outdated" warning banner with [Update] / [Not now]; error panels' "More help" link uses `docsUrl()` (ADR-0138).

> **Pending amendment from [0021](0021-gui-feedback-round-1.md) (ADR-0168, ADR-0172; applied when 0021 is approved —
> then this plan needs re-approval):** target pickers (device bar, Choose device) show only `app` / `lte`, the long
> title moves to the tooltip; the rail's Settings item (and Ctrl+,) opens Settings as a special tab in the tab
> strip that exists only while open, not counted in the 8-tab limit and not restored; `openSettings(sectionId)`.

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
    ShortcutOverlay, PhaseStepper). *Amended by 0007 (2026-10-16):* the set also contains WizardDialog,
    WizardStepList, CheckList, CommandBlock, Illustration and HelpMenu (ADR-0066), built by 0007.
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
  whenever an error has a `wizard`. *Amended by 0007 (2026-10-16):* from 0007 on the button is enabled and shown
  only when the error's wizard is registered (hidden, not disabled, otherwise); "Coming soon" is removed.
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
  sit at the bottom of the rail and are global. *Amended by 0007 (2026-10-16):* the Help item opens the Help menu
  (Keyboard shortcuts (F1), Setup wizard…, Connect a device…, Troubleshoot connection…, Flash firmware (guided)…,
  Create diagnostic report…, Documentation ↗; items whose wizard is not registered are hidden); F1 / Ctrl+/ still
  open the shortcut overlay directly.

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

  *Amended by 0009 (2026-10-16):* the empty state's button is **[Guide me]** → Connect wizard (`connect-device`)
  on this tab; the page also shows the ghost link "Not sure how to connect? Guide me".
- *Amended by 0010 (2026-10-16):* below Connect, the link "Flash firmware with a guide" opens the flash wizard
  (`flash-firmware`) on this tab.
- **J-Link library missing:** an ErrorPanel shows `dependency.jlink_not_found` with "Open Settings" (path override).
  *Amended by 0008 (2026-10-16):* the panel also offers "Help me fix this" → setup wizard (`first-run-setup`).

### Device bar

`[Probe J-Link probe · 683012345] [Target: app ▾] State text … [Connect | Disconnect | Cancel] [⋯]`

- **Connection states:**
  - *Disconnected*: "Disconnected", with a Connect button.
  - *Connecting*: "Connecting…" with a spinner and Cancel (non-blocking open, ADR-0053).
  - *Connected*: "Connected · 3.30 V", with a Disconnect button.
  - *Disconnected with error*: an ErrorPanel at the top of the page area, plus a state text that names the cause
    (e.g. "Target not powered").
  - *Auto-reconnecting*: "Reconnecting… (target not powered)".
  - *Busy*: the text the busy page provides (e.g. "Flashing…"). The probe is shown as busy.
- The ⋯ popover holds speed, RTT latency, auto-reconnect, reset on open and "Save console to log file"
  (ADR-0052), with the note "Changes apply on the next connect." Speed (ADR-0111): an **Automatic speed** switch,
  on by default (ADR-0090), and the kHz spin box, disabled while automatic (it keeps the last fixed value; 4000 kHz
  when none was stored). A fixed speed below 125 kHz shows the ADR-0110 warning (`device::slowSpeedWarning`).
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
- *Amended by 0007 (2026-10-16):* "Help me fix this" is shown in an ErrorPanel when `ErrorInfo.wizard` is non-empty
  **and** registered (`AppViewModel.launcher.canOpen(error.wizard)`). It calls `WizardLauncher::open(wizardId,
  errorInfo, tab)`; the wizard starts with the error as its context. An already open wizard is brought to the front
  (one at a time, ADR-0066).

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
                WizardDialog.qml WizardStepList.qml CheckList.qml CommandBlock.qml Illustration.qml
                (amended by 0007, 2026-10-16; built by 0007 C2)
    shell/HelpMenu.qml              (amended by 0007, 2026-10-16; built by 0007 C2)
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
    // Opens the tab's session when none exists: always on the console target, with RTT when that target has an
    // RTT console. Operations carry their own target (ADR-0057; amended after review S2: no target/attachRtt
    // parameters). No-op when a working session exists; a stopped one (fatal error) is replaced (S1).
    // Errors: those of DeviceSession::open, dependency.*, usage.invalid_argument (no probe), internal.busy.
    // Amended by 0007 (2026-10-16; reconciled with S2; confirmed by the user 2026-10-16, see 0007 Implementation notes):
    // connectTarget = false opens a probe-only session (SessionOptions::connectTarget = false, state ProbeOpen,
    // 0002) on the console target without RTT. The target does not matter there, because diagnose(targetId) and
    // every operation carry their own target; DeviceSession::startConnection(attachRtt) promotes the session later.
    // This replaces the ensureSession(target, attachRtt = false, connectTarget = false) written in 0007 and 0009.
    [[nodiscard]] Status ensureSession(bool connectTarget = true);
    // Amended by 0007 (2026-10-16): opens the wizard `id` (kebab id) for this tab through the WizardLauncher.
    Q_INVOKABLE void openWizard(const QString& id);
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
    // session() was replaced or released (Connect, Disconnect, probe/target change, shutdown on tab close/quit).
    // Emitted before the old session is deleted; pages keep the session only as QPointer or re-read session()
    // here (amended after review S3).
    void sessionChanged();
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
- **Amendments after review round 1 (S2, S3, S5; 2026-10-12):**
  - `ensureSession()` opens the console target with RTT; operations carry their target (ADR-0057).
    *Amended by 0007 (2026-10-16):* `ensureSession(false)` opens a probe-only session on the console target
    (reconciled with S2; confirmed by the user 2026-10-16, see 0007 Implementation notes).
  - `sessionChanged` is also emitted by `shutdown()`; pages hold the session only as `QPointer` or re-read it.
  - The shell owns **all** console markers: "— connected —", "— disconnected —", "— disconnected: <cause> —",
    "— paused: <busy text> —" and "— resumed —" (after the busy page clears). Pages never append them.
  - F4 (reconnect) and Ctrl+Shift+D are **global** shortcuts of the shell only; a page must not register them.
  - Pages register their shortcuts with `descriptionSourceText = QT_TRANSLATE_NOOP("Shortcuts", "…")`: the overlay
    translates descriptions in the "Shortcuts" context (the shell's own Ctrl+1 … entries use the page titles,
    context "Pages").
- **Amended by 0005 (2026-10-16, user decision, review round 1 of 0005):** the session-warning banner in `PageHost`
  (`WarningBanner`) has a [Details] toggle for warnings with details (cause, suggestion, code + detail, Copy; e.g.
  `probe.firmware_outdated`), and `ConnectionViewModel::setWarning` shows the console log file's `io.write_failed`
  warning as "The console log file could not be written; logging to file is off for this session." (the backend
  detail stays for [Details]). Pages do not repeat session warnings. `ProbeScanner::stop()` (stop and join, also
  used by the destructor) lets the Qt Quick test runner stop the scanner when a test file has finished.
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
    Q_PROPERTY(bool autoSpeed ...) Q_PROPERTY(int speedKhz ...) /*fixed value*/ Q_PROPERTY(QString speedWarning ...)
    Q_PROPERTY(int latencyMs ...) Q_PROPERTY(bool autoReconnect ...)
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
    // Amended by 0007 (2026-10-16): `wizard` is always the kebab id (ADR-0067); the ErrorPanel's wizard button binds
    // visible: AppViewModel.launcher.canOpen(error.wizard).
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
| Probe used by another process | `probe.in_use_by_other_process` (0002) | shown in the picker row and, on connect, in the ErrorPanel | Troubleshooting (*amended by 0007, 2026-10-16*; was —) |
| Restored tab list invalid | (no error) | logged; start with one Choose device tab | — |
| Settings value invalid | `usage.invalid_argument` | inline at the control, not saved | — |

### Settings keys added (ADR-0025)

| Key | Type | Default |
|---|---|---|
| `ui/theme` | string | `system` |
| `ui/windowGeometry` | string (base64) | — |
| `ui/restoreTabs` | bool | `true` |
| `ui/tabs` | string (JSON `{current, tabs:[{profile,target,sn}]}`, max 8) | — |
| `jlink/speedKhz`, `rtt/latencyMs`, `rtt/autoReconnect`, `rtt/resetOnOpen` | int/int/bool/bool | profile values (speed: 0 = automatic, ADR-0111) |
| `probes/<sn>/{speedKhz,latencyMs,autoReconnect,resetOnOpen}` | per probe, LRU max 32 (speed: 0 = automatic) | — |

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
- [x] A1 Prerequisite check: the 0002 amendments (ADR-0053, ADR-0058) are implemented in M1 (otherwise stop and
  report)
- [x] A2 Material Dense style setup, `qtquickcontrols2.conf`, `Theme.qml` tokens (both themes), font loading (LFS +
  licences), icon subset + README (ADR-0060)
- [x] A3 Components: HButton, HToolButton, ErrorPanel, WarningBanner, Toast, ConfirmDialog, EmptyState,
  ShortcutOverlay, PhaseStepper + Qt Quick Tests
- [x] A4 `ErrorInfo` gadget + mapping of category → primary action
- [x] A5 `SettingsStore` (key table, validation, `valueChanged`, read-only mode) + `SingleInstance` (ADR-0058) + tests

Phase B — shell
- [x] B1 `PageRegistry` (pages + settings sections), `ShortcutRegistry` (duplicate check), `PageHost`, placeholder
  and test pages
- [x] B2 `DeviceTabListModel`, `DeviceTabViewModel` (session ownership, `ensureSession`, busy API), tab bar, rail
  (compact < 1100 px), tab restore, close/quit confirmation
- [x] B3 `ProbeScanner` worker + `ProbePickerViewModel` + ChooseDevicePage (empty, missing library and in-use states)
- [x] B4 `ConnectionViewModel` + DeviceBar + Advanced popover + per-probe settings
- [x] B5 F1 overlay and the global shortcuts of ADR-0051 (Ctrl+T, Ctrl+W, Ctrl+Tab, Ctrl+,, F1, …)
- [x] B6 Accessibility names and reduced motion; Czech translation of the shell texts, `.ts` fresh; screenshots of
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
  - ErrorPanel rendering of all fields and the disabled wizard button (*amended by 0007, 2026-10-16:* from 0007 on
    the button is shown only for a registered wizard; tested in 0007);
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

## Implementation notes

Recorded by the implementing agent (2026-10-12, branch `v2/m3-gui`). Minor local choices; the open points at the
end need a decision by the user.

- **A1:** ADR-0053 (non-blocking `open()`, `waitUntilConnected`), ADR-0058 (`ProbeLock`,
  `probe.in_use_by_other_process`, the lock owner per probe) and ADR-0063 (`SessionOptions::consoleLog`,
  `appendMarker`) are present in the M1/M2 base (7f371c6). No change to `libs/*`.
- **AppViewModel** is created by `main()` (and by the test runners) with its dependencies (`AppOptions`: settings,
  registry directories, scan interval, `registerExtensions`) and reaches QML through the `QML_SINGLETON` factory
  `create()`, which finds it as a direct child of the application (the M0 pattern). It owns the `SettingsStore`,
  `PageRegistry`, `ShortcutRegistry`, `SessionRegistry`, `ProbeScanner` and the tabs; its destructor closes the tabs
  before the registry and the scanner go away. The `LanguageManager` now reads and writes `ui/language` through the
  `SettingsStore` (the GUI's only settings writer).
- **Page registration:** `registerBuiltInPages()` (`src/pages/builtin_pages.cpp`) registers placeholder pages for
  `console` and `flash` (Ctrl+1 / Ctrl+2) as well as the `settings` placeholder, so the rail is complete and the
  shell is usable on its own; 0005 and 0006 replace these registrations. Page shortcuts `page.<id>` (Ctrl+1 …) are
  registered by the shell for the per-tab pages in rail order. The Settings placeholder also offers Language (moved
  from the M0 window) and Theme and shows the build info next to the read-only J-Link library path.
- **Help** (rail, F1, Ctrl+/) opens the shortcut overlay; there is no Help menu (M4/M5 amendments not applied).
  *Amended by 0007 (2026-10-16):* the Help menu replaces this when 0007 C2 is implemented.
- **Close / quit confirmation:** the contract carries the busy text and the confirmation text only, so the buttons
  are generic: [Keep running] [Cancel and close] / [Cancel and quit].
- **ErrorInfo:** primary action per category: Probe, Target → Reconnect; Firmware → Choose another file;
  Dependency → Open Settings; Io, Network, Timeout, Internal → Retry; Usage, Cancelled → Dismiss. QML uses
  `ErrorAction.Reconnect`, …; `ErrorPanel.primaryAction` overrides it. The wizard id comes from the error's JSON
  form (`hio::toJson`), the single place that names wizards.
- **ProbeScanner** never touches the GUI-owned `SessionRegistry`: on its worker it runs `jlink::locateLibrary`,
  a kept `jlink::ProbeEnumerator` (ADR-0093, review S4) and `ProbeLock::owner` per probe; results are handed over in a
  mutex-protected slot and a coalesced queued call by name (the `DeviceSession::wakeUp` pattern). "In use (tab N)"
  is computed on the GUI thread from the tabs' configured probe serial numbers (a superset of the registry's
  sessions: a restored, disconnected tab also reserves its probe). Rescans with the same probes update the rows in
  place (no model reset), so focus and hover survive the 2 s refresh.
- **J-Link options (ADR-0052, ADR-0111):** `jlink/speedKhz` and `rtt/latencyMs` default to 0 = the profile's value;
  for the speed, 0, a missing key or a value outside 5–50000 means automatic (the profile default, automatic for every
  profile since ADR-0090; `SessionOptions::speedKhz` = `std::nullopt`), and the per-probe speed stores 0 for
  automatic. Stored fixed values stay fixed, including the old explicit 2000 kHz default (no migration);
  `rtt/autoReconnect` defaults to on and `rtt/resetOnOpen` to off (the profile has no values for these). "Save
  console to log file" is per tab (default `console/logToFile` once 0005 registers it, otherwise on) and is not
  remembered per probe (it is not in the per-probe key list). Per-probe values are written when an option is changed
  in the popover (all four keys) and the probe is moved to the front of `probes/recent` (comma-separated, most recent
  first, ≤ 32); a connect also moves a remembered probe to the front. A target without an RTT console (CHESTER
  `lte`) is connected with `attachRtt = false`.
- **Settings schema:** stays 1. The 0004 keys are new keys with defaults, so the "migration" is a no-op and no
  `schemaVersion` bump is needed (older builds ignore unknown keys); documented in `settings_store.h`.
  `ui/windowGeometry` is base64 of `{"v":1,"x","y","w","h","maximized"}`; it is applied only when it is at least
  960 × 600 and visible on a connected screen. `ui/tabs` is saved on every tab change and on quit.
- **Lifetimes:** a closed session is deleted on the next event-loop turn (never inside its own signal); a closed tab
  is deleted with `deleteLater()` after its QML delegates (Qt parent = AppViewModel until then). Tab delegates keep
  their last tab while the model removes the row (`Binding` with `RestoreNone`).
- **Console markers (ADR-0063):** "— connected —", "— disconnected —", "— disconnected: <cause> —" on connection
  changes and "— paused: <busy text> —" on `setBusy`, in both channels.
- **Components:** `HButton` draws its own flat background (1 px border for normal buttons, accent / error fill for
  primary / danger, hover and press overlay, no ripple or elevation) and its own content colour, as the design
  system asks (flat with borders) and so that the colours are those of `Theme` in both themes. `Icon.qml` wraps
  `IconImage` (QtQuick.Controls.impl). `FocusRing.qml` and `StateDot.qml` are small shared helpers.
- **Reduced motion:** Qt 6.8 has no API. Windows: `SPI_GETCLIENTAREAANIMATION`; macOS: `reduceMotion` of
  `com.apple.universalaccess`; every OS: `HIO_REDUCED_MOTION=1`. `Theme.animationMs` is 0 and spinners stop then.
- **Assets:** Inter 4.1 and JetBrains Mono 2.304 (OFL 1.1, Git LFS, `assets/fonts/README.md` with SHA-256s);
  Material Symbols Outlined from google/material-design-icons, pinned to commit `bd8cb85` (2026-09-25) because no
  tag contains Material Symbols (`assets/icons/README.md`, Apache 2.0). The icons need Qt's SVG image plugin at
  runtime (packaging, M7).
- **Not implemented** (design-system details outside this plan's normative UX): tab reordering by drag, the red
  error dot on rail items.
- **Tests:** `hio_gui_tests` (GoogleTest, fake J-Link library, a helper mode of the binary for the second
  single-instance process and for a probe lock held by another process) and `tst_gui` (Qt Quick Test: components in
  light and dark, ErrorPanel, shell with the contract test page `tests/qml/TestPage.qml`, shortcuts, layout at
  960 × 600 and 1099 px in Czech). `tst_screenshots.qml` writes the B6 screenshots when `HIO_SCREENSHOT_DIR` is set
  (`QT_QUICK_BACKEND=software` on the offscreen platform). Qt Test (`QSignalSpy`) is linked by the tests only.

- **Sanitizer setup:** every `tst_*.qml` file runs as its own ctest (`tst_gui.<file>`, one process each: several
  QML engines in one process left QML binding memory that LeakSanitizer reported). New
  `tools/sanitizers/lsan.supp` (fontconfig keeps the data parsed while `addApplicationFont` registers the bundled
  fonts; `LSAN_OPTIONS` added to the test presets) and `tools/sanitizers/tsan-qml.supp`, used only by the
  `tst_gui.*` tests: Qt's internal thread pools, the QML type loader and the metatype registry synchronize inside the
  uninstrumented Qt libraries (`called_from_lib` for QtCore, QtGui, QtQml, QtQuick and
  `race:QMetaType::registerConverter`). The view models and the scanner thread are checked by `hio_gui_tests` with
  the unchanged strict `tsan.supp`. The GTest discovery of `hio_gui_tests` creates no application.

### Open points for the user (all decided 2026-10-12, see below and "Fixes of review round 1")

1. **Target voltage:** `DeviceSession` does not report VTref, so the device bar shows "Connected" without
   "· 3.30 V" (`targetVolts` is NaN). Proposal: a 0002 amendment that adds the measured millivolts to
   `ConnectionEvent` (or a status event); the GUI part is ready (`Connected · %1 V`).
2. **Removing settings keys:** `hio::Settings` has no remove, so the `probes/<sn>/*` keys of a probe dropped from the
   32-entry LRU stay in the INI file (they are never read again). Proposal: add `Settings::remove(group)` to
   `libs/core` and remove the group on eviction.
3. **Fonts in CI:** the workflow checks out without LFS (`lfs: false`), so CI builds embed the LFS pointer files;
   the GUI then logs a warning and uses the system font (tests do not depend on the fonts). Proposal: `lfs: true` for
   the build jobs, or accept it.
4. **Reduced motion on Linux:** no common desktop API; only `HIO_REDUCED_MOTION=1` works there. Proposal: accept, or
   read the GNOME `enable-animations` setting later.
5. **Sanitizer suppressions for the Qt Quick tests** (`lsan.supp`, `tsan-qml.supp`, see above): new files, limited
   to uninstrumented Qt / fontconfig and to `tst_gui.*`. Please confirm this scope (the alternative is to run the
   Qt Quick tests only in the non-sanitizer presets).

### Decisions on the implementation questions (2026-10-12, by Matej Strnad)

1. Target voltage in the device bar: amend 0002 — the connection event carries the measured target voltage (mV).
   New ADR-0091, implemented on `v2/m2-flashing` (backend) and merged here; then the device bar shows "Connected · 3.30 V".
2. Per-probe settings LRU: add `Settings::remove(group)` to `libs/core` (ADR-0092, on `v2/m2-flashing`), so evicted
   `probes/<sn>/*` groups are deleted.
3. CI: enable Git LFS checkout (`lfs: true`) in the build jobs, so the bundled fonts are used.
4. Sanitizer suppressions for the Qt Quick tests accepted as scoped (fontconfig leak in `lsan.supp`; Qt-internal races
   in `tsan-qml.supp` for `tst_gui.*` only). Reduced motion on Linux only via `HIO_REDUCED_MOTION=1` accepted.

## Open findings (independent review round 1, 2026-10-12)

Code reading only (no build; /tmp was full). Decided 2026-10-12: fix S1–S13, accept S14. S2: the tab session always opens on the console target with RTT, operations carry their target (ADR-0057) — contract amendment. S4: the scanner keeps one library copy loaded while a picker is visible; backend API on `v2/m2-flashing` (ADR-0093 amends ADR-0050). S5: the shell owns the connection markers incl. "— resumed —", F4 is global only, shortcut descriptions use the "Shortcuts" context; 0005 gets matching notes. Fixes after M2 is committed and merged.

| # | Severity | Finding | Suggested fix |
|---|---|---|---|
| S1 | medium | After a fatal connect failure (session Stopped, kept) the target can't be changed and there is no Disconnect; `ensureSession` returns a dead Stopped session. | Treat Stopped like no session (release it in `setConsoleTarget` / `ensureSession`; `canChangeTarget` by state). |
| S2 | medium | `ensureSession` opens the tab's session on the page's target / `attachRtt`: after flashing `lte` from a disconnected `app` tab the tab is "Connected · Target: app" but on `lte` without RTT; console stays empty. | Contract: the session always opens on the console target with RTT; operations carry their target (ADR-0057). |
| S3 | medium | `shutdown()` destroys the session without `sessionChanged`; page view models (children) outlive it → use-after-free for 0005/0006 that keep the pointer. | Emit `sessionChanged()` in `shutdown()` and document; pages hold `QPointer`. |
| S4 | medium | Every 2 s probe scan copies (29 MB) and loads the JLinkARM library (`enumerateProbes` → `loadCopy`); AV scans each copy on Windows. | Scanner keeps one library copy loaded while a picker is visible (reload on location change). |
| S5 | medium | Contract conflicts with 0005: connection markers appended by the tab and by 0005's console VM (duplicates; "resumed" unowned); F4 global vs 0005's console-scope F4 (registry rejects); shortcut description translation context unspecified. | Amend the contract (marker owner, F4 global only, `QT_TRANSLATE_NOOP("Shortcuts", …)`) and record in 0005. |
| S6 | medium | Close confirmation uses a tab index that can shift while the dialog is open → closes the wrong (idle) tab without confirmation. | Carry a stable tab id / pointer. |
| S7 | low | "Already running" dialog: always English, ignores theme, builds a fallback AppViewModel (scanner thread, registry cleanup). | Minimal context for the dialog; load settings + language first. |
| S8 | low | Our own probe lock shows "In use by another program (hio-monitor, PID self)" after disconnect until the next scan. | Ignore own-PID owners on the GUI thread. |
| S9 | low | Language change rebuilds all pages (QVariantList model) → page QML state lost. | Page list as `QAbstractListModel` with `dataChanged` for titles. |
| S10 | low | `dismissError` doesn't clear the tab's `hasError`; Stopped state text keeps the cause. | Connect `errorChanged`; clear consistently. |
| S11 | low (plausible) | Single-instance socket `/tmp/hio-monitor-<user>` can be squatted by another local user; any `listen()` failure turns later launches into the "does not respond" dialog. | Socket in `XDG_RUNTIME_DIR` / AppLocalData (full path); listen failure = error. |
| S12 | low (plausible) | `SettingsStore` and members destroyed before tabs/page VMs pending `deleteLater` → future UAF. | Delete remaining children explicitly first in `~AppViewModel`. |
| S13 | low | Czech device-bar state elided at 960 × 600 ("Odpojeno — J-Link n…"); layout test checks only buttons. | Tooltip/wrap or shorter text; extend the geometry test. |
| S14 | note | `tsan-qml.supp` `called_from_lib:libQt6Core.so.6` is broad but acceptable for `tst_gui.*` only; duplicates `tsan.supp` entries (sync hazard). | Accept; comment on the duplication. |

| S15 (user report 2026-10-12) | low | The RTT latency spin box steps from 1 ms to 11 ms (not 10); J-Link speed likewise: Qt `SpinBox` adds `stepSize` without snapping. | +/− (buttons, arrow keys, wheel) snap to the step grid (1 → 10 → 20, 15 → 10); typed values are kept; one shared component for both boxes; Qt Quick test. Decided: fix with S1–S13. |

### Fixes of review round 1 (2026-10-12)

All fixed on `v2/m3-gui` on top of the merge of `v2/m2-flashing` (55c348c); tests in `app/gui/tests`.

| # | Fix | Tests |
|---|---|---|
| S1 | A stopped session counts as none: `canChangeTarget` by state, `setConsoleTarget` and `ensureSession` release it, Connect replaces it. | `DeviceTabTest.AStoppedSessionIsReplaced` |
| S2 | `ensureSession()` (no parameters) opens the console target with RTT; contract amended. | `DeviceTabTest.EnsureSessionOpensTheConsoleTarget` |
| S3 | `shutdown()` emits `sessionChanged()` before the session is deleted; contract: pages keep `QPointer` / re-read. | `DeviceTabTest.ShutdownTellsThePages` |
| S4 | `ProbeScanner` keeps one `jlink::ProbeEnumerator` (ADR-0093) while a picker is visible, unloads it when none is, reloads on another library location; a Refresh without a picker uses a copy for that scan only. | `ProbePickerTest.OneLibraryCopyWhileAPickerIsVisible` |
| S5 | The shell owns all markers incl. "— resumed —"; F4 global only; page shortcut descriptions in the "Shortcuts" context; contract amended, notes in 0005. | `DeviceTabTest.BusyStateAndText`, `ShellShortcuts.TheShellMapHasNoDuplicates` |
| S6 | The close confirmation carries the tab (`closeConfirmationNeeded(DeviceTabViewModel*, text)`, `closeTabObject()`); the dialog keeps a guarded tab property. | `DeviceTabTest.CloseConfirmationCancelsAndCloses`, `Shell::test_contractPageBusyAndCloseConfirmation` |
| S7 | The "already running" dialog gets a minimal context in `main()` (settings, `SettingsStore` with the shell keys, `LanguageManager`, theme as initial property); it no longer uses `Theme` / `AppViewModel` (its two accent colours are the `Theme` values). | `AlreadyRunning::test_startAnyway`, `AlreadyRunning::test_quit` |
| S8 | A lock owner with this process's PID is not shown as "another program". | `ProbePickerTest.OwnLockIsNotAnotherProgram` |
| S9 | `AppViewModel.pages` is a `PageListModel` (roles `pageId`, `title`, `iconName`, `qmlUrl`, `isGlobal`); a language switch emits `dataChanged` for the titles only (`revision` for `title(id)` bindings). | `AppViewModelTest.PageModelUpdatesTitlesOnly`, all `tst_gui.*` |
| S10 | `errorChanged` updates the tab's `hasError`; the Stopped state text and kind use the dismissible error. | `DeviceTabTest.DismissedErrorClearsTheTabError` |
| S11 | The server is a socket path in `$XDG_RUNTIME_DIR` (else AppLocalData; named pipe on Windows); if `listen()` fails the lock is released (single-instance off for that run, logged as critical). | `SingleInstanceTest.AListenFailureTurnsSingleInstanceOff`, `SingleInstanceNames.DefaultServerNameIsPrivate` |
| S12 | `~AppViewModel` deletes closed tabs still waiting for `deleteLater()` and the global page view models before the members. | `AppViewModelTest.DestroyedWithClosedTabsPending` (ASan) |
| S13 | The device-bar state text wraps to two lines (tooltip when still truncated); the layout test asserts it is not truncated at 960 × 600 and 1099 px in Czech. | `Layout::test_deviceBar` |
| S14 | Accepted; `tsan-qml.supp` notes that it repeats `tsan.supp`. | — |
| S15 | `StepSpinBox.qml` (speed and latency): +/− buttons, Up/Down and the wheel step on the grid via `AppViewModel.stepValue()`; typed values stay exact. | `AppViewModelTest.StepValueSnapsToTheGrid`, `Components::test_stepSpinBox` |
| ADR-0091 | "Connected · 3.30 V" from `ConnectionEvent::targetMillivolts` (nothing when empty; decimal comma in Czech). | `ConnectionTest.StateTextForEveryConnectionEvent`, `ConnectionTest.VoltageFollowsTheLocale`, `Shell::test_chooseDeviceAndConnect` |
| ADR-0092 | LRU eviction removes the `probes/<sn>` group (`SettingsStore::removeGroup` → `Settings::remove`). | `ConnectionTest.AtMost32ProbesAreRemembered` |
| ADR-0111 | Merge of M1 round 6 (ADR-0090 automatic speed, ADR-0110 attach deadline): "Automatic speed" switch (on by default) in the ⋯ popover, kHz box disabled while automatic (keeps the fixed value, 4000 kHz by default), the ADR-0110 warning below 125 kHz; `ConnectionViewModel::autoSpeed` / `speedKhz` / `speedWarning`, `ConnectionOptions::sessionSpeedKhz()` → `SessionOptions::speedKhz` (nullopt = automatic); speed keys store 0 = automatic. The GUI has no fixed connect timeout (it shows the session's events), so `connectTimeoutForSpeed()` is not needed there. | `ConnectionTest.AutomaticSpeedByDefault`, `.StoredSpeedZeroIsAutomatic`, `.StoredFixedSpeedStaysFixed`, `.OutOfRangeStoredSpeedIsAutomatic`, `.PerProbeSpeedWinsOverTheGlobalOne`, `.SlowSpeedWarningBelow125Khz`, `Shell::test_advancedPopoverSpeed`, screenshots 08/09 |
| CI | `lfs: true` for the checkout of the build jobs (the check job keeps `lfs: false` for `check-lfs.sh`). | — |
| (S4 follow-up, backend) | `libs/hw/jlink/src/jlink_library.cpp`: loading and unloading of library copies are serialized by one process-wide mutex. With S4 the scanner unloads its copy on its own thread while a session may load one; a new copy can be mapped where the old one was, and ld.so's locking is invisible to TSan, which reported the fake library's state as a race (`tst_gui.tst_shell` under TSan). Deviation from "no change to `libs/*`"; to be confirmed. | `tst_gui.tst_shell` (TSan), the 30× TSan repeat |


## Open findings (independent review round 2, 2026-10-12)

Review of ee47114 (code reading + one qmltestrunner check). No high findings. Decided 2026-10-12: fix all S16–S22 (S19: keep the lock, document it, scanner never holds it on a GUI-critical path), then commit; 0005 starts only after a separate approval.

| # | Severity | Finding | Suggested fix |
|---|---|---|---|
| S16 | medium (confirmed) | `StepSpinBox` assigns `box.value` from JS, which breaks the `value:` binding to the view model: after one step, option changes made by the VM (probe change → `loadOptions`) are not shown; the next connect uses a different value than displayed. | Don't assign `value`: emit `stepped(newValue)` and let the owner write the VM (or a C++ `increase()` path); test that changes the bound source after a step. |
| S17 | medium | When `ProbeEnumerator` load/enumerate fails (e.g. J-Link < 8.00, missing exports), nothing remembers it: the 29 MB copy + load repeats every 2 s. | Cache the failure per location (path + size + mtime); retry on Refresh / location or file change. |
| S18 | low–medium | A fatally Stopped session counts as "none" in the UI but keeps the cross-process probe lock, the library copy and a registry slot; the bar offers only Connect → `hio` CLI fails with "in use by hio-monitor". `sessionStopped()` is also true for a deliberate `suspend()`. | Release a fatally Stopped session automatically (keep its error in the ConnectionViewModel); only treat `Stopped && error` as dead. |
| S19 | low (plausible) | The process-wide load/unload mutex can freeze the GUI on Disconnect/close/quit while the scanner loads a fresh copy on Windows (antivirus, seconds). No deadlock found. | Accept and document, or scanner unload/load off the critical path (e.g. scanner never holds the lock across AV-slow steps); or limit to TSan builds. |
| S20 | low | `StepSpinBox` steps on every wheel event (touchpad small deltas → huge jumps); holding +/− no longer auto-repeats. | Accumulate `angleDelta.y` per 120; restore auto-repeat. |
| S21 | low (plausible) | Socket path from `XDG_RUNTIME_DIR` but lock file in the home dir: sessions with different runtime dirs (ssh, `su -l`) → "does not respond" instead of activation. | Derive the socket path from the lock-file directory, or accept. |
| S22 | note | CI `lfs: true` in 11 jobs without an LFS cache (bandwidth quota); no tests for scanner reload on location change and one-shot Refresh copy. | Cache `.git/lfs` in CI; add the two tests. |

### Fixes of review round 2 (2026-10-12)

Uncommitted on top of ee47114; tests in `app/gui/tests`.

| # | Fix | Tests |
|---|---|---|
| S16 | `StepSpinBox` never assigns `value`: a step emits `stepped(newValue)` and `AdvancedPopover` writes the view model, so the `value:` binding stays (typed values arrive through `valueModified`, the SpinBox's own setter). | `Components::test_stepSpinBox` (changes the bound source after steps and after typing) |
| S17 | The scanner remembers a library that failed to load or list probes (path + size + modification time) and does not copy/load it on the 2 s tick; a Refresh (or a picker becoming visible), another location or a changed file retries. New counter `libraryLoadAttempts()`. | `ProbePickerTest.AFailedLibraryIsNotLoadedOnEveryTick` (incomplete fake library) |
| S18 | A session that stops with an error is released on the next event-loop turn (probe lock, library copy, registry slot); its error stays in the `ConnectionViewModel` (bar text, ErrorPanel, red dot unchanged). `sessionFailed()` = Stopped **with** an error; a Stopped session without one (deliberate `suspend()`) is a working session. | `DeviceTabTest.AFatallyStoppedSessionIsReleased`, `DeviceTabTest.TheProbeLockIsFreeAfterAFatalConnectError` |
| S19 | Kept the process-wide load/unload lock (ADR-0093 amendment); documented in ADR-0093, `jlink_library.cpp` and `probe_scanner.h`: `DeviceSession::close()` (joined by the GUI thread on Disconnect, tab close, quit) unloads its copy under the lock and so can wait for one scanner load in progress (seconds with a Windows antivirus). Mitigation within app/gui: the scanner loads rarely (picker shown, location/file change, Refresh after a failure; never on the tick thanks to the kept copy and S17) and never takes the lock on the GUI thread. No further libs change. | — (documentation) |
| S20 | Wheel deltas accumulate, one step per 120 (touchpads); held +/− auto-repeat (300 ms delay, then every 100 ms). | `Components::test_stepSpinBox` |
| S21 | The socket is `gui.sock` next to the lock file (AppLocalData; owner-only socket via `UserAccessOption`); a path too long for a Unix socket falls back to the runtime directory. Windows unchanged (named pipe). | `SingleInstanceTest.ActivationThroughASocketBesideTheLock`, `SingleInstanceTest.ASecondInstanceActivatesTheFirst` (now with a socket path beside the lock), `SingleInstanceNames.DefaultServerNameIsPrivate` |
| S22 | CI build jobs check out without LFS, restore `.git/lfs` from `actions/cache` keyed by the LFS object list (`git lfs ls-files --long`) and `.gitattributes`, then `git lfs pull`; `.lfs-assets-id` is ignored. Tests for the reload on a location change and for the one-shot Refresh copy. | `ProbePickerTest.AnotherLibraryLocationLoadsANewCopy`, `ProbePickerTest.ARefreshWithoutAPickerUsesAOneShotCopy` |

