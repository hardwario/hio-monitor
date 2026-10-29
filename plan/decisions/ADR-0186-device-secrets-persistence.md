# ADR-0186 — Device secrets are kept out of persistent files

- **Status:** Accepted
- **Date:** 2026-10-29
- **Decided by:** Matej Strnad

## Context
Review M-4: CHESTER shell commands carry keys, passwords and tokens; they are kept verbatim in command history,
console files, PIB hex dumps in logs, and the diagnostic report bundles those logs.

## Options considered
History: profile deny list / leading-space convention / "Remember commands" + "Clear history" (all chosen).
Logs: PIB redaction (chosen) / MCP argument redaction (not chosen — arguments keep being logged) / report pattern
redaction (chosen).

## Decision
- History and console files are created owner-only (`0600` on Unix; per-user AppData ACL on Windows).
- `TargetProfile::sensitiveCommandPrefixes`; `CommandHistory::add()` skips lines starting with a space or matching a
  prefix. CHESTER list (from chester-sdk `ctr_lrw`, `ctr_lrw_v2`, `ctr_lte`, `ctr_lte_v2`, `ctr_ble` shell configs):
  `lrw config appkey`, `lrw config nwkskey`, `lrw config appskey`, `lte config username`, `lte config password`,
  `ble config passkey` (prefix match on whitespace-normalised words). Commands that only *print* secrets
  (`info claim-token`, `info ble-passkey`) have no secret in the input and are not listed; their output stays in the
  console file, which is the user's own session log (owner-only).
- Setting `console/rememberCommands` (default true); "Clear history" in GUI and `hwio console history clear`.
- Console files write sensitive input lines as ` < <prefix> <redacted>`.
- PIB hex dumps in logs, console-file `@` lines and `Error.detail` show bytes 0x45–0x76 as `..`; `pib read` is full.
- Diagnostic report redacts `(password|passwd|token|secret|key|psk)[=:]value`, falls back to `getpwuid()` /
  `USERNAME` when `USER` is empty, and the dialog warns that logs may contain device output.

## Consequences
Secrets typed without a known prefix still reach history unless the user prefixes a space. MCP activity logs may
contain secrets sent by an AI client (accepted).
