# 0010 — M4 flash firmware wizard

- **Status:** Approved (2026-10-16, by Matej Strnad; without the pending M5 amendments); amended by 0021 (approved 2026-10-20 by Matej Strnad, implemented under 0021's checklist)
- **Milestone:** M4 (plan 4 of 4, ADR-0065)
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0174, ADR-0165, ADR-0166 (0021), ADR-0036, ADR-0037, ADR-0038, ADR-0040, ADR-0049, ADR-0054, ADR-0055, ADR-0057, ADR-0062,
  ADR-0066, ADR-0067, ADR-0069, ADR-0074, ADR-0076, ADR-0077
- **Depends on:** [0007](0007-m4-wizard-framework-diagnostics.md) (wizard contract, `Diagnose`, connection guide),
  [0006](0006-m3-flash-settings.md) (`FirmwareLoader`, `RecentSourcesModel`, Flash page). The hand-off to
  troubleshooting appears only when 0009's wizard is registered.
- **Can run in parallel with:** [0008](0008-m4-first-run-setup.md), [0009](0009-m4-connect-and-troubleshooting.md)
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

> **Amended by [0021](0021-gui-feedback-round-1.md) (approved 2026-10-20; ADR-0165, ADR-0166, ADR-0174; applied in this text,
> marked *amended by 0021*):** step 2 shows the two kind cards of the Flash page (Application firmware, LTE
> firmware); the Firmware step for LTE offers the designated LTE firmware (default) or an own file; catalog IDs show
> the firmware name and version.

## Context

The M3 Flash page (ADR-0049, ADR-0055) is a single form for everyday flashing. First-time users still fail before
they reach it: no probe chosen, cable on the wrong MCU, no power, a file of the wrong kind. The FlashFirmware errors
(`firmware.hex_parse_error`, `firmware.invalid_image`, `firmware.out_of_range`, `firmware.not_found_in_catalog`)
need a target for "Help me fix this". ADR-0074 decides a guided end-to-end flow that reuses the Flash page's
logic and the session operations.

## Goals / Non-goals

**Goals**
- The `flash-firmware` wizard: device and probe → what to flash → cable + live check → firmware → confirm →
  progress → result and hand-off to the console.
- Starting points per FlashFirmware error, with the problem explained.
- "Guide me" entry on the Flash page.
- EN + CS texts, accessibility.

**Non-goals**
- Catalog browsing (open question 15, deferred until HARDWARIO's new system provides an API).
- Maintenance actions (erase, recover) — they stay on the Flash page with ADR-0038 confirmations.
- New flash capabilities: the wizard uses exactly the operations of 0003 / ADR-0062.

## User experience

Step list: Device · Firmware kind · Cable · Firmware · Confirm · Flash · Done.

1. **Device and probe** [Embedded `probe-picker`] — as the Connect wizard's probe step (0009) without the target
   (the kind decides it). Skipped when the tab already has a probe.
2. **What do you want to flash?** [Choice] — the kind cards of the Flash page (ADR-0055; *amended by 0021, ADR-0174*:
   two cards, Application firmware · LTE firmware), with the same prerequisites inline (nRF Util for LTE firmware, with
   [Set up nRF Util…] → setup wizard at its nRF Util step, 0008).
3. **Cable** [Info + Checks] — the profile guide for the kind's MCU (ADR-0077) and the `Diagnose` steps `probe` …
   `identify` (RTT steps are not needed for flashing and are not run). `target.protected` is shown as information:
   "The MCU is protected. Flashing removes the protection only through Recover, which erases everything." with
   hand-off [Open Flash page]. Skipped when Diagnose passes at once (connected tab, same MCU).
4. **Firmware** [Embedded `firmware-source`] — the Flash page's source card (drop zone, Browse, catalog ID with
   [Download], recent list, summary, kind-mismatch hint with [Switch] — which changes step 2's choice and repeats
   step 3 if the MCU changes). Options under it: "Allow writing UICR" only when the image has UICR records
   (ADR-0036). "Halt after flashing" is not offered (expert option, Flash page only). *Amended by 0021 (ADR-0165,
   ADR-0166):* for LTE firmware the step offers the designated LTE firmware (default, downloaded and SHA-256 verified)
   or "Use your own file…"; a catalog ID shows the firmware name and version.
5. **Confirm** [Info] — "Ready to flash" summary: "chester-clime-v3.2.1.hex (Intel HEX, 412 kB) → Application MCU
   (nRF52840) of CHESTER via J-Link 683012345. The current application firmware is replaced. Configuration and the
   Product Information Block are kept." (UICR option on: "The UICR configuration is also written; the Product
   Information Block is kept."). Modem: "Updating the modem takes 1–3 minutes. Do not disconnect the device."
   Default button [Flash].
6. **Flash** [Embedded `phase-progress`] — the PhaseStepper with `plannedPhases`, step 1/2 for LTE bundles,
   progress, elapsed, [Cancel] (footer Cancel = cancel the operation, with "Flashing is in progress. Cancel it and
   close the wizard?"). The tab is busy (`setBusy("flash-firmware", "Flashing…", …)`).
7. **Done** [Done]
   - Success: "Firmware flashed and verified in 14 s." + chip. Buttons [Open console] (default; hand-off
     `open-console`, connects RTT for the flashed target if it is the console target) · [Flash another] (back to
     step 4) · [Close]. The source is added to the recent list (`flash/recent`, same as the page).
   - Failure: the ErrorPanel of the error inside the step, with [Try again] (back to step 5) and, when the error has a
     Troubleshooting wizard, [Help me fix this] → closes this wizard and opens troubleshooting on the same tab.
   - Cancelled: "Flashing was cancelled. The device may not start until you flash it again." [Flash again] · [Close].

### FlashFirmware errors (start points)

| Code | Opens at | Explanation shown above the step |
|---|---|---|
| `firmware.hex_parse_error` | Firmware | "The file is not a valid HEX file (line %1). Use the .hex file produced by the build, or download it from the catalog again." |
| `firmware.invalid_image` | Firmware | "This file is not CHESTER firmware. Application and LTE firmware are .hex files; LTE bundles and modem firmware are .zip files." |
| `firmware.out_of_range` | Firmware kind | "This firmware is for another MCU. Check whether it is application or LTE firmware." (preselects the other kind when the image fits it) |
| `firmware.not_found_in_catalog` | Firmware | "No firmware has this ID. Copy the ID from the firmware catalog again." + [Open firmware catalog ↗] |

When opened from an error, the wizard reuses the tab, probe and (when known) the kind, skipping steps 1–3 if
Diagnose passes.

### Entry points

- Flash page (0006): a ghost button "Guide me" in the page header (0006 amendment, text + button only).
- Choose-device page (0004): link "Flash firmware with a guide" under Connect.
- Help → "Flash firmware (guided)…".
- "Help me fix this" on FlashFirmware errors; hand-offs from the Connect wizard (`target.protected`, missing
  console) and the troubleshooting topic `no-console`.

### Texts

English source texts as above (the Flash page's existing texts are reused where identical). Czech is translated
in Qt Linguist as part of done (ADR-0027).

## CLI

None new. The CLI equivalent is `hio chester app|lte check` followed by `hio chester app|lte flash` (M2).

## Design

### Modules

```
app/gui/src/wizard/flash_wizard.*            FlashWizard : Wizard ("flash-firmware", per tab)
app/gui/src/viewmodels/firmware_source_view_model.*   extracted from FlashViewModel (0006 amendment, below)
app/gui/qml/flash/SourceCard.qml             (0006) reused unchanged, bound to FirmwareSourceViewModel
app/gui/tests/ wizard/flash_wizard_test.cpp tst_flash_wizard.qml
```

### Reuse of 0006 (amendment to 0006, applied with this plan's approval)

- 0006's `FlashViewModel` keeps its API, but its source part (kind-filtered loading, catalog download, recent list,
  summary, kind hint, `hasUicr`, `allowUicr`) moves into a child `FirmwareSourceViewModel` that both the Flash page
  and the wizard instantiate (each its own instance, sharing the app's `FirmwareLoader` worker and the
  `RecentSourcesModel` settings key). `SourceCard.qml` binds to `FirmwareSourceViewModel`.
- If 0006 is already implemented when this plan starts, the extraction is a refactoring step (W1) with the 0006
  tests kept green.

### `FlashWizard` behaviour

- Operation mapping identical to 0006 (§ Operation mapping): `flash` / `flashLteBundle` / `updateModem` on the
  tab's session with the kind's target; the session is ensured with `attachRtt = false` when the tab has none
  (ADR-0057 cross-target rules apply when the console runs on the other MCU).
- Diagnose (step 3) uses the same session; the wizard opens a probe-only session only when the tab has none and
  promotes it with `startConnection(attachRtt = false)` before flashing (0007).
- `[Open console]`: when the flashed target is the console target, the hand-off asks the tab to connect (or
  re-attach) RTT; otherwise it switches the tab's console target only after the user confirms ("Open the console of
  the LTE MCU?") — the LTE console is out of scope (0002), so for `lte` the button is hidden and [Close] is the
  default.
- Busy and cancel through the tab (`setBusy` / `clearBusy`), the operation id kept as on the Flash page.

### Error cases (GUI-originated)

| Situation | Code | Shown as | Wizard |
|---|---|---|---|
| Kind changed after the cable step to another MCU | (not an error) | step 3 repeats | — |
| nRF Util missing for modem | `dependency.nrfutil_not_found` (0003) | inline prerequisite in step 2; [Set up nRF Util…] | FirstRunSetup |
| Another operation running on the tab | `internal.busy` (0003) | the [Flash] button disabled with the reason | — |

All backend errors from 0003 are shown through `ErrorPanel` in the Done step.

### Illustration slots (ADR-0076)

Uses `jlink-usb`, `chester-app-cable`, `chester-lte-cable`, `chester-power` (defined in 0007). No new slots.

### Bounds

As 0006 (recent 10, loader queue 4, file limits of M2). One operation per wizard.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Flash wizard scope | guided flow / error help only / + catalog browser | guided flow | ADR-0074 |
| Catalog browsing and Cloud token | now / defer | defer; new HARDWARIO system will provide an API | open question 15 |
| Source logic reuse | extract `FirmwareSourceViewModel` / duplicate | extract (refactoring of 0006) | — (follows ADR-0067 "no logic in QML", no new decision) |

## Implementation steps

- [ ] W0 Prerequisite check: 0007 done; 0006 done or in progress with the amendment applied (otherwise stop)
- [ ] W1 Extract `FirmwareSourceViewModel` from `FlashViewModel` (0006 tests stay green)
- [ ] W2 `FlashWizard` steps, skip rules, start points per error + GTest
- [ ] W3 Progress, cancel, result, hand-offs (`open-console`, troubleshooting) + busy integration
- [ ] W4 Entry points: Flash page "Guide me", Choose-device link, Help menu, registration
- [ ] W5 Accessibility, Czech translation, `.ts` fresh, screenshots (light/dark, EN/CS, 960 × 600)

## Tests and verification

- **GTest (fake J-Link library, fake nRF Util):** full flow for each kind (app hex, app catalog ID via the local HTTP
  fixture, LTE hex, LTE bundle 1/2 → 2/2, modem); each FlashFirmware error start point; kind switch from the
  mismatch hint repeats the cable step only when the MCU changes; cancel at every phase; failure → Help me fix this
  hands off to troubleshooting; connected tab skips steps 1–3; UICR option only with UICR records.
- **Qt Quick Tests:** each step in light/dark; 960 × 600 in Czech; keyboard-only flash; Esc during flashing asks.
- **Sanitizers:** TSan with progress events racing Cancel and dialog close.
- **HIL (CHESTER + J-Link):** a new user flashes application firmware by catalog ID from an empty tab; LTE bundle;
  cable on the wrong MCU caught in step 3; cancel mid-flash; flash `lte` while the console streams from `app`.
- **Manual (by the user, before Done):** the flows on Linux, Windows and macOS.

## Risks and open points

- **Duplicate entry points** (page vs. wizard) can confuse; the page keeps "Guide me" small and the wizard is
  promoted for first-time users from the Choose-device page and the setup wizard's Done step.
- **Catalog browsing** remains out until the new HARDWARIO system provides an API (open question 15).
