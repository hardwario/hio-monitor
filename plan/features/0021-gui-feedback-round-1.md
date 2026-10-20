# 0021 — GUI feedback round 1: flash page, target picker, console, Settings tab

- **Status:** Draft
- **Milestone:** M3 follow-up (changes the implemented plans 0004, 0005, 0006 and the flash wizard 0010)
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0164, ADR-0165, ADR-0166, ADR-0167, ADR-0168, ADR-0169, ADR-0170, ADR-0171, ADR-0172;
  amended: ADR-0033, ADR-0043, ADR-0045, ADR-0046, ADR-0055; context: ADR-0037, ADR-0040, ADR-0054, ADR-0058, ADR-0104
- **Depends on:** [0004](0004-m3-gui-shell.md), [0005](0005-m3-console.md), [0006](0006-m3-flash-settings.md),
  [0010](0010-m4-flash-wizard.md) implemented (all on `v2-dev`); 0003 (M2: catalog download, LTE operations);
  the one-shot command capture of 0002 (`hio chester app command`)
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md)

## Context

Matej Strnad tried the integrated GUI (`v2-dev`) and sent these notes (2026-10-20, translated):

| # | Note | Section |
|---|---|---|
| N1 | Merge LTE flashing into one card. | Flash page |
| N2 | Fix the link for application firmware to the docs page (and its English counterpart). | Flash page |
| N3 | For LTE there is one designated firmware: download it, and optionally offer flashing a custom one. | Flash page |
| N4 | For application firmware, find out and show what firmware it is. | Flash page |
| N5 | Target picker: only `app` / `lte`, no longer names in the list. | Shell |
| N6 | Scroll the log by lines. | Console |
| N7 | Autocompletion and suggestions in the console. | Console |
| N8 | Can Settings be another tab? | Shell |
| N9 | With the Info level switched off in the filter, hexdump lines (`44 74 4f 34 … \|DtO4xTs1 K6iAsASJ`) are still shown. | Console |

Answers to the clarifying questions are recorded in the ADRs listed above and in § Decisions.

## Goals / Non-goals

**Goals**
- Flash page with two kinds; the LTE card downloads the designated LTE firmware, custom file optional (N1, N3).
- Catalog ID → firmware name and version; the firmware in the device shown on the Application card (N4).
- Docs link per UI language (N2).
- Target pickers with ids only (N5).
- Console: whole-line scrolling (N6), history suggestions + device-command completion in GUI and CLI (N7), hexdump
  level hidden by default (N9).
- Settings as a tab in the tab strip (N8).
- EN + CS for every new string; CLI counterparts for every new backend feature.

**Non-goals**
- Checking for a newer LTE firmware than the pinned one (ADR-0165, option 3 rejected for now).
- Name/version detection inside a local `.hex` (ADR-0166).
- Firmware version of the LTE MCU (it has no console).
- Catalog browsing (open question 15 unchanged).

## User experience

### Flash page (N1–N4; ADR-0164 … ADR-0167)

1. **What do you want to flash?** — two cards:

   | Kind | MCU | Source |
   |---|---|---|
   | **Application firmware** | Application MCU (nRF52840) | `.hex` or catalog ID |
   | **LTE firmware** | LTE MCU (nRF91) + modem | the designated LTE firmware (default) or your own file |

   - Application card, when the tab's `app` console is attached: "In the device: hio-chester-push-fm v4.0.1"
     (from `info show`, ADR-0166). Not shown otherwise.
   - LTE card prerequisite: nRF Util (the designated bundle always contains modem firmware), inline as today.
2. **Firmware** card, Application kind — as in 0006, plus:
   - after a valid catalog ID: "hio-chester-push-fm v4.0.1 · uploaded 2026-09-07" (spinner "Looking up the
     firmware…"; "Unknown firmware — it can still be flashed." when the page is unreachable);
   - the link **"Find firmware IDs ↗"** opens the docs page in the UI language (ADR-0167).
3. **Firmware** card, LTE kind:
   - default row (radio): **"HARDWARIO LTE firmware 1.7.0 (recommended)"** — "LTE application and modem firmware".
     Choosing the LTE kind starts the download at once if it is not cached ("Downloading… 2.1 / 6.1 MB" [Cancel]);
     a cached, verified copy shows "Ready". Errors: `network.download_failed` with [Try again],
     `firmware.checksum_mismatch` with "The downloaded file is damaged. Try again." [Try again].
   - second radio: **"Use your own file…"** — drop zone, [Browse…] (filter: `*.hex *.zip`) and Recent (LTE kind,
     including former modem entries). Summary per classification (ADR-0040): "lte.hex · LTE application",
     "LTE bundle (application + modem 1.3.7)", "Modem firmware 1.3.7 (modem only)".
   - Notes as today: "Updating the modem takes 1–3 minutes. The console is disconnected meanwhile." for bundles and
     modem files.
4. Options, sticky bar, progress (step 1/2 for bundles), result and Maintenance unchanged (0006). The result card of a
   successful application flash shows the refreshed "Now in the device: … v4.0.2" when `info show` answers within 10 s
   of the console re-attaching.

Kind-mismatch hint only between the two cards: an LTE bundle or modem zip on Application → "This is LTE firmware —
switch to LTE firmware?" [Switch]; an `app` hex on LTE → "This is application firmware — switch?" [Switch].

### Target pickers (N5; ADR-0168)

- Device bar combo box: items `app`, `lte`; the closed box shows "Target: app" (unchanged). Choose-device buttons:
  `app`, `lte`. Tooltip and accessible description: the long title ("Application MCU (nRF52840)").

### Settings tab (N8; ADR-0172)

- The rail keeps its Settings item (gear, bottom, above Help). Clicking it or Ctrl+, opens Settings as a new tab in the
  tab strip, or switches to it when it is already open. The tab is there only while open (no permanent tab, no extra
  button in the tab strip).
- The tab: gear icon + "Settings"; no state dot, no rail, no device bar; the Settings page over the full width
  (max content width per design system).
- Closing: ×, Ctrl+W, middle-click. Not counted in the 8-tab limit, not restored at start.
- "Open Settings" (ErrorPanel primary action, wizard links, "Set path in Settings") opens the tab and scrolls to the
  section (`openSettings(sectionId)`).

### Console (N6, N7, N9; ADR-0169 … ADR-0171)

- **Scrolling:** wheel = system lines-per-notch (default 3) whole rows, no coasting; touchpad accumulated to rows; the
  top row always aligned; keys Up/Down, PgUp/PgDn, Ctrl+Home/End in the focused view.
- **Ghost text:** the rest of the highlighted suggestion (the first row of the list; a history entry of the same
  profile/target when no list is shown) is greyed after the cursor; → or End accepts it, Esc hides it.
- **Automatic suggestion list (above the input, no Tab needed):** as soon as the input is non-empty, a list opens
  above the input with the matching device commands for the current word (learned from `help`, subcommands on
  demand), then matching history entries (clock icon); 8 rows visible, max 50; each command row shows its help text
  muted. Keys while the list is open: ↑ ↓ move the highlight, Tab or → accepts the highlighted row, Esc closes the list
  until the next keystroke. **Enter always sends the typed line** unless the user moved the highlight with ↑ ↓ — then
  Enter accepts the row (pending minor default). With an empty input, ↑ ↓ walk the history as today. While the
  command list is being learned, the list shows "Reading commands from the device…".
- **Tab completion:** Tab completes the current word with the highlighted row, or with the common prefix when the
  list is closed.
- **Settings › Console:** "Suggest commands while typing" (`console/suggestions`, default on) and "Read the command
  list from the device on connect (sends `help`)" (`console/readCommandsOnConnect`, default on; off → `help` is sent
  the first time a suggestion is needed).
- **Hexdump chip:** the level chips become D I W E **H**; H ("Hexdump lines") is off by default; tooltip "Show hexdump
  lines". Hexdump rows use the muted text colour when shown.

## CLI

| Command | Change | Default (pending confirmation) |
|---|---|---|
| `hio chester lte flash [FILE] [--allow-uicr] [--no-cache]` | FILE becomes optional; without it the designated LTE firmware is downloaded (SHA-256 verified, cached) and flashed. Human output names it: "HARDWARIO LTE firmware 1.7.0". py-hardwario compatible (it requires FILE). | yes |
| `hio chester app catalog-info ID [--json]` (new) | Name, version, upload time, SHA-256 from the public catalog page. | name `catalog-info` |
| `hio chester app info [--json]` (new) | Runs `info show` and prints the parsed fields (`fw-name`, `fw-version`, …); `--json` gives the fields as an object. | name `info` |
| `hio chester app flash ID` | Prints the firmware name/version before flashing and verifies the SHA-256 when the page is readable. | yes |
| `hio chester app console` | replxx hints from history and Tab completion from the device command tree (ADR-0170). | yes |

`--json` outputs and exit codes follow 0006-cli; `firmware.checksum_mismatch` maps to the firmware exit code.

## Design

### Backend

- `libs/device` profile (`profile.h`):
  - `TargetProfile::designatedFirmware` — `std::optional<DesignatedFirmware>`
    `{ std::string_view name, version, url; std::array<std::uint8_t, 32> sha256; FirmwareKind kind; }`; CHESTER `lte`:
    `HARDWARIO LTE firmware`, `1.7.0`, `https://docs.hardwario.com/download/hio-chester-lte-v1.7.0.zip`,
    SHA-256 `95d301a11738bcd9a9df4726159361d64fd8f681c3546f479d46b49f4bf0fe49` (measured 2026-10-20), LTE bundle.
  - `DeviceProfile::firmwareDocsUrl` — `std::span<const LocalizedUrl>` (`{language, url}`), English first = fallback.
- `libs/firmware`:
  - `FirmwareCatalog::downloadDesignated(const DesignatedFirmware&)` → `std::shared_ptr<const LteBundle>`; same
    owner thread, limits and cancel as `download` (ADR-0037); cache `firmware-cache/lte-<sha256>.zip`; SHA-256 via
    `QCryptographicHash` before writing (QSaveFile) and on every hit.
  - `FirmwareCatalog::lookup(id)` → `CatalogInfo { name, version, uploaded, sha256 }`; fetches
    `<catalogUrl>/<id>` (1 MiB cap, 15 s timeout); `parseCatalogPage(std::string_view)` is a pure function
    (`[[nodiscard]] std::expected<CatalogInfo, Error>`), fuzz target `catalog_page_fuzz`.
  - `download(id)` verifies against `CatalogInfo::sha256` when a lookup succeeded.
  - New error code `firmware.checksum_mismatch` (what / why / fix texts, CS translation).
- `libs/device`:
  - `LogLevel::Hexdump` (`levelId` → `"hex"`), assigned by the line classifier after the level-tag parse
    (ADR-0171); unit tests on recorded CHESTER output.
  - `ShellQuery` — silent query on a session: `query(command, timeout)` → captured lines, built on the one-shot
    capture of `chester app command` and the ADR-0104 end rule; a user `sendLine` waits until a running query ends;
    no query while busy. Captured lines are not appended to the console log (they go to the app log at debug).
  - `DeviceInfo` — `parseInfoShow(lines)` → `{fwName, fwVersion, …}` (bounded: 64 fields, 256 B each).
  - `CommandTree` — nodes `{name, help, children}`, `parseHelp(lines)` for Zephyr `help` / `<cmd> -h` output;
    `CommandTreeCache` on disk (`AppLocalDataLocation/command-trees/`, JSON, bounds of ADR-0170, LRU, file lock like
    the history, ADR-0058); `Completer` — `suggest(input, history)` and `complete(input)` (pure, shared by GUI and CLI).
- `app/cli`: commands of § CLI; console wires replxx `set_hint_callback` / `set_completion_callback` to `Completer`.

### GUI

- `FlashViewModel::Kind` = `AppFirmware | LteFirmware`; `FirmwareSourceViewModel` gains
  `lteSource` (`Designated | OwnFile`), `designatedState` (`Idle | Downloading | Ready | Failed`), `catalogInfo`
  (`name`, `version`, `uploaded`, `lookupState`), `docsUrl` (by `LanguageManager` language);
  operation mapping per classification of the LTE input (0006 table: `flash` / `flashLteBundle` / `updateModem`).
- `DeviceTabViewModel::deviceInfo` (`fwName`, `fwVersion`, valid flag) filled by a `ShellQuery` after the `app`
  console attaches and after a flash; cleared on disconnect.
- `ConnectionViewModel::targets()` and `ProbePickerViewModel::targets` add `label` (= id); QML uses `label` for
  text, `title` for tooltip/`Accessible.description`.
- `ConsoleView.qml`: a `WheelHandler` that consumes wheel events and moves the view by whole rows
  (`wheelScrollLines`, pixel deltas accumulated), so the Flickable never coasts on wheel input (touch drag kept);
  row alignment via `positionViewAtIndex(row, ListView.Beginning)`; key handling per ADR-0169.
- `ShellInput.qml`: ghost-text overlay, Tab handling (`Keys.onTabPressed`; leaving the field per the pending
  minor default in § Decisions), `SuggestionList.qml` (above the input, opens on typing); `ConsoleViewModel::suggestion(text)`,
  `complete(text)` → `{replacement, candidates}`; tree learning through `ShellQuery`.
- `ConsoleLineModel::levelMask`: new bit `level_bits::hexdump`, default mask = D|I|W|E.
- Shell: `DeviceTabListModel` gets a non-device entry kind `Settings` (one at most, excluded from the limit and from
  `ui/tabs`); `AppViewModel::openSettings(QString sectionId = {})`; the rail's global Settings item calls
  `openSettings()` instead of showing the page in place; `PageHost` for the Settings entry hosts the `global` page.

### Threading

No new threads. Downloads and lookups run on the existing catalog owner thread; `ShellQuery` runs on the session's
worker (results back by queued signal); the command-tree cache is read/written on the GUI thread (small files) under
the cross-process lock.

### Errors

| Case | Code | User text | Wizard |
|---|---|---|---|
| Designated / catalog file hash differs | `firmware.checksum_mismatch` | "The downloaded firmware is damaged (checksum mismatch). Try again; if it repeats, report it to HARDWARIO." | — |
| Designated download fails | `network.download_failed` | existing texts, plus "You can also choose your own file." | — |
| Catalog page unreadable | (not an error) | "Unknown firmware — it can still be flashed." | — |
| `info show` / `help` query fails or times out | (not an error) | nothing shown / completion from history only | — |

### Limits

Catalog page 1 MiB / 15 s · designated download 32 MiB / 60 s (ADR-0037) · shell query 2 s, 2 000 lines · command
tree 2 000 nodes, 64 KiB per firmware, 20 firmwares · completion popup 50 rows · `info show` 64 fields.

### Settings keys

| Key | Type | Default |
|---|---|---|
| `console/suggestions` | bool | `true` |
| `console/readCommandsOnConnect` | bool | `true` |

Registered by the console settings (0005 `ConsoleSettings`), shared with the CLI console (`hio chester app console`
reads both; `--no-suggestions` overrides for one run). The H chip is not persisted, as the other chips. `flash/recent` entries with kind `modem` are read as LTE.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| LTE flashing in the GUI | three cards / two cards, LTE merged | Two cards, LTE merged (2026-10-20) | ADR-0164 |
| Designated LTE firmware source | pinned in profile / web manifest / pinned + newer check | Pinned in profile (2026-10-20) | ADR-0165 |
| What to show for application firmware | catalog name+version / device firmware / both | Both (2026-10-20) | ADR-0166 |
| Docs link | per UI language | EN/CS docs URLs (note N2) | ADR-0167 |
| Target picker | ids only | `app` / `lte`, long title in tooltip (note N5) | ADR-0168 |
| Log scrolling | whole rows, no coasting | Whole rows (2026-10-20) | ADR-0169 |
| Completion source | history / history + device commands / Tab to device | History + device commands, GUI and CLI (2026-10-20) | ADR-0170 |
| Hexdump lines | inherit level / own level hidden by default | Own level, hidden by default (2026-10-20) | ADR-0171 |
| Settings | rail page / tab in tab strip | Rail item stays; it opens Settings as a tab that exists only while open (2026-10-20) | ADR-0172 |
| Plan form | new plan + notes / edit 0004–0006 | New plan 0021 + pending notes (2026-10-20) | — |
| When to send `help` / `info show` | on connect / lazily | `help` on connect, configurable in Settings (`console/readCommandsOnConnect`) (2026-10-20); `info show` on connect | ADR-0170, ADR-0166 |
| Suggestions without Tab | Tab only / automatic list | Automatic list above the input, can be switched off (2026-10-20) | ADR-0170 |
| *Minor defaults (pending):* Enter sends the typed line; it accepts a suggestion only after the user moved the highlight with ↑ ↓ | as proposed / Enter always accepts | _pending_ | ADR-0170 |
| *Minor defaults (pending):* CLI names `catalog-info`, `info`; `lte flash` without FILE | as proposed / other | _pending_ | — |
| *Minor defaults (pending):* Tab in the shell input completes instead of moving focus; the focus leaves the field with Shift+Tab, or Esc then Tab (Ctrl+Tab keeps switching tabs) | as proposed / other | _pending_ | ADR-0170 |
| *Minor defaults (pending):* designated LTE download starts automatically when the LTE kind is chosen | automatic / on [Download] | _pending_ (proposed: automatic) | ADR-0165 |

## Implementation steps

- [ ] A1 Profile: `designatedFirmware`, `firmwareDocsUrl`; `firmware.checksum_mismatch` (+ texts, CS) + tests
- [ ] A2 `FirmwareCatalog::downloadDesignated` + SHA-256 verify + cache; `lookup` + `parseCatalogPage` + fuzz; ID
      download verified by the lookup hash; local HTTP fixture extended
- [ ] A3 `LogLevel::Hexdump` classification + tests on recorded output; `levelId` "hex" in console files / JSON
- [ ] A4 `ShellQuery`, `parseInfoShow`, `CommandTree` + `parseHelp`, `CommandTreeCache` (lock, LRU, bounds),
      `Completer` + tests (fake J-Link scenario with a Zephyr shell)
- [ ] B1 CLI: `lte flash` without FILE, `app catalog-info`, `app info`, ID info before `app flash`, replxx hints and
      completion + CLI end-to-end tests
- [ ] C1 Flash page two kinds, LTE source (designated / own file), catalog info, device info, docs link, kind hints;
      flash wizard 0010 step 2 + Firmware step aligned
- [ ] C2 Target pickers ids only (device bar, Choose device, wizards)
- [ ] C3 Settings tab (tab model entry, rail item + Ctrl+, open/switch, `openSettings(section)`)
- [ ] C4 Console: line scrolling, ghost suggestion, automatic suggestion list above the input, Tab completion, the
      two console settings, H chip default off
- [ ] D1 `update_translations`, Czech for every new string; `check_translations`
- [ ] D2 Docs: `docs/` user guide sections for Flash (LTE), console completion, Settings tab; plan notes in
      0004/0005/0006/0010 marked applied; 0006-cli table updated

## Tests and verification

- **GTest:** profile entries; designated download success / hash mismatch / cache hit re-verify / cancel; catalog page
  parser on the saved page and on mutated input (fuzz 10 min in CI fuzz job); hexdump classifier (real CHESTER
  hexdump, near-misses, header line keeps its level); `parseInfoShow`; `parseHelp` (top level, subcommands, ANSI);
  `CommandTreeCache` bounds, LRU and two-process lock; `Completer` (suggestion order, common prefix, 50 cap);
  `ShellQuery` (end rule, user line waits, busy refusal, output not in the console log).
- **ViewModel tests:** Flash kinds and operation mapping for designated / hex / bundle / modem file; kind hints;
  catalog info states; device info after attach and after flash; Settings tab model (limit, not restored,
  `openSettings(section)`); target labels; level mask default.
- **Qt Quick Tests:** Flash page both kinds in light/dark, 960 × 600 Czech; Settings tab open/close/cycle; wheel
  scrolling moves whole rows and aligns the top row; ghost text, the automatic list (opens on typing, ↑ ↓ / Tab / Esc / Enter rules, off by setting), `help` sent
  on connect or lazily per setting; H chip off by default.
- **CLI end-to-end:** `lte flash` without FILE against the HTTP fixture (fake nRF Util); `app catalog-info --json`;
  `app info --json` with the fake shell.
- **Sanitizers / tidy:** debug, ASan+UBSan, TSan, `linux-tidy` zero findings, clang-format, qmllint.
- **HIL (CHESTER + J-Link):** designated LTE firmware download + flash (step 1/2 modem, 2/2 application); catalog ID
  shows name/version and flashes; "In the device" before and after; `help` learning and completion on real firmware;
  hexdump hidden with the default chips; wheel scrolling on Linux, macOS (touchpad) and Windows.

## Risks and open points

- **Catalog page scraping** can break when the page changes; isolated parser, graceful "Unknown firmware". Replace
  with the new HARDWARIO API (open question 15).
- **Docs download URL** of the LTE bundle may move; profile change + release.
- **Silent queries** appear in the device's own shell history and could collide with output the device prints at the
  same time; the end rule and the 2 s cap limit the impact. Firmware without `help` / `info show` → no completion
  from the device, no device info.
- **CHESTER prompt** for the end rule is still open question 25 (ADR-0104 silence rule meanwhile).
- **Hexdump false positives:** only exact continuation format; tested on real output.
- The LTE connector note of the docs ("Connect the 10-pin flat cable to the connector labeled APP") must be checked
  against ADR-0057 Phase 0 (cable guidance per MCU) during HIL.
