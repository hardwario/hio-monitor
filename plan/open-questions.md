# Open questions

Decisions that are not taken yet. Each must be asked of the user (and recorded as an ADR)
before the plan that depends on it is approved.

| # | Question | Needed by |
|---|---|---|
| 1 | ~~Replacement of nrfjprog~~ — **resolved**: J-Link library only (ADR-0015), modem firmware via optional nRF Util (ADR-0016) | — |
| 2 | Application update mechanism: notify only (GitHub releases) vs. in-app updater per platform? | M7 |
| 3 | Crash reporting / telemetry: none, opt-in crash dumps, which service? (M0: none — only a terminate handler that flushes the log; decided in M7) | M7 |
| 4 | Localization: English only, or also Czech (Qt Linguist)? | M3 |
| 5 | ~~Product name, executable names, app ids~~ — **resolved**: "HARDWARIO Monitor", `hio-monitor`, CLI `hio`, `com.hardwario.monitor` (ADR-0018) | — |
| 6 | Code-signing certificates (macOS Developer ID, Windows) — owner and CI secret handling | M7 |
| 7 | Flatpak: how to grant USB (J-Link) and BLE access; is Flatpak viable with runtime-loaded vendor libs? | M7 |
| 8 | Windows installer format: Qt Installer Framework (as before) vs. MSI (WiX) vs. MSIX | M7 |
| 9 | MCP: existing C++ MCP SDK vs. own implementation; MCP protocol revision | M6 |
| 10 | ~~JSON library~~ — **resolved**: Qt `QJsonDocument` (ADR-0019) | — |
| 11 | In scope later: PIB/UICR write, firmware upload to catalog, LTE modem trace, serial console, other devices | after v2.0 |
| 12 | Obtain `rttt` sources (RTT console + MCP semantics reference) | M1 / M6 |
| 13 | Linux arm64 and Windows arm64 builds? | M7 |
| 14 | Minimum supported J-Link Software version and minimum nRF Util version | M1 / M2 |
