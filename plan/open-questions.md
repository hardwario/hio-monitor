# Open questions

Decisions that are not taken yet. Each must be asked of the user (and recorded as an ADR)
before the plan that depends on it is approved.

| # | Question | Needed by |
|---|---|---|
| 1 | ~~Replacement of nrfjprog~~ — **resolved**: J-Link library only (ADR-0015), modem firmware via optional nRF Util (ADR-0016) | — |
| 2 | ~~Application update mechanism~~ — **resolved**: notify only, GitHub releases, SemVer, non-modal banner (ADR-0116, 0016) | — |
| 3 | ~~Crash reporting / telemetry~~ — **resolved**: local crash dumps by an own handler, in the diagnostic report, never uploaded; no telemetry (ADR-0123) | — |
| 4 | ~~Localization~~ — **resolved**: English + Czech via Qt Linguist, extensible; CLI English only (ADR-0027) | — |
| 5 | ~~Product name, executable names, app ids~~ — **resolved**: "HARDWARIO Monitor", `hio-monitor`, CLI `hio`, `com.hardwario.monitor` (ADR-0018) | — |
| 6 | ~~Code-signing certificates~~ — **resolved**: Apple Developer ID + notarytool API key, Windows Azure Trusted Signing via OIDC, owned by HARDWARIO a.s., secrets in the `release` environment (ADR-0120); Trusted Signing eligibility still to be confirmed (0015 risk) | — |
| 7 | Flatpak: how to grant USB (J-Link) and BLE access (BLE needs D-Bus `--system-talk-name=org.bluez`, 0011); is Flatpak viable with runtime-loaded vendor libs? **Deferred from M7** (ADR-0118): own plan starting with a feasibility spike | after v2.0 |
| 8 | ~~Windows installer format~~ — **resolved**: per-machine MSI built with WiX (ADR-0117) | — |
| 9 | ~~MCP: existing C++ MCP SDK vs. own implementation; MCP protocol revision~~ — **resolved**: own implementation on QtNetwork + QJsonDocument (ADR-0096), MCP 2025-11-25 + 2026-07-28 (ADR-0095) | — |
| 10 | ~~JSON library~~ — **resolved**: Qt `QJsonDocument` (ADR-0019) | — |
| 11 | In scope later: PIB/UICR write, firmware upload to catalog, LTE modem trace, serial console, other devices | after v2.0 |
| 12 | ~~Obtain `rttt` sources~~ — **resolved**: rttt 1.9.2 sdist from PyPI read for M1; RTT behaviour in ADR-0034 (`mcp_server.py` re-read for M6, ADR-0099) | — |
| 13 | Linux arm64 and Windows arm64 builds? **Deferred from M7** (ADR-0124: v2.0 is x86_64 + macOS universal) | after v2.0 |
| 14 | ~~Minimum nRF Util version~~ — **decided**: the versions tested in the M2 Phase 0 spike (ADR-0042, Proposed until then); J-Link: 8.00 (ADR-0029) | M2 spike |
| 15 | Firmware catalog browsing (list/info API needs a HARDWARIO Cloud token): token source and storage (env only / OS keychain), GUI catalog browser (ADR-0037 defers it; M3 GUI uses the ID field only, ADR-0054). **M4: deferred again** — HARDWARIO is migrating to a new system that will provide an API; revisit when it exists | after the new HARDWARIO API |
| 16 | HARDWARIO brand colours for the GUI (palette in gui/design-system.md is a proposal, ADR-0047) | M3 review |
| 17 | Detach a device tab into its own window; several GUI instances (ADR-0058 makes the GUI single-instance); per-probe (instead of per profile/target) command history | after v2.0 |
| 18 | Can one J-Link reach both CHESTER MCUs from one connector (needed for cross-target operations, ADR-0057)? Answered by the M2 spike 0.5b | M2 spike |
| 19 | ~~J-Link firmware update button in the GUI~~ — **resolved**: banner, doctor Fix and troubleshooting step in M8 (ADR-0132, 0017) | — |
| 20 | ~~Documentation links per error code~~ — **resolved**: `docs/troubleshooting.md` anchors per code and `docsUrl()` (ADR-0138, 0017) | — |
| 21 | PIB / UICR read of the connected device in the diagnostic report and the GUI — not in M4 | later |
| 22 | CHESTER connection guide content: exact connector names, cable type, pin-1 marking, power wording, and the real illustrations for the wizard slots (ADR-0076, ADR-0077) — HARDWARIO content task | before 0009 is Done |
| 23 | CHESTER Bluetooth facts used by M5 (taken from the old app and py-hardwario only): advertised name format, whether NUS is in the advertisement, shell prompt `bt_nus:~$ `, passkey default 123456 and how bonds are cleared on the device; also BLE illustrations (ADR-0089) — confirm with the CHESTER firmware team | before 0011 is Done |
| 24 | Windows: does `QBluetoothLocalDevice::requestPairing` show a passkey dialog for CHESTER? If not, `hio ble pair` on Windows falls back to instructions (ADR-0084) | 0011 HIL |
| 25 | CHESTER RTT shell prompt string (app and lte targets) for `TargetProfile::shellPrompt` (ADR-0104); empty until recorded on hardware — `send_command` then ends by silence | 0013 HIL (E3) |
| 26 | Drop the legacy MCP dialect (2025-11-25) once Claude, Cursor and VS Code speak 2026-07-28 (ADR-0095) | after v2.0 |
| 27 | ~~v2 app icon~~ — **resolved**: the v1 icon is reused (0015); a later brand icon only replaces the files | — |
| 28 | Legal approval of the LGPLv3 §4 exception wording in `LICENSE` (ADR-0119) — release gate | before the first v2 release |
| 29 | Publishing the user docs (`docs/`, ADR-0138) on docs.hardwario.com: how and by whom — the user decides later | before v2.0.0 or after |
