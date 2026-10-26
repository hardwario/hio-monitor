# 0012 — M5 BLE GUI: Bluetooth in the device tab, BlePairing wizard, setup Bluetooth step

- **Status:** Draft
- **Milestone:** M5 (plan 2 of 2, ADR-0078)
- **Author:** Matej Strnad (with Claude), 2026-10-10
- **Related ADRs:** ADR-0043, ADR-0050, ADR-0052, ADR-0058, ADR-0059, ADR-0061, ADR-0063, ADR-0066, ADR-0067,
  ADR-0071, ADR-0076, ADR-0077, ADR-0079–ADR-0084, ADR-0086, ADR-0088, ADR-0089
- **Depends on:** [0011](0011-m5-ble-backend-cli.md) (BLE backend, `ConsoleSession`), [0004](0004-m3-gui-shell.md)
  (shell, picker, device bar, tab restore), [0005](0005-m3-console.md) (console page),
  [0007](0007-m4-wizard-framework-diagnostics.md) (wizard contract), [0008](0008-m4-first-run-setup.md) (setup
  wizard), [0009](0009-m4-connect-and-troubleshooting.md) (topic table)
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

## Context

After 0011 the backend and the CLI speak BLE, but the GUI only knows J-Link tabs. The old app had a separate
Bluetooth page (welcome text, SCAN and CONNECT buttons in the right tool panel, a device list with a name filter,
RSSI colour and signal icon, a shell without a log pane, a pairing warning "If the pairing window does not appear
when you connect, try pairing the CHESTER device using the bluetooth system tool", and toasts such as "Bluetooth is
turned off"). Its pain points: the selection jumped to the top whenever a device appeared, scanning needed a click
every 5 s, failures were all reported as "probably unpaired", and there was no help beyond the warning text.

Decisions: Bluetooth is a connection type of a device tab with a continuously scanning list (ADR-0081); pairing help
is a wizard with app-initiated pairing where supported (ADR-0084); the macOS permission and adapter state get a
setup-wizard step (ADR-0088).

## Goals / Non-goals

**Goals**
- Choose-device page: **J-Link | Bluetooth** switch; BLE device list with continuous scan (ADR-0081); Connect.
- BLE tabs: title, device bar (BLE fields), Console page bound through `ConsoleSession` (ADR-0079), log pane hidden
  while empty, Reset hidden, Flash disabled with a reason, auto-reconnect option, tab restore (ADR-0083).
- `ble-pairing` wizard (`WizardId::BlePairing`) for every error naming it, and Help → "Pair a Bluetooth device…".
- Troubleshooting coverage: every `ble.*` code with a wizard is handled by the BlePairing wizard (topic table
  coverage test extended).
- Setup wizard: optional **Bluetooth** step (ADR-0088), entry for `ble.permission_denied`.
- Illustration slots for BLE (ADR-0076), mockups, EN + CS texts, accessibility.

**Non-goals**
- Backend changes (0011). If the GUI needs one, stop and amend 0011.
- Firmware update over BLE, BLE settings of the device (passkey change) — the passkey is changed with
  `hio chester app pib write` (later, open question 11).
- MCP (M6).

## User experience

### Choose device (amends ADR-0050 / 0004)

```
┌ Choose device ─────────────────────────────────────────────────────────┐
│  Connect via  [ J-Link | Bluetooth ]          Device [CHESTER ▾]       │
│                                                                        │
│  🔍 Filter by name…                          ☐ Show all devices        │
│  ▂▄▆█  CHESTER-ABC123   F1:22:33:44:55:66   −58 dBm                    │
│  ▂▄▆_  CHESTER-XYZ789   C0:11:22:33:44:55   −81 dBm  In use (tab 2)    │
│  ▂___  CHESTER-OLD001   …                   not seen recently (dimmed) │
│  Scanning…  (spinner)                                       [Connect]  │
└────────────────────────────────────────────────────────────────────────┘
```

- The switch remembers the last choice (`ui/lastTransport`, default `jlink`). The J-Link view is unchanged.
- **Bluetooth view:**
  - Scans while visible (ADR-0081): restart every 10 s; rows sorted by RSSI but **the selected row keeps its
    selection and keyboard focus** when the order changes (legacy bug); new rows are announced to screen readers
    at most once per 5 s ("3 devices found").
  - Row: signal icon (4 levels: ≥ −55, ≥ −67, ≥ −80, below; RSSI 0 = unknown → no number), name, id (hidden on macOS,
    where the id is a UUID — shown in the tooltip instead), RSSI, badges "In use (tab N)" (selecting switches to the
    tab) or "In use by another program (hio, PID 1234)" (row disabled), "not seen recently" (dimmed after 15 s,
    removed after 60 s).
  - Filter field (substring, case-insensitive), "Show all devices" (rows without a profile show "Unknown device"
    and cannot be connected unless a profile is picked in Device).
  - Exactly one connectable match → preselected. Enter / double-click = Connect.
  - **Empty state** after the first scan: "No CHESTER found nearby — Make sure the device is powered and its
    Bluetooth is on." [Pair a device…] (BlePairing wizard) · link "Show all devices".
  - **Adapter / permission problems** replace the list with an ErrorPanel (`ble.adapter_not_found`,
    `ble.adapter_off`, `ble.stack_unavailable`, `ble.permission_denied`) with "Help me fix this"; the panel
    re-checks every 2 s and disappears when the adapter becomes ready.

### BLE tab

- **Tab title** `CHESTER · CHESTER-ABC123` with a Bluetooth icon instead of the probe icon; state dot as 0004.
- **Device bar:** `[ᛒ CHESTER-ABC123 · −58 dBm] State text … [Connect | Disconnect | Cancel] [⋯]`
  - States as 0004, with BLE texts: *Connecting…* → *Waiting for pairing — enter the passkey in the system dialog
    (default 123456)* (after `pairingPrompt`) → *Connected · MTU 247*; *Reconnecting… (device not in range)*.
  - ⋯ popover for BLE: Auto-reconnect, "Save console to log file". J-Link fields are not shown. Per-device memory
    `ble/<id>/autoReconnect` (LRU 32, as ADR-0052).
  - No target picker (BLE console target is the profile's `consoleTargetId`).
- **Rail:** Console; Flash disabled with the tooltip "Flashing needs a J-Link. Open a J-Link tab to flash this
  device." 
- **Console page:** unchanged UI, bound to the tab's `ConsoleSession`. The log pane is hidden while the Log channel
  is empty (ADR-0082); the shell pane takes the full width; the "Reset ▾" action is hidden (capability `Reset`
  missing); markers "— connected (Bluetooth) —", "— disconnected: out of range —".
- **Errors:** ErrorPanel per 0004; primary action for `ble.*` Target codes = Reconnect; `ble.not_paired` and
  `ble.pairing_failed` primary action = [Pair…] (opens the wizard at its Pair step).
- **Restore:** BLE tabs are restored Disconnected (ADR-0083). Missing device → `ble.device_not_found` with
  [Choose another device].

### BlePairing wizard (`ble-pairing`, per tab; ADR-0084)

Steps (ADR-0067 kinds). Step list: Bluetooth · Device · Pair · Test · Done.

1. **Bluetooth** [Checks: `bluetooth`] — adapter present, on, permission (macOS). Not ok → the error's suggestion;
   on macOS with permission not yet decided: [Allow Bluetooth access] (as the setup step, ADR-0088); denied →
   [Open Privacy settings] (`x-apple.systempreferences:com.apple.preference.security?Privacy_Bluetooth`). Skipped
   automatically when Ok and the wizard was opened from a device error.
2. **Device** [Info, slot `ble-enable`, + Embedded `ble-picker`] — "Make sure the device is powered and its Bluetooth
   is on." (profile `pairingGuide`); the BLE list of the picker (read-only selection, no Connect). Preselects the
   tab's device.
3. **Pair** [Info + Commands, slot `ble-pair-os`] — per OS:
   - **Linux:** "Click Pair. Your desktop shows a dialog asking for the passkey." [Pair] (default; calls
     `BleSession::pair()` / a short-lived session). Headless hint with commands in a `CommandBlock`:
     `bluetoothctl`, `pair <id>`, `trust <id>`.
   - **Windows:** [Pair] as Linux; secondary [Open Bluetooth settings] (`ms-settings:bluetooth`) with "Add device →
     Bluetooth → choose the device".
   - **macOS:** "macOS asks for the passkey when HARDWARIO Monitor connects. Click Connect and enter the passkey."
     [Connect] (default).
   - Passkey hint box: the profile's `passkeyHintSourceText` ("The passkey is 123456 unless it was changed when
     the device was configured."). When another tab has a connected J-Link session to a CHESTER:
     [Read passkey from that device] → PIB read (M2 `readMemory` + PIB parser) → shows the passkey in the box,
     never stored, never logged.
   - "Already paired but it does not work?" → Manual: "Remove CHESTER from the system Bluetooth settings, and on
     the device clear its bonds (see the CHESTER documentation), then pair again." (`ble.pairing_failed` starts
     here.)
4. **Test** [Checks] — connects (or reconnects) the tab's BLE session and shows rows "Connected", "Console
   service found", "Paired (encrypted)", "Shell answers" (sends an empty line, waits ≤ 3 s for the prompt).
   [Check again].
5. **Done** — hand-off [Open console] (default) · [Close].

Start step by error: `ble.adapter_*`, `ble.stack_unavailable` → 1; `ble.device_not_found`, `ble.connect_failed`,
`ble.disconnected` → 2; `ble.not_paired`, `ble.pairing_failed` → 3; `ble.service_not_found`, `ble.write_failed`,
`ble.write_timeout` → 4 (with a Manual note: "The firmware may not have the Bluetooth console. Flash firmware with
it enabled." [Flash firmware…] when 0010 is registered). Help menu: "Pair a Bluetooth device…" opens it at 1 on
the current tab (or a new Choose-device tab if the current tab is J-Link connected).

### Setup wizard Bluetooth step (amends 0008, ADR-0088)

After nRF Util: **Bluetooth (optional)** [Checks: `bluetooth`]. Body: "Needed only to use the console over
Bluetooth." Ok / Warning texts from the check; macOS `notDetermined` → [Allow Bluetooth access] (1 s scan to trigger
the prompt, then re-check); denied → [Open Privacy settings]. [Skip] is the default when not Ok. Done's summary
gains "Bluetooth ✓ / – skipped". `ble.permission_denied` ("Help me fix this") starts the setup wizard at this
step.

### Shortcuts

None new. Ctrl+T opens a Choose-device tab in the last transport.

### Texts

English source texts as above. Czech is translated in Qt Linguist as part of done (ADR-0027).

## CLI

None new (0011 has `hio ble …`).

## Design

### Modules

```
app/gui/src/
  viewmodels/ble_picker_view_model.*     BLE list model over BleScanner (roles below)
  viewmodels/choose_device_view_model.*  transport switch; owns ProbePickerViewModel + BlePickerViewModel
  viewmodels/device_tab_view_model.*     + transport, consoleSession(), openBle()
  viewmodels/connection_view_model.*     + BLE fields
  viewmodels/console_view_model.*        binds ConsoleSession (amends 0005)
  wizard/ble_pairing_wizard.*            BlePairingWizard : Wizard ("ble-pairing", per tab)
  wizard/setup_wizard.*                  + Bluetooth step (amends 0008)
  workers/                               BleHost owned by AppViewModel (0011 type; no new worker)
qml/pages/ChooseDevicePage.qml (+ TransportSwitch), qml/shell/BleDeviceList.qml, DeviceBar.qml (BLE variant)
assets/wizard/ ble-enable.svg ble-pair-os.svg (placeholders, LFS)  assets/icons/bluetooth*.svg, signal_cellular_*.svg
tests/ viewmodels/ble_picker_test.cpp, wizard/ble_pairing_wizard_test.cpp, tst_ble_picker.qml, tst_ble_tab.qml
```

### View models (sketch, `hio::gui`)

```cpp
class BlePickerViewModel : public QAbstractListModel {     // parent = ChooseDeviceViewModel
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, RssiRole, SignalLevelRole /*0–4*/, ProfileRole /*id or ""*/,
                 InUseTabRole /*-1*/, InUseOtherRole /*"hio, PID 1234" or ""*/, StaleRole, ConnectableRole };
    Q_PROPERTY(QString filterText ...) Q_PROPERTY(bool showAll ...) Q_PROPERTY(QString selectedId ...)
    Q_PROPERTY(bool scanning ...) Q_PROPERTY(hio::gui::ErrorInfo adapterError ...) Q_PROPERTY(bool hidesIds ...)
    Q_INVOKABLE void connectSelected();
    void setVisible(bool);                                  // starts/stops the BleScanner (visibility count)
};

class ChooseDeviceViewModel : public QObject {
    Q_PROPERTY(QString transport READ transport WRITE setTransport NOTIFY transportChanged)   // "jlink" | "ble"
    Q_PROPERTY(ProbePickerViewModel* probes ...) Q_PROPERTY(BlePickerViewModel* ble ...)
};

// DeviceTabViewModel (0004) amendment
Q_PROPERTY(QString transport READ transport NOTIFY sessionChanged)              // "jlink" | "ble" | ""
[[nodiscard]] hio::device::ConsoleSession* consoleSession() const;              // J-Link or BLE, nullptr if none
// session() stays: the J-Link DeviceSession*, nullptr for BLE tabs (Flash page and Diagnose use it)
[[nodiscard]] Status openBle(const hio::ble::BleDeviceId&, const std::string& name);  // BleSession::open with the tab's log
[[nodiscard]] hio::device::BleSession* bleSession() const;                      // wizard Pair / Test
```

- `ConsoleViewModel` (0005) connects to `tab.consoleSession()` on `sessionChanged`; `reset()` is not exposed when
  `capabilities()` lacks `Reset`; `inputDisabledReason` texts gain "Waiting for pairing…".
- Flash page (0006) `enabled = tab.transport != "ble"` with the reason text above.
- `ConnectionViewModel`: `transport`, `deviceName`, `rssi`, `mtu`, `pairingPending`; J-Link fields hidden for BLE.
- Tab restore (0004, ADR-0083): `ui/tabs` entries `{transport, profile, …}`; reading an entry without `transport`
  = `jlink`; schema bump with no-op migration.

### Wizard registration and coverage

- `WizardRegistry::add({WizardId::BlePairing, perTab = true, …})`. The `bluetooth` check and the Test step use
  `DiagWorker` (environment check) and the tab's `BleSession` (test).
- 0009's coverage test is extended: every error code whose wizard is BlePairing maps to a BlePairing start step
  (table above); `ble.permission_denied` maps to the setup wizard's Bluetooth step.
- New Embedded step name `ble-picker` (0007 contract: one more allowed value of `WizardStep::embedded`).

### Data flow and threading

- GUI thread: all view models, `BleScanner` and `BleSession` façades. The `BleHost` (0011) is owned by
  `AppViewModel`, created lazily on the first Bluetooth use (so users who never use BLE never start the thread or
  trigger the macOS prompt), stopped on quit after all tabs closed their sessions.
- One `BleScanner` for the app; pickers and the wizard register visibility; scanning stops when no picker is
  visible and no BLE session is reconnecting.

### Error cases (GUI-originated)

| Situation | Code | Shown as | Wizard |
|---|---|---|---|
| Device chosen without a profile ("Show all") | `usage.invalid_argument` | inline: "Choose a device type first." | — |
| Restored BLE tab, device gone | `ble.device_not_found` (0011) | ErrorPanel + [Choose another device] | BlePairing |
| PIB read for the passkey failed | the M2 error | inline in the passkey box | — |

### Settings keys added (ADR-0025)

| Key | Type | Default |
|---|---|---|
| `ui/lastTransport` | string `jlink`\|`ble` | `jlink` |
| `ui/tabs` | (0004) entries gain `transport`, BLE entries `{transport, profile, id, name}` | — |
| `ble/autoReconnect` | bool | `false` |
| `ble/<id>/autoReconnect` | per device, LRU 32 | — |

### Illustration slots (ADR-0076)

| Slot | Shows | Caption source text |
|---|---|---|
| `ble-enable` | CHESTER powered, Bluetooth enabled, laptop nearby | from the profile `pairingGuide` |
| `ble-pair-os` | a system passkey dialog next to CHESTER | from the profile `pairingGuide` |

### Limits

Scan restart 10 s, stale 15 s, removal 60 s, list ≤ 256 (backend), screen-reader announcements ≤ 1 per 5 s,
adapter re-check 2 s, Test step prompt wait 3 s, remembered BLE devices 32.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Where BLE lives in the GUI | connection type in tab / BLE rail page / global page | connection type in the device tab | ADR-0081 |
| Scan in the GUI | continuous while visible / manual button | continuous while visible | ADR-0081 |
| Pairing help | OS + wizard + app-initiated pairing | as decided | ADR-0084 |
| Passkey hint | default 123456 / label / PIB only | default 123456, optional PIB read | ADR-0084 |
| Bluetooth step in setup | setup step / pairing wizard only | setup step (optional) + doctor | ADR-0088 |
| Tab restore for BLE | restore / no | restore Disconnected | ADR-0083 |
| Minor defaults: signal thresholds (−55 / −67 / −80 dBm, from the old app), hide ids on macOS, `ui/lastTransport`, lazy `BleHost`, Flash disabled text, wizard steps and start mapping above | accept / change | _pending_ | — |

## Implementation steps

- [ ] G0 Prerequisite check: 0011 phases A–C done; 0004, 0005, 0007 (phase C), 0008, 0009 done (otherwise stop and
  report)
- [ ] G1 `ChooseDeviceViewModel`, `BlePickerViewModel` (+ selection stability, stale rows, announcements) + GTest
- [ ] G2 `DeviceTabViewModel` transport + `consoleSession()` + `openBle`; `ConsoleViewModel` on `ConsoleSession`;
  Flash page disabled reason; tab restore schema + migration + GTest
- [ ] G3 QML: transport switch, `BleDeviceList`, BLE device bar, popover; icons (ADR-0060 subset additions)
- [ ] G4 `BlePairingWizard` (steps, OS variants, start mapping, PIB passkey read) + coverage test + GTest
- [ ] G5 Setup wizard Bluetooth step (0008 amendment) + GTest
- [ ] G6 Illustration placeholders (LFS), mockups (`gui/mockups/index.html`: BLE picker, BLE tab, pairing wizard),
  design-system component entries
- [ ] G7 Accessibility, Czech translation, `.ts` fresh; screenshots (light/dark, EN/CS, 960 × 600)

## Tests and verification

- **GTest (fake BLE scenarios from 0011):** picker filtering, `showAll`, in-use badges (other tab, other process
  via a held `BleLock`), selection kept while RSSI reorders, stale/removal timing (fake clock), adapter error
  appears/disappears; BLE tab lifecycle (connect, pairing prompt state, reconnect, close), the tab's `ConsoleLog`
  survives a BLE reconnect; restore with and without `transport`; BlePairing start step for every BLE code; Pair on
  a "macOS" scenario shows the macOS step; setup step on all adapter states.
- **Qt Quick Tests:** picker and BLE tab in light/dark; 960 × 600 in Czech (geometry); keyboard-only connect;
  Flash rail item disabled with tooltip; log pane hidden while empty; wizard steps.
- **`qmllint`** clean; no logic in QML.
- **Sanitizers:** ASan+UBSan and TSan run the view-model tests with the fake BLE backend.
- **HIL (CHESTER with Bluetooth shell):** pick from the list and connect; pairing from a fresh bond through the
  wizard on each OS; one J-Link tab and one BLE tab to the same CHESTER; out of range → reconnect.
- **Manual (by the user, before Done):** Linux, Windows 10/11, macOS 13.3+: first-run Bluetooth step (macOS prompt),
  denied permission → wizard, adapter off, screen reader announcement of new devices.

## Risks and open points

- **Windows pairing UI** may not show a passkey dialog for app-initiated pairing (0011 risk); the wizard then
  falls back to "Open Bluetooth settings".
- **Many advertising devices** (offices, fairs) make the "Show all" list long; bounded to 256 and filterable.
- **Illustrations** are placeholders until the HARDWARIO content task (open question 22) also covers BLE.
- **Passkey read from PIB** requires a J-Link tab; most BLE users will not have one, so the default hint matters.
