# ADR-0004 — Backend may use non-GUI Qt modules only

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

The backend must be independent of the frontend, buildable and testable headless (CLI, MCP over stdio). Pure C++ would require reimplementing BLE, HTTP and an event loop for three OSes.

## Decision

Backend libraries (`libs/*`) may depend on **QtCore, QtNetwork, QtBluetooth** but **never QtGui, QtQuick or QtWidgets**. The frontend is a thin QML layer over ViewModels.

## Consequences

- Headless CLI and tests reuse the full backend.
- CMake enforces the rule (backend targets do not link GUI modules).
