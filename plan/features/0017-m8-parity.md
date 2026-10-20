# 0017 — M8 parity: audit, J-Link firmware update in the GUI, CLI compatibility, HIL CI, user docs

- **Status:** Draft
- **Milestone:** M8 (plan 1 of 2, ADR-0131; followed by [0018](0018-m8-beta-release-retirement.md))
- **Author:** Matej Strnad (with Claude), 2026-10-19
- **Related ADRs:** ADR-0006, ADR-0012, ADR-0017, ADR-0024, ADR-0026, ADR-0075, ADR-0099, ADR-0105, ADR-0106,
  ADR-0125, ADR-0131, ADR-0132, ADR-0137–ADR-0140
- **Depends on:** M1–M7 implemented and merged into `v2-dev` (the audit runs on the integrated build). Phases C
  (CLI compat) and E (docs) can start as soon as the CLI of M1–M6 is merged; phase D needs the runner hardware.

## Context

The vision's first success criterion is "every feature of the old app has an equivalent (parity checklist in 0002) and
the old app can be retired". The checklist rows were assigned to plans 0002–0016 but nobody has ticked them against the
integrated build. One legacy behaviour is still missing in the GUI: offering a J-Link firmware update (open question
19). The CLI promises py-hardwario compatibility (0006-cli.md principle 1) without a systematic check. HIL testing is
manual, and users have no documentation outside the app.

## Goals / Non-goals

**Goals**
1. Audit every row of the 0002 parity checklist on the integrated `v2-dev` build on Linux, macOS and Windows, with
   evidence; fix gaps or record them as accepted differences approved by the user.
2. J-Link firmware update in the GUI (ADR-0132).
3. CLI compatibility snapshot, expectations and CTest; `docs/cli-migration.md` (ADR-0139).
4. HIL workflow on a self-hosted runner, part of the release gate (ADR-0140).
5. User documentation in `docs/` and the `docsUrl()` link table (ADR-0138).

**Non-goals**
- Beta, release, branch switch (0018).
- New features beyond parity (PIB/UICR write, catalog upload, modem trace, other devices — open question 11).
- py-hardwario changes (ADR-0137); publishing to docs.hardwario.com (open question 29).

## User experience

**J-Link firmware update (ADR-0132)**

Banner on the device bar after connect when the probe firmware is outdated:

> ⚠ The J-Link firmware is outdated (2024-05-10, available: 2026-02-03). **[Update]** [Not now]

Confirmation dialog:

```
Update J-Link firmware?
<probe product> · SN <serial>
Current:   <firmware string>
New:       <firmware string from the J-Link library>
Do not unplug the J-Link during the update (about 10–30 s).
The console of this device is paused and resumes afterwards.
                                       [Cancel]  [Update]
```
(Probe product and serial come from the probe at runtime.) Progress: indeterminate spinner "Updating J-Link
firmware…", then a success toast "J-Link firmware updated." or the `probe.firmware_update_failed` error panel. The
doctor row `probe-firmware` gets [Update]; the troubleshooting topic `probe-firmware` gets an [Update J-Link firmware]
step, with the CLI command still shown as an alternative.

**Docs:** About and error panels link "Documentation" / "More help" to `docs/…` pages (ADR-0138).

## CLI

- `hio --dump-command-tree` (hidden, JSON of the CLI11 command tree; used by the compat test only).
- No change to `hio jlink update-firmware`; it and the GUI share the backend operation.
- Any compat gap found in phase C is fixed as an alias or option (CLI11 aliases) unless the user accepts it as a
  difference.

## Design

### A. Parity audit

The table below is copied into the plan's verification record and filled during the audit (OS columns = evidence:
test name, manual check id or screenshot under LFS).

| 0002 row | v2 plan(s) | Automated evidence | Manual check |
|---|---|---|---|
| RTT attach/detach, probe by SN, speed | 0002, 0005, 0009 | device tests on fake J-Link | HIL + GUI on 3 OSes |
| Shell on channel 0, echo highlight, error colouring | 0002, 0005 | console model tests | GUI |
| Device log on channel 1, level colouring | 0002, 0005 | log parser tests | GUI |
| Log search (regex, highlight, next/prev) | 0005 (Enter / Shift+Enter) | `FindFilterEngine` tests | GUI; v1 keys `n` / Shift+`n` added as aliases (item A3) |
| Log filter | 0005 | engine tests | GUI |
| Pause/resume, clear, autoscroll, 100 000-line view | 0005 | model bound tests | GUI |
| Multi-line selection and copy | 0005 | Qt Quick tests | GUI |
| Command history (persisted, Up/Down, Ctrl+R) | 0002, 0005 | history tests | GUI + CLI |
| Batch commands from file (200 ms) | 0005 | batch tests | GUI, path with spaces on 3 OSes |
| Open log file | 0005 | — | GUI |
| BLE scan/connect/NUS shell | 0011, 0012 | BLE fake tests | HIL manual (BLE not in runner) |
| Flash local .hex | 0003, 0006, 0010 | flash tests | HIL |
| Flash by catalog ID, cache | 0003, 0006 | catalog tests | HIL |
| Flash log | 0003, 0006 | — | GUI |
| Update check | 0016 | update tests | packaged build |
| Doc / HARDWARIO links | 0006, ADR-0138 | link table test (every `docsUrl` topic has a page) | GUI |
| J-Link firmware outdated → offer update | ADR-0132 (this plan) | view-model tests | HIL manual with an outdated probe |
| py-hardwario behaviour (program/erase/recover/LTE/RTT/PIB read/catalog) | 0002, 0003 | backend tests | HIL suite (D) |
| Old-app and py-hardwario defects must not recur | all | regression tests named after the defect | review |

Rules: a row is ticked only with evidence on all three OSes (BLE: OSes where M5 supports it). A gap is either fixed in
this plan (new checklist item) or written into the decisions table as an accepted difference with the user's answer.

### B. J-Link firmware update in the GUI (ADR-0132)

- `ProbeFirmwareViewModel` (app/gui): `status` (Current/Outdated/Newer/Unknown), `currentString`, `availableString`,
  `canUpdate`, `blockedReason`; `Q_INVOKABLE requestUpdate()` → dialog; `confirmUpdate()`.
- Flow on the session owner thread: if a `DeviceSession` is open on the probe → `pauseForProbeMaintenance()` (stop
  RTT, release the probe but keep the probe lock, ADR-0058) → `updateFirmware()` via the existing backend operation →
  re-open → re-attach → resume. Without a session: short-lived probe session as `hio jlink update-firmware` does.
- Blocked when MCP holds the device (ADR-0105) or a flash/erase operation is running ("Wait for the current operation
  to finish").
- New error `probe.firmware_update_failed` (category probe, EN + CS, troubleshooting `probe-firmware`); CLI
  `hio jlink update-firmware` maps the same failure to it (exit 5).
- Banner suppression "Not now": in-memory set of probe SNs (bounded 32).

### C. CLI compatibility (ADR-0139)

- `tools/cli-compat/extract_py_hardwario.py` → `tests/cli-compat/py-hardwario-1.7.2.json` (committed).
- `tests/cli-compat/expectations.json`, CTest `cli_compat`, generator for `docs/cli-migration.md`.
- Items to resolve during the phase (each becomes a decision row if not a plain alias): console `--reset`,
  `--coredump-file`, `--device`; `device <family>` group; default HEX argument discovery (`find_hex('.')`, py-hardwario
  flashes the build output in the current directory when no argument is given).

### D. HIL CI (ADR-0140)

- `.github/workflows/hil.yml` (dispatch + `v2.*` tags), environment `hil`, runner labels `self-hosted, hil, chester`.
- `tests/hil/` CTest label `hil`: scripts drive `hio` with `--json` and assert results; fixtures: a known-good
  CHESTER app hex (built from a public HARDWARIO sample, LFS) and a catalog id.
- `hil/README.md`: runner setup (OS, J-Link Software, nRF Util, udev, service user, power switch), maintenance.

### E. User documentation (ADR-0138)

- `docs/` pages listed in ADR-0138; `cli.md` generated from `hio --help` per command (CTest `docs_cli_fresh` fails
  when stale); `troubleshooting.md` anchors = `hio::Error` codes (CTest: every code in the registry has an anchor).
- `libs/core` `docsUrl(DocsTopic)` table → `https://github.com/hardwario/hio-monitor/blob/main/docs/<page>.md#<anchor>`;
  used by About, wizards and error panels ("More help").

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Next milestone to plan | M8 | M8 | — |
| Where to write | new `v2/m8-plan` worktree | `v2/m8-plan` in `../hio-monitor-m8-plan` from 40f8e40 | — |
| M8 split | two / three / one | two: parity (0017), beta + release + retirement (0018) | ADR-0131 |
| J-Link firmware update in the GUI | in M8 / CLI only | in M8 | ADR-0132 |
| CLI compatibility check | generated table + test / manual | generated snapshot + CTest | ADR-0139 |
| HIL CI | opt-in workflow + release gate / manual / every PR | opt-in self-hosted, release gate | ADR-0140 |
| User docs | in-repo / docs site / both | both; `docs/` now, docs.hardwario.com publishing later (user) | ADR-0138 |
| py-hardwario deprecation | out of scope / notice | out of scope | ADR-0137 |
| Czech user documentation | English only in v2.0 / EN + CS | English only in v2.0 | ADR-0138 |
| Old search keys `n` / Shift+`n` (v1) | add as aliases in the log view / Enter + Shift+Enter only | add as aliases (log view focus, search active) | — |
| Minor defaults: banner "Not now" per probe until restart; session paused and resumed around the update; blocked while MCP or a flash runs; error `probe.firmware_update_failed`; HIL suite contents and 20-min bound; modem flash and recover only on dispatch; `docsUrl` points to GitHub `main`; parity row ticked only with evidence on all three OSes; `--dump-command-tree` hidden option | accept / change | accepted (2026-10-19) | — |

## Implementation steps

- [ ] A1 Integrated build of `v2-dev` with M1–M7; fill the parity table (automated evidence first).
- [ ] A2 Manual parity session on Linux, macOS, Windows; gaps → items below or decision rows.
- [ ] A3 v1 search keys: `n` / Shift+`n` jump to next / previous match while the log view has focus and a search is
  active (not in text fields); Qt Quick test; shortcut overlay (F1) updated.
- [ ] B1 `pauseForProbeMaintenance` / resume in the session layer + tests on the fake J-Link (update success, failure,
  unplug mid-update simulated by the fake).
- [ ] B2 `ProbeFirmwareViewModel`, banner, dialog, doctor Fix, troubleshooting step; Qt Quick tests; translations.
- [ ] C1 Extract script + snapshot; `--dump-command-tree`; expectations; CTest `cli_compat`.
- [ ] C2 Resolve listed items (aliases/options) or decision rows; `docs/cli-migration.md` generator.
- [ ] D1 Runner set-up (HARDWARIO), `hil/README.md`.
- [ ] D2 `tests/hil/` suite, `hil.yml`, release gate wiring with `release.yml`.
- [ ] E1 `docs/` pages, `docsUrl` table + tests, `cli.md` generator + freshness test.
- [ ] E2 Links from About, wizards and error panels.
- [ ] F1 All parity rows ticked (0002 updated), plan verification record complete.

## Tests and verification

- Unit/Qt Quick tests for B; `cli_compat`, `docs_cli_fresh`, docs-anchor tests; full suite with ASan/UBSan, TSan,
  clang-tidy, clang-format, qmllint, `check_translations`.
- HIL workflow green on dispatch.
- Manual: J-Link firmware update with an outdated probe on Linux, macOS, Windows (including cancel, unplug during
  update → error and recovery via J-Link Commander); parity session per OS.

## Risks and open points

- An outdated J-Link to test the update may be hard to find once updated; J-Link Commander can't downgrade. Keep one
  probe on old firmware for testing, or accept fake-only coverage for some branches.
- The audit can surface more gaps than expected; each becomes a decision for the user, which can delay 0018.
- The self-hosted runner is a single point of failure for releases; 0018's checklist allows a documented manual HIL
  run when the runner is down (decision in 0018).
