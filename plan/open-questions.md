# Open questions

Decisions that are not taken yet. Each must be asked of the user (and recorded as an ADR)
before the plan that depends on it is approved.

| # | Question | Needed by |
|---|---|---|
| 1 | ~~Replacement of nrfjprog~~ — **resolved**: J-Link library only (ADR-0015), modem firmware via optional nRF Util (ADR-0016) | — |
| 2 | Application update mechanism: notify only (GitHub releases) vs. in-app updater per platform? | M7 |
| 3 | Crash reporting / telemetry: none, opt-in crash dumps, which service? (M0: none — only a terminate handler that flushes the log; decided in M7) | M7 |
| 4 | ~~Localization~~ — **resolved**: English + Czech via Qt Linguist, extensible; CLI English only (ADR-0027) | — |
| 5 | ~~Product name, executable names, app ids~~ — **resolved**: "HARDWARIO Monitor", `hio-monitor`, CLI `hio`, `com.hardwario.monitor` (ADR-0018) | — |
| 6 | Code-signing certificates (macOS Developer ID, Windows) — owner and CI secret handling | M7 |
| 7 | Flatpak: how to grant USB (J-Link) and BLE access; is Flatpak viable with runtime-loaded vendor libs? | M7 |
| 8 | Windows installer format: Qt Installer Framework (as before) vs. MSI (WiX) vs. MSIX | M7 |
| 9 | MCP: existing C++ MCP SDK vs. own implementation; MCP protocol revision | M6 |
| 10 | ~~JSON library~~ — **resolved**: Qt `QJsonDocument` (ADR-0019) | — |
| 11 | In scope later: PIB/UICR write, firmware upload to catalog, LTE modem trace, serial console, other devices | after v2.0 |
| 12 | ~~Obtain `rttt` sources~~ — **resolved**: rttt 1.9.2 sdist from PyPI read for M1; RTT behaviour in ADR-0034 (re-read `mcp_server.py` for M6) | — |
| 13 | Linux arm64 and Windows arm64 builds? | M7 |
| 14 | ~~Minimum nRF Util version~~ — **decided**: the versions tested in the M2 Phase 0 spike (ADR-0042, Proposed until then); J-Link: 8.00 (ADR-0029) | M2 spike |
| 15 | Firmware catalog browsing (list/info API needs a HARDWARIO Cloud token): token source and storage (env only / OS keychain), GUI catalog browser (ADR-0037 defers it; M3 GUI uses the ID field only, ADR-0054). **M4: deferred again** — HARDWARIO is migrating to a new system that will provide an API; revisit when it exists | after the new HARDWARIO API |
| 16 | HARDWARIO brand colours for the GUI (palette in gui/design-system.md is a proposal, ADR-0047) | M3 review |
| 17 | Detach a device tab into its own window; several GUI instances (ADR-0058 makes the GUI single-instance); per-probe (instead of per profile/target) command history | after v2.0 |
| 18 | Can one J-Link reach both CHESTER MCUs from one connector (needed for cross-target operations, ADR-0057)? Answered by the M2 spike 0.5b | M2 spike |
| 19 | J-Link firmware update **button in the GUI** (console banner, doctor, troubleshooting). Deferred from M1/M3, not taken into M4; the legacy parity item ("offer firmware update", 0002) is covered by `hio jlink update-firmware` and the hint in the `probe-firmware` topic until then | M8 parity review |
| 20 | Documentation links per error code / wizard step (docs.hardwario.com URL table) — not in M4 | later |
| 21 | PIB / UICR read of the connected device in the diagnostic report and the GUI — not in M4 | later |
| 22 | CHESTER connection guide content: exact connector names, cable type, pin-1 marking, power wording, and the real illustrations for the wizard slots (ADR-0076, ADR-0077) — HARDWARIO content task | before 0009 is Done |
