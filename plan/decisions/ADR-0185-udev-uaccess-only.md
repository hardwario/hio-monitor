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
suggested command. The suggested command installs it as `/etc/udev/rules.d/60-hardwario-jlink.rules` (a `99-*` file
runs after `73-seat-late.rules`, where `uaccess` is applied, and would grant nothing) and asks users to remove an old
`MODE="0666"` `99-jlink.rules`. The USB-access check hints that users without a local seat (SSH only) need their own rule.

## Consequences
Headless/SSH-only use needs a manual rule. ADR-0072 stays valid otherwise.

## Amendment 2026-11-02
Residual risk: SEGGER's J-Link Software installs its own `99-jlink.rules` with a world-accessible MODE, which keeps
probes world-rw although our rule is `uaccess` only. The app does not modify the vendor's file from the package; the
USB-access doctor check warns when a vendor-1366 device node is world-rw and offers the fix (review round 1, R1).
