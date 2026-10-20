# ADR-0168 — Target pickers show only `app` / `lte`

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (GUI feedback round 1, plan 0021)

## Context

The device bar's target combo box and the Choose-device target buttons list long titles ("Application MCU
(nRF52840)", "LTE modem MCU …"). Users know the targets as `app` and `lte` (CLI, docs, connector labels).

## Decision

- Every target picker (device bar, Choose-device page, wizards) shows only the target id: `app`, `lte`.
- The long title stays as the tooltip and as the accessible description (screen readers).
- Other texts that name an MCU in a sentence (maintenance title, warnings) keep the long names.

## Consequences

- Shorter, consistent pickers; ids are not translated.
