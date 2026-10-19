# ADR-0082 — NUS console behaviour (shell only, line pipeline, MTU chunking, reconnect)

- **Status:** Accepted
- **Date:** 2026-10-10
- **Decided by:** Matej Strnad (M5 planning session)

## Context

py-hardwario has no BLE, so the legacy app (branch `main`, `src/btworker.cpp`, `src/bluetooth.cpp`) is the only
behavioural reference. It uses the standard Nordic UART Service (`6e400001-b5a3-f393-e0a9-e50e24dcca9e`, RX write
`…0002…`, TX notify `…0003…`), carries the Zephyr shell only ("Device's log is not supported via Bluetooth", prompt
`bt_nus:~$ `), frames responses by waiting for the prompt in an unbounded buffer, sends each command as one write
without chunking, never reads the MTU, decides the write type with `properties() == WriteNoResponse` (equality
instead of a bit test), reports "sent" before the write completes and never reconnects.

## Options considered

The user asked to follow the old app (and py-hardwario where it applies) while fixing its defects. Alternatives were
"shell + logs routed by level" and "design for both".

## Decision

- **Content:** NUS carries the **shell only**. All received bytes go to the Shell channel through the M1
  `LineAssembler` (UTF-8 per line, U+FFFD, ANSI stripping, 4 KiB partial line). The profile's routing rules still
  apply, so if firmware ever sends `#`-prefixed or level-tagged lines they land in the Log channel; the GUI then
  shows the log pane (ADR-0081 hides it only while it is empty).
- **Prompt:** the profile's `shellPrompt` (`bt_nus:~$ ` for CHESTER) is recognised when the pending partial line
  equals it after ANSI stripping. It is not emitted as a line; the session emits a `PromptSeen` marker used by
  `hio ble command` as the end of a response (besides its timeout) and ignored elsewhere.
- **Writes:** after connect the link reads the negotiated MTU (fallback 23 → 20-byte payload). A command line + `\n`
  is split into chunks of `MTU − 3` bytes. Write-without-response is used only if the RX characteristic has the
  `WriteNoResponse` **bit**; otherwise write-with-response, one chunk in flight. The command is echoed (`dir = in`)
  only after the last chunk is confirmed (with-response) or handed to the stack (without-response). A write that
  does not complete within **2 s** → `ble.write_timeout`, the link is reported Disconnected.
- **Connection state:** `Connecting` until the TX notifications are enabled (CCCD written), then `Connected`. Link
  loss → `Disconnected` with `ble.disconnected`.
- **Auto-reconnect:** the same option as J-Link (CLI `--auto-reconnect`, GUI popover, default from settings): retry
  every **3 s**, but only while the device has been seen by a scan in the last 15 s (a background scan runs during
  reconnect). An explicit Reconnect always tries. Without the option the link stays Disconnected.
- **Bounds:** command queue 256, event queue 10 000 (as ADR-0031), outgoing line ≤ 4 KiB, ≤ 1 chunk in flight
  (with-response) / ≤ 16 chunks queued (without-response).

## Consequences

- Behaviour matches the old app for users; the defects listed above have regression tests in 0011.
- Firmware changes to the prompt need a profile change only.
