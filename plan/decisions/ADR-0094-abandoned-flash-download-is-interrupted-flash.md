# ADR-0094 — An abandoned flash download is reported as an interrupted flash

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M2 Phase 0, item 0.10 / review R16)

## Context

`JLinkProgrammer::download()` buffers the image with `JLINKARM_BeginDownload` + `JLINKARM_WriteMem` and programs
it with `JLINKARM_EndDownload`. When buffering fails (a `WriteMem` error or the overall program deadline),
M2 "abandoned" the download by closing the J-Link without `EndDownload`, assuming that the library then
discards the buffer (review R3), and reported the plain failure.

Phase 0 item 0.10 tested that assumption on a CHESTER nRF52840 with J-Link Software 9.84: read a 4 KiB
application page, `BeginDownload`, `WriteMem` of four 1 KiB chunks of `0xA5`, `JLINKARM_Close` without
`EndDownload`, reopen, read the page back. **4091 of 4096 bytes had changed** — closing the probe programs the
buffered data. The plan's rule for this outcome: stop, new decision.

## Options considered

1. **Report as an interrupted flash (chosen):** keep closing the probe, but no longer claim that nothing was
   written. The error says that the firmware may be partly written and must be flashed again — the same
   situation as a power loss or unplugged cable during programming.
2. **Finish and verify:** never close mid-download; always call `EndDownload`, run the verify step and report
   exactly what is in flash. Deterministic, but slower on failure and still leaves a partly written MCU.
3. **Research other download APIs** (`JLINK_DownloadFile`, per-sector programming) that might be cancellable;
   pauses M2.

## Decision

- An abandoned download (buffering deadline, `WriteMem` failure) still closes the J-Link, and is reported with
  the error code `target.program_failed` and its existing message, plus an amended cause and suggestion. The
  cause says that programming was interrupted and the firmware may be partly written. The suggestion says to
  check the cables and power, then flash the firmware again. The `detail` names the abandon reason.
- Code comments and log text no longer state that buffered data is "never programmed".
- The session marks nothing special: the next flash overwrites the partly written image (sector erase + program).
- A cancel requested by the user during buffering is not added. A download stays non-interruptible, as decided
  for M2.

## Consequences

- The user gets an honest message; the device may not boot until it is flashed again, as after any interrupted
  flash. The UICR and the PIB are not touched by the abandoned download (they are not in the buffered image).
- Review R3/R16's guarantee ("nothing is programmed") is withdrawn; tests that asserted it are changed to
  assert the new error text.
- If a later J-Link version discards the buffer on close, the message is still correct ("may be").
