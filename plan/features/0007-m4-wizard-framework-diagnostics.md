# 0007 — M4 wizard framework and diagnostics: `libs/diag`, `Diagnose`, `doctor`, report, wizard dialog

- **Status:** Draft
- **Milestone:** M4 (plan 1 of 4, ADR-0065; followed by [0008](0008-m4-first-run-setup.md),
  [0009](0009-m4-connect-and-troubleshooting.md), [0010](0010-m4-flash-wizard.md))
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0003, ADR-0004, ADR-0007, ADR-0017, ADR-0020, ADR-0022, ADR-0024, ADR-0025, ADR-0026,
  ADR-0027, ADR-0031, ADR-0041, ADR-0057, ADR-0058, ADR-0059, ADR-0062, ADR-0065–ADR-0070, ADR-0072, ADR-0073,
  ADR-0075, ADR-0076, ADR-0077
- **Depends on:** 0002 (M1), 0003 (M2), [0004](0004-m3-gui-shell.md) (M3 shell: components, `ErrorInfo`,
  page-extension contract). The backend part (phases A–B) can start after M1 and M2; the GUI part (phase C) after
  0004.
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

> **Pending amendment from M5 ([0011](0011-m5-ble-backend-cli.md), [0012](0012-m5-ble-gui-pairing.md), ADR-0088;
> applied when they are approved — then this plan needs re-approval):** environment check `bluetooth` (after
> `app-dirs`, Warning only, 5 s) with `EnvironmentContext::ble`; `WizardStep::embedded` gains `ble-picker`;
> illustration slots `ble-enable`, `ble-pair-os`; Help menu item "Pair a Bluetooth device…".

## Context

Every `hio::Error` already names a wizard (`WizardId`, M0) and the M1/M2 error tables map about 25 error codes to
one. The M3 ErrorPanel shows "Help me fix this" disabled ("Coming soon"). Nothing checks the user's environment:
neither the old app nor py-hardwario detects a missing J-Link installation, USB permissions, a program holding the
probe or an unpowered target before failing, and support has no way to collect versions and logs.

This plan builds what the three wizard plans (0008–0010) plug into:
- the backend checks and the diagnostic report (`libs/diag`, ADR-0068);
- the live connection check as a session operation (`Diagnose`, ADR-0069);
- their CLI commands (`hio doctor`, `hio diag report`, `hio chester app|lte check`);
- the GUI wizard engine and dialog (ADR-0066, ADR-0067), the launcher behind "Help me fix this", the Help menu
  and the diagnostic-report dialog;
- the **wizard contract** for 0008–0010.

## Goals / Non-goals

**Goals**
- `libs/diag`: `CheckRunner`, environment checks, process scan for probe holders (ADR-0073), Linux USB access check
  (ADR-0072), report builder with redaction (ADR-0075).
- `Diagnose` session operation and probe-only sessions (`connectTarget = false`), as an amendment to 0002/0003.
- CLI: `hio doctor`, `hio diag report`, `hio chester app|lte check`, with human and `--json` output (ADR-0070).
- GUI:
  - `WizardViewModel` + `Wizard` base class + `WizardRegistry` (contract for 0008–0010);
  - components `WizardDialog`, `WizardStepList`, `CheckList`, `CommandBlock`, `Illustration` (an amendment to 0004's
    component set);
  - the wizard launcher: "Help me fix this" becomes active whenever a registered wizard handles the error;
  - Help menu at the rail's Help item: Keyboard shortcuts (F1), Setup wizard…, Connect a device…, Troubleshoot
    connection…, Flash firmware (guided)…, Create diagnostic report…, Documentation ↗;
  - diagnostic report dialog;
  - illustration asset slots and placeholders (ADR-0076);
  - the connection guide in the device profile with the CHESTER guide (ADR-0077), used by 0009 and 0010.
- New error codes (below) with EN + CS texts.

**Non-goals**
- The wizard flows themselves: setup (0008), connect + troubleshooting (0009), flash (0010). This plan ships one
  test wizard to exercise the contract.
- J-Link firmware update button, documentation links per error, PIB/UICR read in diagnostics (open questions 19–21).
- BLE pairing wizard and macOS Bluetooth permission (M5). Catalog browsing (open question 15).
- MCP tools for the checks (M6 reuses `libs/diag`).

## User experience

### "Help me fix this"

- Shown in every ErrorPanel whose `ErrorInfo.wizard` is non-empty **and** registered. It is now enabled; the
  "Coming soon" tooltip is removed. Until a wizard plan lands, its id is not registered and the button is hidden
  (not disabled).
- Click → `WizardLauncher::open(wizardId, errorInfo, tab)`. The wizard opens with the error as its context (the
  troubleshooting topic, the setup step to start at, the flash step to start at).
- If a wizard is already open, it is brought to the front (ADR-0066).

### Help menu

The rail's Help item (bottom) opens a menu instead of the shortcut overlay. F1 / Ctrl+/ still open the overlay
directly.

| Item | Opens |
|---|---|
| Keyboard shortcuts (F1) | shortcut overlay (0004) |
| Setup wizard… | `first-run-setup` (0008) |
| Connect a device… | `connect-device` on the current tab (0009) |
| Troubleshoot connection… | `troubleshooting` without an error → topic list (0009) |
| Flash firmware (guided)… | `flash-firmware` on the current tab (0010) |
| Create diagnostic report… | report dialog (this plan) |
| Documentation ↗ | `https://docs.hardwario.com/chester/platform-connectivity/hardwario-monitor/` |

Items whose wizard is not registered are hidden.

### Wizard dialog (ADR-0066)

```
┌─ Connect your CHESTER ──────────────────────────────── × ┐
│ ✓ Probe ─ ● Cable ─ ○ Power ─ ○ Check                    │
│                                                          │
│   [illustration 480×270]                                 │
│   Connect the J-Link flat cable to the programming       │
│   connector of the application MCU.                      │
│                                                          │
│                               [Back]  [Next]  [Cancel]   │
└──────────────────────────────────────────────────────────┘
```

- Header: title, step list (done ✓ / current ● / pending ○); below 800 px window width "Step 2 of 4 · Cable".
- Step kinds (ADR-0067):
  - **Info** — illustration (optional), heading, body text, optional link.
  - **Checks** — `CheckList`: one row per check with a status icon (spinner while running, ✓, !, ✗, –), title,
    result text; a failed row expands to the error's cause and suggestion; [Check again] in the footer.
  - **Choice** — radio cards (as the Flash page's kind cards).
  - **Embedded** — a named control: the probe picker (0004), the firmware source card (0006), the PhaseStepper.
  - **Commands** — `CommandBlock`: monospace lines, each with [Copy].
  - **Done** — success mark, summary, hand-off buttons (e.g. [Open console]).
- Footer: [Back] (hidden on the first step and while busy), the default button ([Next] / [Check again] / [Finish] /
  the step's action), [Cancel]. Enter = default button, Esc = Cancel.
- Cancel while the wizard runs an operation: "Flashing is in progress. Cancel it and close the wizard?"
  [Keep going] [Cancel and close] (the text comes from the step). Otherwise Cancel closes immediately.
- Accessibility: the dialog has `Accessible.name` = title; step changes are announced ("Step 2 of 4, Cable");
  check results are announced when they change; focus moves to the step heading on each step change.

### Check list texts (environment checks, shared by `hio doctor` and the wizards)

| Id | Title | Ok text | Not ok |
|---|---|---|---|
| `jlink-software` | J-Link Software | "8.10 · /opt/SEGGER/JLink" | Failed: `dependency.jlink_not_found` / `_load_failed` / `_too_old` |
| `nrfutil` | nRF Util (only for LTE modem firmware) | "7.13.0, device 2.7.4" | Warning: `dependency.nrfutil_*` |
| `usb-access` | USB access to J-Link (Linux) | "Access granted" | Failed: `usb.access_denied`; Skipped "No J-Link connected" |
| `probe-holders` | Other programs using J-Link | "None found" | Warning: "J-Link Commander (PID 4242) is running" (`probe.held_by_program`) |
| `probes` | J-Link probes | "2 found: 683012345, 801004417" | Warning: `probe.not_found` (not Failed: doctor also runs without a probe) |
| `probe-firmware` | J-Link firmware | "Current" | Warning: `probe.firmware_outdated` |
| `app-dirs` | Settings and log folders | "Writable" | Failed: `io.write_failed` |

Order of execution = order of the table. `jlink-software` Failed → `probes` and `probe-firmware` Skipped.

### Diagnose step texts (shared by `hio chester … check` and the wizards)

| Id | Title | Ok text | Not ok |
|---|---|---|---|
| `probe` | J-Link probe | "J-Link EDU Mini · 683012345" | `probe.*` error |
| `power` | Device power | "3.30 V" | `target.no_power` |
| `connect` | Connection to the %1 MCU | "Connected (SWD 4000 kHz)" | `target.connect_failed` |
| `identify` | MCU type | "nRF52840, not protected" | `target.wrong_family`; Warning `target.protected` |
| `rtt` | Device console (RTT) | "Found" | `target.rtt_not_found` |
| `rtt-buffers` | Console channels | "Terminal, Logger" | `target.rtt_buffer_not_found` |

### Diagnostic report dialog (ADR-0075)

- Title "Create diagnostic report". While collecting: "Collecting information…" with a spinner (the doctor checks
  run; ≤ 15 s), [Cancel].
- Then: the read-only `summary.txt` in a monospace scroll area; the text "The report contains this summary, the
  recent application logs and the settings file. Your user name and home folder are replaced. The report stays on
  your computer — attach it to your support request."; buttons [Save report…] (default) [Copy summary] [Close].
- Save → native file dialog with `hio-diagnostics-YYYYMMDD-HHMMSS.zip` in the Documents folder. Success → toast
  "Diagnostic report saved." with [Show in folder]. Failure → ErrorPanel in the dialog (`diag.report_write_failed`).

### Shortcuts

None added; F1 / Ctrl+/ unchanged.

### Texts

English source texts as above. Czech is translated in Qt Linguist as part of done (ADR-0027).

## CLI

### `hio doctor [--json]`

```
$ hio doctor
✓ J-Link Software            8.10 · /opt/SEGGER/JLink
! nRF Util                   not found — needed only for LTE modem firmware
✓ USB access to J-Link       access granted
! Other programs using J-Link  J-Link Commander (PID 4242) is running
✓ J-Link probes              1 found: 683012345
✓ J-Link firmware            current
✓ Settings and log folders   writable

2 warnings. Run `hio diag report` to create a report for HARDWARIO support.
```

`--json`: ADR-0070 envelope. Exit code per ADR-0070. `--jlink-library` and `--nrfutil` overrides apply.

### `hio diag report [-o FILE] [--json]`

Writes the zip (default `./hio-diagnostics-YYYYMMDD-HHMMSS.zip`), prints the path and the summary's first lines.
`--json`: `{"ok":true,"data":{"path":"…","bytes":123456}}`. Errors: `diag.report_write_failed` (exit 3).
`-o -` is rejected (`usage.invalid_argument`): binary output to a terminal is not supported.

### `hio chester app|lte check [-n SN] [--speed KHZ] [--json]`

Opens a probe-only session (`connectTarget = false`), runs `Diagnose`, prints one line per step as it finishes
(NDJSON `{"type":"check",…}` lines with `--json`, then the final envelope). Exit code per ADR-0070. Ctrl+C → exit
130.

## Design

### Modules

```
libs/diag/                                  new static library (ADR-0022), QtCore only (ADR-0004)
  include/hio/diag/
    check.h            CheckStatus, CheckResult, Check, CheckRunner
    environment.h      environmentChecks(EnvironmentContext) → std::vector<Check>
    process_scan.h     ProcessMatch, findProbeHolders(), IProcessSource (for tests)
    usb_access.h       UsbAccessResult, checkUsbAccess() (Linux; others → Skipped)
    report.h           ReportInput, ReportBuilder, redact()
  src/ check.cpp environment.cpp process_scan.cpp usb_access.cpp report.cpp redact.cpp
      platform/ process_source_linux.cpp process_source_macos.cpp process_source_windows.cpp
  tests/ (GTest; fake J-Link library from 0002, fake nRF Util from 0003, fake process source, temp sysfs tree)
libs/device/   + Diagnose operation, connectTarget, ProbeOpen state (amendment, below); connection guide in
               TargetProfile + CHESTER guide (ADR-0077)
libs/core/     + error codes (below)
app/cli/       + commands doctor, diag report, chester app|lte check
app/gui/
  src/wizard/  wizard.* wizard_view_model.* wizard_registry.* wizard_launcher.* check_list_model.*
               report_view_model.* test_wizard.* (tests only)
  src/workers/ diag_worker.*   owned std::jthread running CheckRunner / ReportBuilder off the GUI thread
  qml/components/ WizardDialog.qml WizardStepList.qml CheckList.qml CommandBlock.qml Illustration.qml
  qml/dialogs/ ReportDialog.qml   qml/shell/HelpMenu.qml
  assets/wizard/ (Git LFS) placeholder SVGs per slot + README.md (slot table)
```

### `libs/diag` interfaces (sketch, `hio::diag`)

```cpp
enum class CheckStatus : std::uint8_t { Pending, Running, Ok, Warning, Failed, Skipped };

struct CheckResult {
    std::string id;                          // kebab-case, stable (JSON, tests)
    CheckStatus status = CheckStatus::Pending;
    std::string title;                       // translated at creation (ADR-0027)
    std::string result;                      // short translated result text
    std::optional<hio::Error> error;         // Warning / Failed only
};

struct Check {
    std::string id;
    const char* titleSourceText;             // QT_TRANSLATE_NOOP("hio::diag", …)
    std::chrono::milliseconds timeout;       // per check, ≤ 15 s
    std::vector<std::string> requires;       // ids that must be Ok/Warning, else Skipped
    std::function<CheckResult(std::stop_token)> run;   // pure function of its context, no Qt objects
};

class CheckRunner {                          // runs on the caller's thread
public:
    using Callback = std::function<void(const CheckResult&)>;   // after each check (and Running before it)
    [[nodiscard]] static std::vector<CheckResult> run(std::span<const Check>, std::stop_token, const Callback&);
};

struct EnvironmentContext {                  // overrides as in M1/M2 (CLI option > env > setting > auto)
    hw::jlink::LocatorOptions jlink;
    hw::nrfutil::LocatorOptions nrfutil;
    std::filesystem::path settingsDir, logDir;
    std::shared_ptr<IProcessSource> processes;   // nullptr = the platform source
};
[[nodiscard]] std::vector<Check> environmentChecks(const EnvironmentContext&);

struct ProcessMatch { std::string displayName; std::string executable; qint64 pid; };
class IProcessSource {                       // platform implementation or test fake
public:
    virtual ~IProcessSource() = default;
    struct Process { qint64 pid; std::string name; std::string executablePath; };
    [[nodiscard]] virtual Result<std::vector<Process>> list(std::size_t maxCount, std::stop_token) = 0;
};
[[nodiscard]] Result<std::vector<ProcessMatch>> findProbeHolders(IProcessSource&, std::stop_token);

struct UsbDeviceAccess { int bus; int device; std::string product; bool accessible; };
[[nodiscard]] Result<std::vector<UsbDeviceAccess>> checkUsbAccess(const std::filesystem::path& sysfsRoot = "/sys",
                                                                  const std::filesystem::path& devRoot = "/dev");

struct ReportInput {
    std::vector<CheckResult> doctor;
    QJsonObject app;                         // version, git hash, Qt, compiler, language
    QJsonArray probes, tabs;                 // tabs: GUI only
    std::vector<std::filesystem::path> logFiles;   // current + previous of GUI and CLI
    std::filesystem::path settingsFile;
};
class ReportBuilder {
public:
    [[nodiscard]] static std::string summaryText(const ReportInput&);        // redacted
    [[nodiscard]] static QJsonObject summaryJson(const ReportInput&);        // redacted
    [[nodiscard]] static Status writeZip(const ReportInput&, const std::filesystem::path& target, std::stop_token);
};
struct RedactionContext { std::string home, user, host; bool caseInsensitive; };
[[nodiscard]] std::string redact(std::string_view text, const RedactionContext&);
}
```

- `writeZip` writes to `<target>.part` via `QSaveFile` semantics (temporary + rename) so a failed write leaves no
  partial zip. Each log is read from its end (last 2 MiB, cut at the next line start) and redacted line by line.
- `LibraryLocator::locate` and the version check run inside the `jlink-software` check; `probes` and
  `probe-firmware` use `ProbeEnumerator` with a library copy loaded for the check and unloaded afterwards
  (ADR-0030; the copy lives in its own instance directory and is removed at the end).
- The process scan excludes `QCoreApplication::applicationPid()` and processes named `hio` / `hio-monitor`
  (they use probe locks; their holders are reported through `inUseBy`).

### Amendment to 0002 / 0003 / ADR-0062 (ADR-0069)

Applied to the text of 0002 and 0003 when this plan is approved (both then need re-approval):

```cpp
// libs/device — SessionOptions
bool connectTarget = true;     // false: load library, open probe, take ProbeLock, report ProbeOpen; no target connect,
                               // no RTT, no power polling, no auto-reconnect. Only operations touch the target.
// ConnectionState gains ProbeOpen (between Connecting and Connected; terminal state for connectTarget = false)

enum class OperationKind { Flash, EraseCode, EraseAll, Recover, Modem, LteBundle, Reset, ReadMemory, Diagnose };
struct DiagnoseStep { std::string id; diag::CheckStatus status; std::string result; std::optional<hio::Error> error; };
// OperationResult gains: std::vector<DiagnoseStep> checks;   // Diagnose only, ≤ 16
// OperationProgress for Diagnose: phase = Connecting, step = index of the current check (1-based), steps = 6,
//                                 plus signal stepFinished below
[[nodiscard]] Result<OperationId> diagnose(std::string_view targetId);
// Promotes a probe-only session (ProbeOpen) to a normal one: connect the target, attach RTT when asked, start power
// polling and auto-reconnect per SessionOptions. The probe and its ProbeLock stay held. Other states → usage.invalid_argument.
[[nodiscard]] Status startConnection(bool attachRtt);
signals: void diagnoseStep(hio::device::OperationId, const hio::device::DiagnoseStep&);   // queued, one per step
```

- Diagnose order and behaviour as ADR-0069. It never resets, halts or writes. For a target that is the console
  target of a connected session, the RTT steps check the running attachment instead of re-attaching. For another
  target, `TargetSwitch` applies (pause RTT → switch → diagnose → restore).
- `power` reads `hwStatus()`; `connect` uses the profile's J-Link device name and the session's speed, one attempt
  (no 3 × 500 ms retry, to report quickly); `identify` reads FICR (0003 `identify`); `rtt` uses the RTT search with
  a 3 s budget; `rtt-buffers` compares the found buffer names with the profile's.
- `DiagnoseStep` lives in `libs/device` and reuses `diag::CheckStatus`, so `libs/device` depends on the header-only
  part of `libs/diag` (`check.h`). `libs/diag` does not depend on `libs/device` except for the probe enumeration
  helpers in `environment.cpp` (no cycle: `check.h` is a separate interface target `hio::diag_types`).
- Bound: Diagnose total ≤ 15 s (each step's timeout is in the table in 0009).

### GUI wizard contract (ADR-0067; 0008–0010 implement it)

```cpp
namespace hio::gui {
enum class StepKind { Info, Checks, Choice, Embedded, Commands, Done };

struct WizardStep {                                  // value type, rebuilt by the wizard when it changes
    QString id;                                      // "cable", "power", …
    StepKind kind;
    QString title;                                   // short, shown in the step list (translated)
    QString heading, body;                           // translated; body may hold simple rich text (<b>, <br>)
    QString illustration;                            // slot id (ADR-0076) or ""
    QString embedded;                                // Embedded: "probe-picker" | "firmware-source" | "phase-progress"
    QStringList commands;                            // Commands
    QVariantList choices;                            // Choice: [{id, title, subtitle}]
    QString primaryText;                             // label of the default button ("Next", "Check again", "Flash")
    bool showInStepList = true;
};

class Wizard : public QObject {                      // one subclass per wizard; parent = WizardViewModel
    Q_OBJECT
public:
    [[nodiscard]] virtual QString id() const = 0;    // kebab-case (ADR-0067)
    [[nodiscard]] virtual QString title() const = 0;
    // Called once; context = the error that launched it (may be invalid) and the tab (nullptr for global wizards).
    virtual void start(const ErrorInfo& context, DeviceTabViewModel* tab) = 0;
    [[nodiscard]] virtual std::vector<WizardStep> plannedSteps() const = 0;   // for the step list
    [[nodiscard]] virtual WizardStep current() const = 0;
    virtual void primary() = 0;                      // default button
    virtual void back() = 0;
    virtual void choose(const QString& choiceId) {}  // Choice steps
    [[nodiscard]] virtual bool canBack() const = 0;
    [[nodiscard]] virtual bool canPrimary() const = 0;
    [[nodiscard]] virtual QString busyCancelText() const { return {}; }   // non-empty while an operation runs
    virtual void cancelOperation() {}
    [[nodiscard]] virtual CheckListModel* checks() { return nullptr; }    // Checks steps
    [[nodiscard]] virtual QObject* embeddedViewModel() { return nullptr; } // Embedded steps (picker, source, progress)
signals:
    void changed();                                  // step, buttons or checks changed
    void finished(bool completed);                   // the dialog closes
    void handOff(const QString& action);             // "open-console", "open-flash-page", "open-settings"
};

struct WizardDescriptor {
    WizardId id;
    bool perTab;                                     // false: global (setup)
    std::function<Wizard*(QObject* parent)> create;  // Qt parent ownership
};

class WizardRegistry {                               // owned by AppViewModel; filled at start-up by each wizard plan
public:
    void add(WizardDescriptor);
    [[nodiscard]] bool has(WizardId) const;
};

class WizardViewModel : public QObject {             // exposed as AppViewModel.wizard; one at a time
    Q_OBJECT
    Q_PROPERTY(bool open ...) Q_PROPERTY(QString title ...)
    Q_PROPERTY(QVariantList steps ...) Q_PROPERTY(int currentIndex ...)   // step list
    Q_PROPERTY(QVariantMap step ...)                 // current WizardStep as a map for QML
    Q_PROPERTY(bool canBack ...) Q_PROPERTY(bool canPrimary ...) Q_PROPERTY(QString busyCancelText ...)
    Q_PROPERTY(CheckListModel* checks ...) Q_PROPERTY(QObject* embedded ...)
    Q_INVOKABLE void primary(); Q_INVOKABLE void back(); Q_INVOKABLE void choose(const QString&);
    Q_INVOKABLE void cancel(bool force = false);     // force = after the user confirmed busyCancelText
    Q_INVOKABLE void copyText(const QString&);       // CommandBlock [Copy]
};

class WizardLauncher : public QObject {              // owned by AppViewModel
    Q_OBJECT
    Q_INVOKABLE bool canOpen(const QString& wizardId) const;          // registered?
    Q_INVOKABLE void open(const QString& wizardId, const hio::gui::ErrorInfo& context, int tabIndex = -1);
};

class CheckListModel : public QAbstractListModel {  // rows = diag::CheckResult / device::DiagnoseStep
    enum Roles { IdRole = Qt::UserRole + 1, StatusRole, TitleRole, ResultRole, ErrorRole /*ErrorInfo*/ };
    void reset(std::span<const QString> ids, std::span<const QString> titles);   // all Pending
    void update(const diag::CheckResult&);
};
}
```

- `ErrorInfo` (0004) is unchanged except that `wizard` is always the kebab id, and the ErrorPanel's wizard button
  binds `visible: AppViewModel.launcher.canOpen(error.wizard)`.
- `DeviceTabViewModel` (0004) gains `Q_INVOKABLE void openWizard(const QString& id)` (calls the launcher with this
  tab) — part of the 0004 amendment.
- The wizard that runs an operation calls the tab's `setBusy` / `clearBusy` like a page (page id = wizard id).
- Hand-off actions are handled by `AppViewModel`: `open-console` → `tab.currentPage = "console"` and connect if
  needed; `open-flash-page`; `open-settings` (with a section id).

### Data flow and threading

- **GUI thread:** all ViewModels, `WizardRegistry`, `WizardLauncher`, `CheckListModel`.
- **`DiagWorker`:** one owned `std::jthread` for the app (owned by `AppViewModel`), bounded request queue of 4
  (newest replaces pending of the same kind). Runs `CheckRunner` (environment checks), the start-up J-Link check
  (0008) and `ReportBuilder`. Results arrive by queued `QMetaObject::invokeMethod` with a request generation;
  stale results are dropped. Stop token honoured; joined on destruction.
- **Diagnose** runs on the tab's session worker (ADR-0031). The wizard opens a probe-only session through
  `DeviceTabViewModel::ensureSession(target, attachRtt = false, connectTarget = false)` when the tab has none
  (0004 amendment: one more parameter, default true).
- **CLI:** checks and the report run on the main thread (synchronous); `chester … check` uses a `QEventLoop` until
  `operationFinished` (like `waitUntilConnected`).

### Error codes added (texts translatable, ADR-0027)

| Code | Category | Message / cause / suggestion | Wizard |
|---|---|---|---|
| `usb.access_denied` | Probe | "This computer does not allow access to the J-Link (USB permissions)." / the udev rule for SEGGER devices is missing / "Install the udev rule shown in the setup wizard, then reconnect the J-Link." | FirstRunSetup |
| `probe.held_by_program` | Probe (warning) | "%1 (PID %2) is running and may be using the J-Link." / another debugging or programming tool keeps the probe open / "Close %1 and try again." | Troubleshooting |
| `diag.report_write_failed` | Io | "The diagnostic report could not be saved." / folder not writable or disk full / "Choose another folder." (path in detail) | — |
| `diag.check_timeout` | Timeout (warning) | "The check \"%1\" did not finish in time." / slow system or a hanging tool / "Run the checks again." | — |

Changes to existing codes:
- `probe.open_failed`: wizard stays Troubleshooting; on Linux the M1 text gains the cause "USB permissions" only
  when `usb.access_denied` is not detected separately (the session runs `checkUsbAccess` after an open failure
  and reports `usb.access_denied` instead when it applies).
- `probe.in_use_by_other_process`: wizard `—` → Troubleshooting (the topic explains how to close the other
  HARDWARIO Monitor or `hio`).
- 0006's row "Custom J-Link path invalid" writes `FirstRunSetup` as the C++ enumerator (ADR-0067); unchanged
  meaning.

### Settings keys added (ADR-0025)

None in this plan (0008 adds `setup/*`). The report dialog remembers no folder.

### Illustration slots (ADR-0076)

`app/gui/assets/wizard/`, 480 × 270 logical px, no text in images. This plan defines the mechanism and the
slots shared by 0008–0010 (0008 adds `setup-welcome`).

| Slot | Shows | Caption source text |
|---|---|---|
| `jlink-usb` | J-Link connected to a computer by USB | "Connect the J-Link to your computer with a USB cable." |
| `chester-app-cable` | flat cable to the application MCU's programming connector | from the profile guide |
| `chester-lte-cable` | flat cable to the LTE MCU's programming connector | from the profile guide |
| `chester-power` | CHESTER with batteries / USB-C power | from the profile power hint |
| `placeholder` | neutral frame (fallback) | — |

### Bounds

| Item | Limit |
|---|---|
| Checks per run | 16 |
| Per-check timeout | 15 s (`nrfutil` 10 s, process scan 2 s, others 5 s) |
| Processes inspected | 4096 |
| Diagnose steps / total time | 6 / 15 s |
| Report: per log file / zip / summary | last 2 MiB / 16 MiB / 256 KiB |
| Log files in report | 4 (current + previous of GUI and CLI) |
| DiagWorker queue | 4 |
| Wizards open | 1 |

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Split of M4 | framework + 3 / one per item / two / one | framework + 3 plans | ADR-0065 |
| Wizard host | modal dialog / page in tab / side drawer | modal dialog | ADR-0066 |
| Wizard engine | C++ step machine + QML steps / JSON / pure QML | C++ step machine | ADR-0067 |
| Checks module and CLI | libs/diag + CLI / spread over libs / no CLI | libs/diag + `doctor`, `diag report`, `chester … check` | ADR-0068 |
| Live connection checks | session operation / standalone thread / try to connect | `Diagnose` operation | ADR-0069 |
| Check exit codes | first failure's category / always 0 / new code | first failure's category | ADR-0070 |
| Linux USB access | detect + commands / pkexec / docs link | detect + commands | ADR-0072 |
| Programs holding the probe | ask only / process scan | process scan | ADR-0073 |
| Diagnostic report | zip + summary / text / zip + console logs; redaction | zip + summary, redact home/user/host | ADR-0075 |
| Illustrations | placeholders / drawn in M4 / photos first | slots + placeholders | ADR-0076 |
| Cabling guidance location | device profile / GUI code | device profile; CHESTER wording later | ADR-0077 |
| Firmware update button, doc links, PIB in report | in M4 / later | later (open questions 19–21) | — |

## Implementation steps

Phase A — backend checks (after M1, M2)
- [ ] A0 Prerequisite check: 0002 and 0003 implemented; this plan's amendment applied to their text and approved
- [ ] A1 `libs/diag` skeleton (CMake target, `hio::diag_types` interface target), `Check`, `CheckRunner` + tests
- [ ] A2 Environment checks `jlink-software`, `nrfutil`, `probes`, `probe-firmware`, `app-dirs` + tests (fake
  J-Link, fake nRF Util)
- [ ] A3 `checkUsbAccess` (Linux; temp sysfs/dev tree in tests) + `usb-access` check
- [ ] A4 `IProcessSource` + three platform sources + `findProbeHolders` + `probe-holders` check + tests (fake
  source; one real-source smoke test per OS)
- [ ] A5 `redact`, `ReportBuilder` (summary text/JSON, zip via miniz, log tails) + tests (golden summary, zip
  contents, bounds, redaction cases incl. Windows case-insensitivity)
- [ ] A6 Error codes + EN texts in `libs/core`
- [ ] A7 Connection guide in the device profile (ADR-0077): `ConnectionGuideStep`, `connectionGuide` per target,
  `powerHintSourceText`; CHESTER guide with neutral texts (open question 22) + test that every slot exists

Phase B — Diagnose and CLI
- [ ] B1 `connectTarget = false`, `ProbeOpen` state, `startConnection` in `DeviceSession` + tests (lock kept)
- [ ] B2 `Diagnose` operation (all six steps, skip rules, cross-target via `TargetSwitch`) + tests with the fake
  library (no power, wrong family, protected, no RTT, missing buffer, cancel)
- [ ] B3 CLI `hio doctor`, `hio diag report`, `hio chester app|lte check` + end-to-end tests (human + JSON, exit
  codes)

Phase C — GUI framework (after 0004)
- [ ] C1 `Wizard`, `WizardRegistry`, `WizardViewModel`, `WizardLauncher`, `CheckListModel` + test wizard + GTest
- [ ] C2 QML: `WizardDialog`, `WizardStepList`, `CheckList`, `CommandBlock`, `Illustration`; ErrorPanel wizard
  button wiring; Help menu; `DeviceTabViewModel::openWizard`, `ensureSession(…, connectTarget)`
- [ ] C3 `DiagWorker`, `ReportViewModel` + `ReportDialog`
- [ ] C4 Asset slots + placeholders (Git LFS pattern added first) + README slot table
- [ ] C5 Amend 0004's component list and design-system § Components (done in the plan text with this plan's approval);
  accessibility; Czech translation; `.ts` fresh; screenshots (light/dark, EN/CS) of the dialog, check list and
  report dialog

## Tests and verification

- **GTest (`libs/diag`):** CheckRunner order, `requires` → Skipped, per-check timeout → `diag.check_timeout`,
  stop token; each environment check with the fake J-Link (missing, too old, load failure, probes, outdated
  firmware) and the fake nRF Util (missing, too old, device command missing); USB access with a temp sysfs tree
  (no SEGGER device, accessible, not accessible); process matching table (every pattern, case, exclusions, 4096
  bound, timeout); redaction (home, user, host, substrings that must not change, Windows case); report zip
  (entries, sizes ≤ bounds, last-2-MiB cut at a line start, `.part` cleanup on failure).
- **GTest (`libs/device`):** Diagnose on the fake library for every failure point; probe-only session never connects
  the target; Diagnose on a connected session keeps RTT running (same target) or switches and restores (other
  target); `internal.busy` while another operation runs; cancel → `cancelled`.
- **CLI end-to-end:** `doctor` / `chester app check` / `diag report` with fakes: human output, `--json` envelopes,
  exit codes 0 / 4 / 5 / 6 / 130.
- **GUI GTest:** WizardViewModel with the test wizard (navigation, busy cancel, one wizard at a time, launcher
  `canOpen`), ReportViewModel (collect → save → copy).
- **Qt Quick Tests:** WizardDialog with each step kind in light and dark; 960 × 600 in Czech (geometry
  assertions, compact step list); keyboard (Enter, Esc, Tab order, focus on heading); ErrorPanel shows the wizard
  button only when registered; every illustration slot resolves to a file.
- **`qmllint`** clean; no logic in QML.
- **Sanitizers:** ASan+UBSan and TSan run `libs/diag`, Diagnose and the GUI ViewModel tests.
- **HIL (CHESTER + J-Link):** `hio chester app check` and `lte check` with: all good; power off; flat cable on
  the wrong connector (`target.wrong_family`); firmware without RTT; J-Link Commander running (`hio doctor`
  warning).
- **Manual on Linux, Windows, macOS (by the user, before Done):** `hio doctor` on a machine without J-Link
  Software; process scan finds J-Link Commander; Linux without the udev rule; report saved, opened and inspected
  for redaction.

## Risks and open points

- **Diagnose vs. a hung vendor call:** a blocking J-Link call can exceed the 15 s budget; the operation then
  reports `diag.check_timeout` for the step after the call returns (vendor calls cannot be interrupted).
- **Process names drift** between tool versions; the table is data and easy to extend.
- **Flatpak (M7):** `/proc` and `/sys` access in the sandbox is limited; the checks then report Skipped. To be
  verified in M7 (open question 7).
- **Report contents** may still contain personal data inside log messages (e.g. file names chosen by the user).
  The dialog shows the summary only; the logs are not previewed. Accepted with ADR-0075.
