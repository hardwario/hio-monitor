# 0008 — M4 first-run setup wizard

- **Status:** Draft
- **Milestone:** M4 (plan 2 of 4, ADR-0065)
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0006, ADR-0016, ADR-0025, ADR-0027, ADR-0029, ADR-0042, ADR-0066, ADR-0067, ADR-0068,
  ADR-0071, ADR-0072, ADR-0073
- **Depends on:** [0007](0007-m4-wizard-framework-diagnostics.md) (wizard contract, `libs/diag`),
  [0006](0006-m3-flash-settings.md) (Settings page sections for J-Link / nRF Util paths)
- **Can run in parallel with:** [0009](0009-m4-connect-and-troubleshooting.md), [0010](0010-m4-flash-wizard.md)
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

## Context

The app needs SEGGER J-Link Software ≥ 8.00 (ADR-0029), loaded from the user's installation (ADR-0006), and
optionally nRF Util for LTE modem firmware (ADR-0016, ADR-0042). On Linux the J-Link needs a udev rule. The old app
bundled J-Link 7.94 and a Windows USB driver; v2 does not, so a new user without J-Link Software currently sees only
`dependency.jlink_not_found`. The vision's success criterion — "a first-time user can go from download to a
working RTT console using only the wizards" — starts here.

## Goals / Non-goals

**Goals**
- The `first-run-setup` wizard: welcome → J-Link Software → USB access (Linux) → nRF Util (optional) → test
  with a probe → done.
- The start-up trigger per ADR-0071 (`setup/completedVersion`, `setup/showAtStart`).
- Handles every error with wizard FirstRunSetup: `dependency.jlink_not_found`, `dependency.jlink_load_failed`,
  `dependency.jlink_too_old`, `dependency.nrfutil_not_found`, `dependency.nrfutil_device_missing`,
  `dependency.nrfutil_too_old`, `usb.access_denied` (0007).
- EN + CS texts, accessibility.

**Non-goals**
- Installing anything: the app never downloads or runs installers and never runs commands as root (ADR-0006,
  ADR-0072). It links to the vendor download pages and shows commands.
- macOS Bluetooth permission (M5, with the BLE pairing wizard).
- Windows USB driver installation (J-Link Software installs it).

## User experience

Steps (ADR-0067 kinds in brackets). The step list shows: Welcome · J-Link · USB access (Linux only) · nRF Util ·
Test · Done.

1. **Welcome** [Info, slot `setup-welcome`]
   - Heading "Welcome to HARDWARIO Monitor". Body: "This assistant checks that your computer is ready to work with
     CHESTER over a J-Link probe. It takes about two minutes."
   - Buttons: [Next] (default) · [Skip setup] (ghost, left) — sets `setup/completedVersion` and closes.
   - When opened because J-Link is missing at a later start: the heading is "J-Link Software is needed" and the
     checkbox "Don't show at start" is shown (ADR-0071).
   - When opened from an error, this step is skipped and the wizard starts at the step that handles the error.
2. **J-Link Software** [Checks: `jlink-software`]
   - Ok: "J-Link Software 8.10 found in /opt/SEGGER/JLink." [Next].
   - Not found: body "HARDWARIO Monitor uses the J-Link Software from SEGGER. Install version 8.00 or newer, then
     press Check again." Links: [Download J-Link Software ↗] (`https://www.segger.com/downloads/jlink/`),
     platform hint ("Choose the 64-bit DEB or RPM installer" on Linux, "the macOS universal installer",
     "the Windows 64-bit installer"). Secondary: [It is installed elsewhere…] → file dialog for the library
     (`libjlinkarm.so*` / `libjlinkarm.dylib` / `JLink_x64.dll`), saved to `jlink/libraryPath` (validated like the
     Settings page) — then the check runs again.
   - Too old: "J-Link Software 7.94 is too old. Install version 8.00 or newer." + download link.
   - Load failed: "The J-Link library was found but could not be loaded." + the detail (searched path) + "Reinstall
     J-Link Software." If a custom path is set: [Use automatic detection] (clears `jlink/libraryPath`).
   - Default button: [Check again] while not Ok; [Next] when Ok. [Next] is disabled while not Ok, with the text
     "You can continue without J-Link, but you cannot connect to a device." and a ghost [Continue anyway].
3. **USB access** [Checks + Commands: `usb-access`] — **Linux only**, skipped elsewhere.
   - Ok / Skipped (no J-Link connected): Ok shows "Your user can access the J-Link." Skipped shows "Connect the
     J-Link to check USB access." with [Check again] and [Next].
   - Failed (`usb.access_denied`): body "Linux needs a rule that allows your user to use SEGGER USB devices. Run
     these commands in a terminal, then unplug and reconnect the J-Link." The two commands of ADR-0072 in a
     `CommandBlock` with [Copy]. [Check again] (default) · [Next].
4. **nRF Util (optional)** [Checks: `nrfutil`]
   - Heading "nRF Util (optional)". Body: "Needed only to update the LTE modem firmware. Everything else works
     without it."
   - Ok: "nRF Util 7.13.0 with device command 2.7.4."
   - Not found: [How to install nRF Util ↗] (`https://www.nordicsemi.com/Products/Development-tools/nRF-Util`),
     the commands `nrfutil install device` in a `CommandBlock`, [It is installed elsewhere…] (saves
     `nrfutil/path`), [Check again] · [Skip] (default when not found).
   - Too old / device command missing: the commands `nrfutil self-upgrade` and `nrfutil upgrade device` (or
     `nrfutil install device`), and the minimum versions from ADR-0042.
5. **Test with a probe** [Embedded `probe-picker` + Checks: `probes`, `probe-firmware`, `probe-holders`]
   - Body: "Connect a J-Link to your computer. You do not need a device for this test." Slot `jlink-usb` (0007).
   - The M3 probe picker list (read-only here, no Connect button), refreshing every 2 s.
   - Checks below it: probes found; firmware current (Warning: "The J-Link firmware is outdated. Update it with
     `hio jlink update-firmware` or J-Link Commander." — no button, open question 19); other programs (Warning
     with the names found, ADR-0073).
   - [Next] is enabled always (the user may not have a probe at hand); the text says "No J-Link yet? You can test
     later from Help → Setup wizard."
6. **Done** [Done]
   - Summary list: J-Link Software ✓ 8.10 · USB access ✓ · nRF Util – skipped · J-Link 683012345 ✓.
   - Hand-off buttons: [Connect a device] (opens `connect-device` on the current tab, 0009, if registered) ·
     [Close] (default).
   - Sets `setup/completedVersion` = app version.

### Start-up behaviour (ADR-0071)

- After the main window is shown, `DiagWorker` runs the `jlink-software` check (≤ 5 s). Then:
  - `setup/completedVersion` absent → open the wizard at Welcome (first-start texts);
  - else if the check failed and `setup/showAtStart` → open at Welcome with the "J-Link Software is needed" texts
    and the checkbox;
  - else nothing.
- While the check runs nothing is blocked. The first Choose-device tab still opens (0004); its J-Link error panel
  now offers "Help me fix this" → this wizard.
- Restored tabs never connect at start (ADR-0061), so the wizard never competes with a connecting tab.

### Entry points

- Start-up (above); Help → "Setup wizard…"; "Help me fix this" on any FirstRunSetup error → starts at the step for
  that code (`dependency.jlink_*` → 2, `usb.access_denied` → 3, `dependency.nrfutil_*` → 4) with the Welcome step
  hidden; Done then offers [Close] only.
- Settings page (0006): the J-Link and nRF Util sections get a link "Run the setup wizard" (0006 amendment, text
  only).

### Texts

English source texts as above. Czech is translated in Qt Linguist as part of done (ADR-0027).

## CLI

None new. The CLI equivalent is `hio doctor` (0007); its human output ends with the same install hints as the
wizard for each failed check.

## Design

### Modules

```
app/gui/src/wizard/setup_wizard.*        SetupWizard : Wizard ("first-run-setup", global)
app/gui/src/startup_check.*              runs the ADR-0071 decision after the window is shown
app/gui/assets/wizard/setup-welcome.svg  (placeholder, LFS)
app/gui/tests/ wizard/setup_wizard_test.cpp  tst_setup_wizard.qml
```

### `SetupWizard`

- Steps as above; `start(context)` maps the error code to the start step and hides Welcome when started from an
  error.
- Each Checks step submits the matching `diag` checks to `DiagWorker` (0007) and fills `CheckListModel` as results
  arrive. "Check again" resubmits; a newer request replaces a pending one.
- "It is installed elsewhere…" → `QFileDialog` from QML (FileDialog), path validated in C++ by running the
  `jlink-software` check with `LocatorOptions::overridePath`; only an Ok result is written to `jlink/libraryPath`
  through `SettingsStore` (0004). Same for nRF Util (`nrfutil/path`). A failed validation shows the error in the
  check row and does not save.
- The platform (Linux / macOS / Windows) decides the hint texts and whether step 3 exists (`QSysInfo` at
  construction; injectable for tests).
- On Done or Skip: `setup/completedVersion` = `QCoreApplication::applicationVersion()`. "Don't show at start" →
  `setup/showAtStart = false`.
- The cached dependency status of 0006 (`dependency_status.*`) is invalidated when a path is saved, so the Settings
  page and Flash prerequisites update.

### Error cases

| Situation | Code | Shown as | Wizard |
|---|---|---|---|
| J-Link missing / too old / not loadable | `dependency.jlink_*` (0002) | check row + step body | (this) |
| Custom path chosen is not a J-Link library | `dependency.jlink_load_failed` | check row; not saved | — |
| USB access missing | `usb.access_denied` (0007) | check row + commands | (this) |
| nRF Util missing / old / no device command | `dependency.nrfutil_*` (0003) | check row (Warning) + commands | (this) |
| Settings read-only | `settings.newer_schema` (M0) | banner in the dialog; paths cannot be saved; Done does not set the key | — |

### Settings keys added (ADR-0025)

| Key | Type | Default |
|---|---|---|
| `setup/completedVersion` | string (app version) | — (absent = first start) |
| `setup/showAtStart` | bool | `true` |

The `schemaVersion` bump and its no-op migration are documented (0004 rule).

### Illustration slots (ADR-0076)

| Slot | Shows | Caption source text |
|---|---|---|
| `setup-welcome` | CHESTER, a J-Link and a laptop | — (decorative, `Accessible.ignored`) |

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| When the wizard opens | first start + J-Link missing / first start / never | first start + J-Link missing, skippable | ADR-0071 |
| Linux udev | detect + commands / pkexec / docs | detect + commands | ADR-0072 |
| macOS Bluetooth permission | setup wizard / M5 | M5 | — (0003-ux updated) |
| J-Link firmware update button | M4 / later | later (open question 19) | — |

## Implementation steps

- [ ] S0 Prerequisite check: 0007 phases A and C done; 0006 done (otherwise stop and report)
- [ ] S1 `SetupWizard` steps, start-step mapping, platform variants + GTest
- [ ] S2 Path selection and validation for J-Link / nRF Util; dependency-status invalidation
- [ ] S3 Start-up check and trigger (ADR-0071) + settings keys + GTest (all trigger combinations)
- [ ] S4 Register the wizard; Settings page links (0006 amendment); placeholder slot
- [ ] S5 Accessibility, Czech translation, `.ts` fresh; screenshots of every step (light/dark, EN/CS, 960 × 600)

## Tests and verification

- **GTest:** step sequences for each platform; start step per error code; skip/finish write the keys; "don't show"
  writes `setup/showAtStart`; read-only settings; path validation (good path saved, bad path not saved); trigger
  decision table (first start, completed + J-Link ok, completed + J-Link missing + show, … + don't show).
- **Qt Quick Tests:** each step in light/dark; 960 × 600 in Czech; CommandBlock copy; keyboard-only walkthrough.
- **Sanitizers:** TSan with the start-up check racing window creation and "Check again" spam.
- **Manual (by the user, before Done):**
  - Linux VM without J-Link Software → install from the link → Check again → USB access step → install udev rule →
    probe test;
  - Windows and macOS fresh user accounts: first start, skip, restart (no wizard), uninstall J-Link → wizard again;
  - custom library path in a non-standard folder.

## Risks and open points

- **Vendor download pages change**: URLs are compiled in; a broken link is a documentation bug fixed in a patch
  release. They are listed in one table in `setup_wizard.cpp`.
- **J-Link installers on Linux** do not always install the udev rule (DEB does, tarball does not) — the USB step
  covers both.
- **J-Link firmware update** is only hinted (open question 19).
