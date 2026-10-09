# ADR-0003 — UI built with Qt Quick / QML

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

The app needs friendly UX with illustrated wizards. Options: Qt Quick/QML or Qt Widgets.

## Decision

Use **Qt Quick / QML** for the UI. QML contains presentation only; logic lives in C++ ViewModels (`app/gui`) and backend libs.

## Consequences

- Rich, animated wizards are straightforward.
- QML is tested with Qt Quick Test; qmllint in CI.
- Strict rule: no business logic in QML.
