# 0009 — M4 connect-device and troubleshooting wizards

- **Status:** Draft
- **Milestone:** M4 (plan 3 of 4, ADR-0065)
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0032, ADR-0050, ADR-0053, ADR-0057, ADR-0058, ADR-0066, ADR-0067, ADR-0069, ADR-0072,
  ADR-0073, ADR-0076, ADR-0077
- **Depends on:** [0007](0007-m4-wizard-framework-diagnostics.md) (wizard contract, `Diagnose`, `libs/diag`),
  [0004](0004-m3-gui-shell.md) (probe picker, device tabs)
- **Can run in parallel with:** [0008](0008-m4-first-run-setup.md), [0010](0010-m4-flash-wizard.md)
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

> **Pending amendment from M5 ([0012](0012-m5-ble-gui-pairing.md), ADR-0084; applied when 0012 is approved):** the
> coverage test also accepts error codes whose wizard is BlePairing when the BlePairing wizard maps them to a start
> step; the Connect wizard stays J-Link only.

> **Pending amendment from M8 ([0017](0017-m8-parity.md), ADR-0132):** the troubleshooting topic `probe-firmware` gets an
> [Update J-Link firmware] step; the `hio jlink update-firmware` CommandBlock stays as the alternative.

## Context

Most support cases are physical: the J-Link is not plugged in, the flat cable is on the other MCU's connector,
CHESTER has no power, another program holds the probe. The old app showed only "Attach Failed."; py-hardwario says
"No J-Link found (check USB cable)" or "Detected low voltage". M1–M2 give precise error codes, and 0007 adds
step-by-step `Diagnose`. This plan turns them into two wizards:
- **Connect device** (`connect-device`) — guides a user from nothing to a connected tab;
- **Troubleshooting** (`troubleshooting`) — one topic per error code, with automatic checks and manual steps.

## Goals / Non-goals

**Goals**
- Connect wizard: device → target → probe → cabling → power → live check → connected tab.
- Uses the connection guide from the device profile (ADR-0077; defined in 0007).
- Troubleshooting topics for **every** error code that names ConnectDevice or Troubleshooting (table below), plus
  a topic list when opened without an error.
- Replace the disabled "Help me fix this" in the M3 Choose-device empty state with an active "Guide me" button.
- EN + CS texts, accessibility.

**Non-goals**
- Real CHESTER cabling art and exact connector names (placeholders; open question 22, ADR-0077).
- J-Link firmware update button (open question 19) — the `probe.firmware_outdated` topic shows the CLI command.
- BLE (M5).

## User experience

### Connect wizard (`connect-device`, per tab)

Step list: Device · Probe · Cable · Power · Check · Done.

1. **Device** [Choice] — "Which device do you want to connect?" Cards: CHESTER (the only profile today; the step is
   skipped when there is exactly one profile) and the target: "Application MCU (app) — console and application
   firmware" / "LTE MCU (lte) — LTE firmware" (default: the tab's target, else `app`).
2. **Probe** [Embedded `probe-picker` + Checks `probes`, `probe-holders`, `usb-access`]
   - The M3 picker (in-use rows, 2 s refresh). Body: "Connect the J-Link to your computer with a USB cable." Slot
     `jlink-usb`.
   - No probe: the checks explain why (USB access on Linux, programs holding probes, "Is the J-Link's LED on?").
     [Check again].
   - Exactly one free probe → preselected; [Next] enabled when a free probe is selected.
3. **Cable** [Info, from `TargetProfile::connectionGuide`] — one step per guide entry (CHESTER: one step per
   target), e.g. "Connect the J-Link flat cable to the programming connector of the application MCU." Slot
   `chester-<target>-cable`. [Next].
4. **Power** [Info] — "Power CHESTER with batteries or the USB-C power input." (`DeviceProfile::powerHintSourceText`,
   final wording in open question 22). Slot `chester-power`. [Next].
5. **Check** [Checks: the six `Diagnose` steps, ADR-0069]
   - The wizard opens a probe-only session for the chosen probe and target (`ensureSession(target,
     attachRtt = false, connectTarget = false)`) and runs `diagnose(target)`. Rows tick as steps finish.
   - All Ok → [Connect] (default).
   - A step fails → the row expands with cause and "What to do", and the wizard offers [Show me how] which jumps back
     to the matching step (no power → Power, `target.connect_failed` / `target.wrong_family` → Cable with the
     text "The J-Link seems to be connected to the %1 MCU. Move the flat cable to the %2 connector." when the
     identified family belongs to the other target) and [Check again].
   - `target.protected` (Warning): "The MCU is read-back protected. Recover removes the protection but erases the
     MCU completely." with the hand-off [Open Flash page] (Recover is under Maintenance, ADR-0038). Connect is still
     possible (RTT usually fails on a protected MCU).
   - `target.rtt_not_found` / `rtt-buffers`: "The device has no console. It may need firmware with the CHESTER
     shell." [Flash firmware…] (0010) · [Connect anyway].
6. **Done** — [Connect] promotes the probe-only session to a normal connection (`startConnection`, below); the tab
   becomes Connecting → Connected as with `connectDevice()` (0004).
   When the tab is Connected: "Connected to CHESTER app · 683012345." with [Open console] (default) · [Close].
   - If the tab was already connected to the same probe and target when the wizard started, steps 1–4 are skipped
     and the wizard is a pure check.

### Troubleshooting wizard (`troubleshooting`, per tab)

- **With an error** (from "Help me fix this"): the topic for the error code. The first step shows the error
  (headline, cause) and "Let's find the cause.", then the topic's steps.
- **Without an error** (Help → Troubleshoot connection…): a Choice step "What is the problem?":
  "No J-Link found" · "Cannot connect to the device" · "The console stays empty" · "Flashing fails" ·
  "Another program uses the J-Link" → the matching topic.
- Every topic ends with **Still not working?** [Done]: "Create a diagnostic report and send it to HARDWARIO
  support." [Create diagnostic report…] (0007) · [Close].

Topics (each row: steps in order; **A** = automatic check, **M** = manual instruction with [Next]; `Check again`
re-runs the automatic checks of the step):

| Topic | Error codes | Steps |
|---|---|---|
| `no-probe` | `probe.not_found` | A `probes` · A `usb-access` (Linux) · A `probe-holders` · M "Unplug and reconnect the J-Link; try another USB port and cable; the J-Link LED should light up." · A `probes` again → Ok: "Found — connect now" [Connect] |
| `probe-busy` | `probe.open_failed`, `probe.in_use_by_other_process`, `probe.held_by_program` | A `usb-access` (Linux; Failed → commands of ADR-0072) · A `probe-holders` → list "Close these programs: J-Link Commander (PID 4242) …" · for `in_use_by_other_process`: M "Close the other HARDWARIO Monitor window or the `hio` command (PID %1)" · M "Unplug and reconnect the J-Link." · A Diagnose `probe` |
| `no-power` | `target.no_power` | M power hint + slot `chester-power` · A Diagnose `probe`, `power` (shows the measured voltage) · M "Check that the flat cable is fully inserted at both ends." (slot `chester-<target>-cable`) · A `power` again |
| `no-connect` | `target.connect_failed`, `target.wrong_family` | A Diagnose `probe`..`identify` · M cabling guide for the target (wrong family: "Move the flat cable to the %1 connector.") · M "Lower the J-Link speed" with [Use 1000 kHz] (sets the tab's speed, ADR-0052) · A Diagnose again |
| `no-console` | `target.rtt_not_found`, `target.rtt_buffer_not_found` | A Diagnose (all) · M "Reset the device" [Reset] (session `reset(target, Go)`) · A `rtt` again · M "The firmware may not have the CHESTER console (RTT shell). Flash firmware with console support." [Flash firmware…] (0010) |
| `console-stuck` | `target.rtt_write_timeout`, `target.rtt_read_failed` | A Diagnose `probe`, `power`, `connect` · M "The device may be busy or hung." [Reset] · A `rtt` · M "Check the cables; the console reconnects automatically when auto-reconnect is on." |
| `probe-firmware` | `probe.firmware_outdated` | A `probe-firmware` · M "Update the J-Link firmware: run `hio jlink update-firmware` or open J-Link Commander, which offers the update." (`CommandBlock`) · A `probe-firmware` again |
| `protected` | `target.protected` | M "The MCU is read-back protected. Recovering erases it completely, including the Product Information Block." · hand-off [Open Flash page] (Recover is under Maintenance with its confirmation, ADR-0038) |
| `program-failed` | `target.program_failed`, `target.erase_failed`, `firmware.verify_failed` | A Diagnose `probe`..`identify` · M "Lower the J-Link speed" [Use 1000 kHz] · M "Make sure the power is stable during flashing (batteries charged or USB-C power)." · hand-off [Try again on the Flash page] |
| `recover-failed` | `target.recover_failed` | M "Disconnect the power for 10 seconds, then connect it again." · A Diagnose `probe`, `power`, `connect` · hand-off [Open Flash page] |
| `modem-failed` | `firmware.modem_update_failed` | A `nrfutil` · M "Check cable and power; the modem update takes 1–3 minutes and must not be interrupted." · M "Use the modem firmware .zip for nRF91 from Nordic (mfw_nrf91…zip)." · hand-off [Open Flash page] |

The topic table is compiled in (`troubleshooting_topics.cpp`, ADR-0067); a unit test asserts that every error code
whose wizard is ConnectDevice or Troubleshooting has exactly one topic or is handled by the Connect wizard.

### ConnectDevice errors

`probe.not_found`, `target.no_power`, `target.connect_failed`, `target.wrong_family` name ConnectDevice. "Help me
fix this" opens the **Connect wizard** with the tab's probe and target preselected, starting at:
`probe.not_found` → Probe; `target.no_power` → Power; `target.connect_failed` / `target.wrong_family` → Cable. The
Check step then confirms the fix.

### Entry points

- "Help me fix this" on ErrorPanels (device bar errors, console page, flash page, probe picker).
- Choose-device page (0004): the empty state's disabled "Help me fix this" becomes **[Guide me]** → Connect wizard
  (0004 amendment); the page also gets a ghost link "Not sure how to connect? Guide me".
- Help → "Connect a device…" / "Troubleshoot connection…".
- Done step of the setup wizard (0008).

### Texts

English source texts as above. Czech is translated in Qt Linguist as part of done (ADR-0027).

## CLI

None new. The CLI equivalents are `hio chester app|lte check` and `hio doctor` (0007). Their human output for a
failed step ends with the topic's first manual instruction ("What to do: …").

## Design

### Modules

```
app/gui/src/wizard/
  connect_wizard.*                      ConnectWizard : Wizard ("connect-device", per tab)
  troubleshooting_wizard.*              TroubleshootingWizard : Wizard ("troubleshooting", per tab)
  troubleshooting_topics.*              compiled-in topic table (ADR-0067)
app/gui/tests/ wizard/connect_wizard_test.cpp troubleshooting_wizard_test.cpp tst_connect_wizard.qml
```

### Topic table (sketch)

```cpp
namespace hio::gui {
enum class TopicStepKind { AutoEnvironment, AutoDiagnose, Manual, Commands, HandOff };
struct TopicStep {
    TopicStepKind kind;
    std::span<const std::string_view> checkIds;   // AutoEnvironment: diag check ids; AutoDiagnose: step ids ("" = all)
    const char* headingSourceText;                // QT_TRANSLATE_NOOP("Troubleshooting", …)
    const char* bodySourceText;
    std::string_view illustrationSlot;            // or "guide:cable" / "guide:power" = from the device profile
    std::string_view action;                      // Manual: "reset" | "speed-1000" | ""; HandOff: "open-flash-page" | "flash-wizard" | "report"
};
struct Topic {
    std::string_view id;
    std::span<const hio::ErrorCode> codes;
    const char* titleSourceText;                  // shown in the topic list
    std::span<const TopicStep> steps;
};
[[nodiscard]] std::span<const Topic> troubleshootingTopics();
[[nodiscard]] const Topic* topicFor(hio::ErrorCode);
}
```

### Behaviour

- **Sessions:** both wizards use the tab's session when one exists (Diagnose on a connected session, ADR-0069);
  otherwise they open a probe-only session through the tab and close it at the end, unless [Connect] follows. Then
  the same session is promoted with `DeviceSession::startConnection(attachRtt = true)` (0007 amendment to 0002):
  the probe and its `ProbeLock` stay held, so no other process can take the probe in between.
- **Busy:** while Diagnose runs the tab is busy (`setBusy("connect-device", "Checking the connection…", …)`), so the
  Flash page cannot start an operation at the same time (`internal.busy` would be returned anyway).
- **Manual actions:** `reset` → `session->reset(target, Go)`; `speed-1000` → the tab's per-probe speed setting
  (ADR-0052) set to 1000 kHz with a toast "J-Link speed set to 1000 kHz for this probe."
- **Wrong connector hint:** `target.wrong_family` carries the found part; the wizard looks up which target of the
  profile owns that part (`TargetProfile::parts`) and names it.
- **Cancel:** cancels a running Diagnose (`cancel(OperationId)`), closes a probe-only session it opened, leaves a
  connected tab untouched.

### Error cases (GUI-originated)

| Situation | Code | Shown as | Wizard |
|---|---|---|---|
| Probe picked in the wizard is used by another tab | (not an error) | row switches to that tab, as in 0004 | — |
| Diagnose while the tab runs an operation | `internal.busy` (0003) | check row "Another operation is running — wait for it to finish." | — |
| Probe-only session cannot be opened | the open error (`probe.*`) | first check row Failed; topic `probe-busy` offered | — |

### Illustration slots (ADR-0076)

Uses `jlink-usb`, `chester-app-cable`, `chester-lte-cable`, `chester-power` (defined in 0007). No new slots.

### Bounds

Topics ≤ 32, steps per topic ≤ 8, Diagnose per "Check again" 1 at a time (repeated clicks are ignored while one
runs), probe-only sessions per tab 1.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Live checks | session operation / standalone / try to connect | `Diagnose` operation | ADR-0069 |
| Programs holding the probe | ask only / process scan | process scan | ADR-0073 |
| Cabling guidance location | device profile / GUI code | device profile | ADR-0077 |
| CHESTER connector names and wording | describe now / placeholder | placeholder, HARDWARIO content task | ADR-0077, open question 22 |
| Illustrations | slots + placeholders | slots + placeholders | ADR-0076 |

## Implementation steps

- [ ] T0 Prerequisite check: 0007 done (Diagnose, `libs/diag`, wizard contract); 0004 done (otherwise stop)
- [ ] T1 (moved to 0007 step A7: connection guide in the profile)
- [ ] T2 `ConnectWizard` (steps, start step per error, skip rules, session promotion with kept lock) + GTest
- [ ] T3 Topic table + `TroubleshootingWizard` + coverage test (every ConnectDevice/Troubleshooting code handled)
- [ ] T4 Choose-device page "Guide me" (0004 amendment), registration, Help menu items
- [ ] T5 Accessibility; Czech translation; `.ts` fresh; screenshots
- [ ] T6 Content: HARDWARIO supplies connector names, power wording and art (open question 22) — required for Done

## Tests and verification

- **GTest (fake J-Link library):** connect wizard happy path; each Diagnose failure leads to the right step and
  back; `target.wrong_family` names the other connector; already-connected tab skips to Check; cancel during
  Diagnose; probe lock kept across the session promotion (a second process cannot take it — `ProbeLock` test);
  every topic runs end to end with fake results; manual actions call `reset` / set the speed.
- **Coverage test:** the set of codes with wizard ConnectDevice/Troubleshooting in `errorTable` equals the codes
  covered by the Connect wizard's start mapping plus the topic table.
- **Qt Quick Tests:** each step in light/dark; 960 × 600 in Czech; keyboard walkthrough; check rows expand.
- **Sanitizers:** TSan with Diagnose progress racing Cancel and dialog close.
- **HIL (CHESTER + J-Link):** a user walkthrough from an unplugged J-Link to a connected console; power off during
  Check; cable on the wrong connector; J-Link Commander open; firmware without RTT.
- **Manual (by the user, before Done):** walkthrough on Linux, Windows and macOS; Czech texts at 960 × 600.

## Risks and open points

- **Connector names and art** are missing (open question 22); the wizard is functional but generic until then.
- **Cross-target Diagnose** depends on the M2 spike 0.5b (open question 18). If one J-Link cannot reach both MCUs
  from one connector, `target.wrong_family` is the expected result for the other target and the text must say
  "move the cable" rather than "switching".
- **Speed change** affects only this probe (ADR-0052 per-probe memory); the toast says so.
