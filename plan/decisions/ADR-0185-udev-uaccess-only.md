# ADR-0185 — Linux udev rule uses uaccess only (amends ADR-0072)

- **Status:** Accepted
- **Date:** 2026-10-29
- **Decided by:** Matej Strnad

## Context
ADR-0072 adopted SEGGER's `MODE="0666", TAG+="uaccess"`. Review M-3: `0666` gives every local account (other SSH
users, service accounts) read/flash/erase access to attached targets; `uaccess` alone already covers the seated user.

## Options considered
1. `TAG+="uaccess"` only. 2. `MODE="0660", GROUP="plugdev"` plus `uaccess`.

## Decision
Option 1: `SUBSYSTEM=="usb", ATTR{idVendor}=="1366", TAG+="uaccess"` in the packaged rule, the wizard text and the
suggested command. The USB-access check hints that users without a local seat (SSH only) need their own rule.

## Consequences
Headless/SSH-only use needs a manual rule. ADR-0072 stays valid otherwise.
