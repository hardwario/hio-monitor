# ADR-0075 — Diagnostic report: zip with summary, logs and settings; redacted; never sent automatically

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

0003-ux-and-wizards asks for "Copy diagnostic report" for support tickets. The app log is rotated
(ADR-0020) and settings are an INI file (ADR-0025). Logs contain paths with the user's name.

## Options considered

- Format: **zip bundle + text summary (chosen)** / plain text only / zip including device console logs.
- Privacy: **redact home path, user name, host name; keep serial numbers and firmware ids (chosen)** /
  no redaction / redact serial numbers too.

## Decision

- File `hio-diagnostics-YYYYMMDD-HHMMSS.zip` (miniz, ADR-0041) containing:
  - `summary.txt` and `summary.json` — app version and git hash, OS and version, CPU architecture, Qt version,
    UI language, J-Link Software path and version, nRF Util path and versions, probes (product, SN, firmware,
    in use by), open tabs (profile, target, SN, state) in the GUI, `doctor` results, report time (UTC);
  - `logs/` — the current and the previous app log file of the GUI and the CLI, each truncated to its **last
    2 MiB** (cut at a line start);
  - `settings.ini` — the settings file.
  Device console logs are **not** included.
- Redaction (all text files): the home directory → `~`, the user name → `<user>`, the host name → `<host>`
  (whole-word, case-sensitive on Linux/macOS, case-insensitive on Windows). Probe SNs and firmware ids stay.
- GUI: Help → "Create diagnostic report…" opens a dialog that shows `summary.txt` (read-only, scrollable) and
  offers [Save report…] (file dialog) and [Copy summary] (summary.txt to the clipboard). CLI: `hio diag report
  [-o FILE]` (default file name in the current directory).
- Nothing is uploaded or e-mailed. The dialog says "The report stays on your computer. Attach it to your support
  request."
- Bounds: the whole zip ≤ 16 MiB; the summary ≤ 256 KiB.

## Consequences

- New error code `diag.report_write_failed` (category Io).
- Device logs needed for firmware issues are attached by the user separately (the console has Save view).
