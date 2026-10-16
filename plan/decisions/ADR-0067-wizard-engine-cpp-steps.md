# ADR-0067 — Wizard engine: C++ step machine, QML step components, kebab-case ids

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

The wizards have branches (Linux-only steps, optional nRF Util, check results that decide the next step) and
troubleshooting has a topic per error code. ADR-0003 forbids logic in QML. Texts must be translatable with Qt
Linguist (ADR-0027). M3 spells wizard ids two ways: `ErrorInfo.wizard` is a "kebab id" (0004), but 0006 writes
`FirstRunSetup`.

## Options considered

1. **A generic C++ step machine; one C++ class per wizard; QML renders step kinds (chosen).**
2. Data-driven flows in bundled JSON (texts hard to translate, errors found only at run time).
3. Pure QML per wizard (logic in QML).

## Decision

- `hio::gui::WizardViewModel` (generic) holds the step list, the current step, `canBack` / `canNext`, the
  default button, busy state and the check results of the current step. A `Wizard` class per wizard
  (`SetupWizard`, `ConnectWizard`, `TroubleshootingWizard`, `FlashWizard`) defines the steps and the transitions
  in C++ (`next()` decides the following step from the results).
- Each step has a **kind** that QML renders with a fixed component: `Info` (illustration + text), `Checks`
  (check list with Check again), `Choice` (radio cards), `Embedded` (a named control: probe picker, firmware
  source, phase progress), `Commands` (copyable command lines), `Done`.
- Troubleshooting topics are a **compiled-in table keyed by error code** (same idea as the device profiles,
  ADR-0032), with English source texts marked by `QT_TRANSLATE_NOOP`.
- Wizard ids are **kebab-case** everywhere outside C++: QML, JSON, settings (`first-run-setup`, `connect-device`,
  `flash-firmware`, `troubleshooting`, `ble-pairing`), as `libs/core/src/json.cpp` already serialises them. Plan
  tables keep the C++ enumerator names (`FirstRunSetup`); 0006's error table is corrected to say so.
- `WizardId` is not extended: the launcher passes the whole `ErrorInfo`, and the wizard picks its topic from the
  error code.

## Consequences

- Flows are unit-testable without QML (GTest drives `next()` / `back()` with fake check results).
- Adding a troubleshooting topic is a table row plus texts, no QML change.
- Changing a flow needs a rebuild; there is no run-time content update.
