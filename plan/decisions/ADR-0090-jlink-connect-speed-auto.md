# ADR-0090 — J-Link connect speed: adaptive (auto) by default

- **Status:** Accepted
- **Date:** 2026-10-11
- **Decided by:** Matej Strnad (M1 independent review round 6)

## Context

M1 connected the target at a fixed SWD speed, `--jlink-speed` with the profile default 2000 kHz, because
rttt 1.9.2 and py-hardwario pass `--jlink-speed` (default 2000) to pylink's `JLink.set_speed()` before the
connect. The round-6 review read pylink-square 2.0.2 (`pylink/jlink.py`) and found that the reference does
not actually run at that speed:

- `JLink.connect(chip_name, speed='auto', verbose=False)` sends `exec_command('Device = <chip>')` and then,
  still before `JLINKARM_Connect`, sets the speed again: for the default `speed='auto'` it calls
  `set_speed(auto=True)`.
- `JLink.set_speed(speed=None, auto=False, adaptive=False)`: with no speed, `speed = 0`; with `auto`,
  `speed = speed | AUTO_JTAG_SPEED`, and `AUTO_JTAG_SPEED = 0x0`; then `self._dll.JLINKARM_SetSpeed(speed)`.
  The library is therefore called with **`JLINKARM_SetSpeed(0)`**, the automatic speed.
  (`ADAPTIVE_JTAG_SPEED = 0xFFFF` is a different mode, adaptive clocking, which pylink uses only for
  `speed='adaptive'`.)
- rttt (`rttt/cli.py`, `PyLinkRTTConnector._reopen_jlink()`) and py-hardwario (`hardwario/device/__init__.py`
  `jlink_setup()`, `hardwario/chester/cli/app.py` `command`) all call `jlink.connect(device)` without a speed
  argument, so the automatic speed overrides the `set_speed(2000)` they made before, for the connect and
  for the whole session. Only the window between `Device =` and that call (pylink notes that `Device =` may
  trigger an auto-connect) can see the earlier speed.

So the reference connects at the automatic speed, and an explicit `--jlink-speed` has no effect there.

## Options considered

1. **Auto, like pylink (chosen):** by default `JLINKARM_SetSpeed(0)` right after `Device =` and before
   `JLINKARM_Connect`; an explicit kHz value forces that fixed speed.
2. **Fixed:** keep the fixed `--jlink-speed` (default 2000 kHz) for the connect and the session, a documented
   difference from the reference.

## Decision

- The J-Link speed is **automatic by default**, as pylink's `JLink.connect(chip, speed='auto')`:
  `JLinkProbe` sets the speed before the target interface is selected and again right after `Device = <name>`,
  before `JLINKARM_Connect` — the automatic speed is the existing `IJLinkApi::setSpeed()` with the value
  `jlink::autoSpeed` (= 0; no new library function, ADR-0028 declaration unchanged and its source cited).
- **An explicit kHz value forces that fixed speed** for the connect and the session (the same two calls with
  that value). This differs from rttt / py-hardwario, where `--jlink-speed` is overridden by pylink's auto.
- CLI: `--jlink-speed auto|KHZ`, default `auto`; a kHz value must be 5–50 000 (pylink's `MIN_JTAG_SPEED` /
  `MAX_JTAG_SPEED`); anything else is `usage.invalid_argument`.
- Device profiles: `TargetProfile::defaultSpeedKhz` is `std::optional<int>`, `nullopt` = automatic; CHESTER's
  targets use `nullopt`. `SessionOptions::speedKhz` is `std::optional<int>` with the same meaning.
- The rule applies to every place that connects a target. `hio jlink info` and `hio jlink update-firmware` do
  not connect a target (they read the probe status and firmware only) and therefore set no speed.

## Consequences

- Sessions behave like the reference by default; a target or cable that needs a low speed can still be forced
  with an explicit kHz value.
- The connect timeout (35.5 s, plan 0002 R47) is not scaled with the speed; the low-speed caveat now applies
  only to an explicit low kHz value. HIL (plan 0002 G1) measures connect and attach at `auto` and at 5, 100
  and 1000 kHz.
- M2 (flashing, plan 0003) inherits the default and the `auto|KHZ` option syntax for its `--jlink-speed`
  through the shared probe code; its plan is aligned when it is next revised.
- Tests: GMock call order `Device = …` → `setSpeed(0)` → `connect()` for auto, the fixed value for an explicit
  kHz, a session test of both, and CLI parsing (`auto`, `2000`; `fast`, `1` → usage error).
