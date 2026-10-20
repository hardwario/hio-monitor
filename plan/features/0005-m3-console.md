# 0005 — M3 console page: shell + log, search/filter, history, batch

> **Amended by the 0004 review (applied 2026-10-16):**
> - Console markers are owned by the shell (`DeviceTabViewModel`): "— connected —", "— disconnected —",
>   "— disconnected: <cause> —", "— paused: <busy text> —" and "— resumed —". The console view model only shows
>   them (§ Busy integration and § Markers below).
> - F4 (reconnect) is a global shortcut of the shell; the console toolbar's Reconnect shows it in its tooltip but
>   does not register it (§ Shortcuts).
> - Console shortcuts are registered with `QT_TRANSLATE_NOOP("Shortcuts", "…")` descriptions.
> - `DeviceTabViewModel::ensureSession()` has no parameters (console target with RTT), and `sessionChanged` is also
>   emitted on tab close / quit: the console keeps the session only as `QPointer` (§ Session change).

> **Amended after review round 1 (2026-10-16, user decisions; applied in this text):**
> - Session warnings are shown by the shell's banner only (0004 `PageHost`, amended): it has a [Details] toggle for
>   warnings with details (e.g. `probe.firmware_outdated`), and the console file's `io.write_failed` warning reads
>   "The console log file could not be written; logging to file is off for this session." The console page does
>   not repeat session warnings (§ Banners).
> - An invalid regular expression keeps the last valid filter result / search matches while the field shows the
>   error (ADR-0046); rows are plain text with highlight rectangles from `matchRanges` (ADR-0045), matched on the
>   shown text (§ Data flow).
> - Open log file uses `DeviceSession::consoleFile()` (0002 amendment) instead of a file lookup (§ Data flow).
> - Selection is whole rows only in M3 (ADR-0113 supersedes ADR-0045's partial selection within a row).

- **Status:** In progress (implementation started 2026-10-16; approved by Matej Strnad, 2026-10-16, with the amendment
  from the 0004 review applied)
- **Milestone:** M3 (plan 2 of 3, ADR-0059)
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0003, ADR-0025, ADR-0027, ADR-0031, ADR-0033, ADR-0034, ADR-0045, ADR-0046, ADR-0051,
  ADR-0056, ADR-0058, ADR-0059, ADR-0061, ADR-0062, ADR-0063, ADR-0113
- **Depends on:** [0004](0004-m3-gui-shell.md) (shell, page-extension contract), 0002 (M1: `DeviceSession`,
  `ConsoleLog`, `CommandHistory`). It uses the M2 `reset(targetId, ResetMode)` operation (ADR-0062) when 0003 has
  landed; before that, M1's `reset(bool)`.
- **Can run in parallel with:** [0006](0006-m3-flash-settings.md)
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

> **Pending amendment from M5 ([0012](0012-m5-ble-gui-pairing.md), ADR-0079; applied when 0012 is approved — then
> this plan needs re-approval):** `ConsoleViewModel` binds to `DeviceTabViewModel::consoleSession()` (J-Link or
> BLE); Reset is hidden without the `Reset` capability; the log pane is hidden while the Log channel is empty for
> BLE tabs.


> **Pending amendment from M6 (ADR-0105, specified in [0014](0014-m6-mcp-gui.md)):** AI activity marker rows and the
> input placeholder "AI is running a command…" while an AI `send_command` collects output.

> **Pending amendment from M8 ([0017](0017-m8-parity.md) item A3):** in the log view (focus not in a text field) with an
> active search, `n` / Shift+`n` go to the next / previous match, as in v1.

## Context

The console is the screen users spend most of their time on. The old app's workflows must be kept:

1. The shell and the log are visible side by side.
2. Command history (Up/Down, Ctrl+R) survives restarts.
3. Log search (next/previous) and filter.
4. Pause, clear, copy, batch files and opening the log file.

Its pain points must be fixed:
- Search mode took over the view and paused silently, and search vs. filter was unclear.
- Large outputs made the UI slow.

## Goals / Non-goals

**Goals**
- Console page registered through the 0004 page contract (id `console`, rail item "Console").
- Shell + log split, resizable and stackable (ADR-0061); virtualised views (ADR-0045).
- Bottom Search | Filter bar with level chips, like the old app (ADR-0056, matching per ADR-0046); Search-only bar
  for the shell.
- Follow / Pause, clear, copy, save view, open log file / folder.
- Command history with Ctrl+R, shared per (profile, target) and merged across processes (ADR-0058).
- Batch file runner.
- Reset / Reconnect from the toolbar.
- Dropped-lines and warning banners, empty states.
- EN + CS, accessibility per the design system § 6.

**Non-goals**
- Console for the LTE target (as in M1/M2).
- Free text selection across rows (row-based selection per ADR-0045; see Risks).
- Partial text selection within a row (rows only in M3, ADR-0113; may come later if users ask).
- Per-probe history (open question 17).

## User experience

### Console page

- **Toolbar** (left → right):
  - Reset ▾ (Reset / Reset and halt) · Reconnect (F4) · Batch… (Ctrl+B) · Clear ▾ (Shell / Log / Both, Ctrl+K =
    focused view) · Save view… (Ctrl+S);
  - spacer;
  - Follow toggle · Pause (Ctrl+P) · ⋯ (Open log file, Open log folder, Show timestamps, Wrap lines, Swap panes,
    Stacked/side-by-side).

  At 960 px in Czech, actions that do not fit move into ⋯ (ADR-0061).
- **Split:** Shell (left) | Log (right), resizable. The ratio is remembered (`console/splitRatio`). Below 1000 px
  page width the split stacks vertically, shell on top.
- **Shell view:** echoed commands `> cmd` in the accent colour, then the device output. The input row
  `> [_____] [Send]` sits under the shell view.
  - Enter sends; Up/Down walk the history; Ctrl+R opens the history popup.
  - While disconnected or while another page is busy, the input is disabled with a placeholder: "Not connected" /
    "Flashing — the console resumes automatically".
- **Log view:** columns time (optional), level tag (D I W E, coloured) and text.
- **Search bar at the bottom of the log** (ADR-0056, like the old app), from left to right:
  - a **Search | Filter** switch;
  - the field with Aa and .\* toggles;
  - in Search mode: "3 / 128", ↑ ↓, ×; in Filter mode: "Showing X of Y lines", Clear filter;
  - the level chips D I W E.

  Ctrl+F opens it in Search mode and Ctrl+L in Filter mode. An active filter stays visible as a chip while the bar is
  in Search mode. Enter / Shift+Enter go to the next / previous match. Esc closes the bar; a second Esc clears the
  filter. Switching modes keeps both queries. Searching never pauses the stream; Follow is turned off only when the
  user moves to a match.
- **Shell search:** Ctrl+F in the shell shows a Search-only bar directly above the command input. Esc hides it.
- **Selection and copy:** click / Shift+click / Ctrl+A select rows; Ctrl+C copies the rows (with timestamps when they
  are shown). Whole rows only (ADR-0113).
- **Banners:**
  - "312 lines were dropped because the device sent data faster than it could be displayed." [Dismiss]
  - Session warnings are shown by the **shell's banner** above every page (0004 `PageHost`; amended 2026-10-16, user
    decision), not repeated by the console:
    - `probe.firmware_outdated` warning: "The J-Link firmware is outdated." [Details] (the update button comes in M4).
    - Console file write failure (`io.write_failed`): "The console log file could not be written; logging to file
      is off for this session."
- **Batch** opens a file dialog (`.txt`, any).
  - It sends one non-empty, non-`#` line every 200 ms; the progress chip shows "Batch 12 / 40 [Stop]".
  - It stops on disconnect.
  - Limits: 1 MiB file and 10 000 commands; above that, an error.
- **Empty states:**
  - Not connected and no lines: "Not connected. Connect to see the device console." [Connect]
  - Connected and nothing yet: "Waiting for data from the device…"
- **Busy integration:** while another page (Flash) runs an operation on the session, the input is disabled. The
  shell adds the marker rows ("— paused: Flashing… —" on `setBusy`, "— resumed —" when the busy page clears;
  ADR-0063, 0004 review S5); the console only shows them. The backend reports no disconnect for the paused RTT
  (ADR-0034). The console never calls `setBusy` itself. The batch runner and Reset/Reconnect are disabled while the
  tab is busy.

### Shortcuts (ADR-0051, registered in scope `console`)

Ctrl+F, Ctrl+L, Ctrl+K, Ctrl+S, Ctrl+P, Ctrl+B, Ctrl+R (in the input), Ctrl+A / Ctrl+C (in a view), registered with
`QT_TRANSLATE_NOOP("Shortcuts", "…")` descriptions (the F1 overlay translates them in the "Shortcuts" context).
F4 (reconnect) is a global shortcut of the shell (0004): the toolbar's Reconnect shows it in its tooltip but does not
register it.

### Texts

The English source texts are as above. Czech is translated in Qt Linguist as part of done (ADR-0027).

## CLI

**None.** The console page maps to `hio chester app console` (M1). The CLI already has its own batch and history
(ADR-0033). Find/filter, follow and pause are view concerns only.

## Design

### Modules

```
app/gui/
  src/viewmodels/
    console_view_model.*            shell + log channels, input, history, batch; registered as page "console"
    console_line_model.*            QAbstractListModel over ConsoleLog (ADR-0045)
    find_filter_engine.*            time-sliced search/filter over a channel (pure logic, unit-tested)
    history_store.*                 one CommandHistory per (profile, target), shared by tabs, owned by AppViewModel
  src/workers/
    batch_runner.*                  QTimer-driven, bounded, owned by ConsoleViewModel
  qml/
    pages/ConsolePage.qml
    console/ ConsoleView.qml ConsoleRow.qml ShellInput.qml HistoryPopup.qml ConsoleSearchBar.qml Banners.qml
    settings/ConsoleSection.qml     Settings section, registered via PageRegistry::addSettingsSection (0004)
  tests/ tst_console*.qml  viewmodels/console_*_test.cpp find_filter_engine_test.cpp
```

### ViewModel interfaces (sketch, `hio::gui`)

```cpp
class ConsoleViewModel : public QObject {      // created by the "console" PageDescriptor; parent = DeviceTabViewModel
    Q_PROPERTY(ConsoleLineModel* shell ...) Q_PROPERTY(ConsoleLineModel* log ...)
    Q_PROPERTY(bool inputEnabled ...) Q_PROPERTY(QString inputDisabledReason ...)
    Q_PROPERTY(quint64 droppedLines ...) Q_PROPERTY(hio::gui::ErrorInfo warning ...)
    Q_PROPERTY(bool batchRunning ...) Q_PROPERTY(int batchDone ...) Q_PROPERTY(int batchTotal ...)
    Q_INVOKABLE bool send(const QString& line);  // false: not sent, the input keeps the text (review K9)
    Q_INVOKABLE QString historyPrevious(const QString& current); Q_INVOKABLE QString historyNext(const QString& current);
    Q_INVOKABLE QStringList historySearch(const QString& text, int max = 50);
    Q_INVOKABLE void runBatch(const QUrl& file); Q_INVOKABLE void stopBatch();
    Q_INVOKABLE void clear(int channelMask); Q_INVOKABLE void saveView(int channel, const QUrl& file);
    Q_INVOKABLE void openLogFile(); Q_INVOKABLE void openLogFolder();
    Q_INVOKABLE void reset(bool halt);          // M2: session->reset(tab.consoleTargetId(), Go|Halt) (ADR-0062)
    Q_INVOKABLE void reconnect();
};

class ConsoleLineModel : public QAbstractListModel {   // one channel, filtered view
    enum Roles { TextRole = Qt::UserRole + 1, LevelRole, DirectionRole, TimeRole, SeqRole, MatchesRole, SelectedRole,
                 CurrentMatchRole, MatchRangesRole /* [{start, length}] of the shown text */, LevelTagRole };
    Q_PROPERTY(bool following ...) Q_PROPERTY(bool paused ...) Q_PROPERTY(int newLinesWhileNotFollowing ...)
    Q_PROPERTY(int totalLines ...) Q_PROPERTY(int visibleLines ...)
    // filter
    Q_PROPERTY(QString filterText ...) Q_PROPERTY(bool filterRegex ...) Q_PROPERTY(bool filterCase ...)
    Q_PROPERTY(int levelMask ...) Q_PROPERTY(QString filterError ...)
    // find
    Q_PROPERTY(QString findText ...) Q_PROPERTY(bool findRegex ...) Q_PROPERTY(bool findCase ...)
    Q_PROPERTY(int matchCount ...) Q_PROPERTY(int currentMatch ...) Q_PROPERTY(int currentMatchRow ...)
    Q_PROPERTY(QString findError ...) Q_PROPERTY(bool searching ...)
    Q_INVOKABLE void findNext(); Q_INVOKABLE void findPrevious();   // emit currentMatchMoved(row): the view scrolls
                                                                    // there (appends never move it; review K2)
    Q_INVOKABLE void select(int row, bool extend); Q_INVOKABLE void selectAll(); Q_INVOKABLE void copySelection(bool withTime);
};
```

### Data flow and threading

- **GUI main thread:** `ConsoleViewModel`, `ConsoleLineModel`, `FindFilterEngine`, `HistoryStore`, `BatchRunner`.
  The tab owns one `ConsoleLog` for all its sessions (ADR-0063; 0004 contract `consoleLog()`); the models index it.
- **Session change:** on `DeviceTabViewModel::sessionChanged` (also emitted on tab close / quit, before the old
  session is deleted) the view model disconnects from the old session and connects to the new one. It keeps the
  session only as `QPointer<DeviceSession>` (or re-reads `tab.session()`). A send or Reset without a session calls
  `ensureSession()` (no parameters: the console target with RTT). The models are not reset: earlier lines stay until
  Clear or eviction.
- **Markers:** the shell (`DeviceTabViewModel`) appends all marker rows to the tab's `ConsoleLog` ("— connected —",
  "— disconnected —", "— disconnected: <cause> —", "— paused: <busy text> —", "— resumed —"; 0004 review S5). The
  console view model never calls `appendMarker`; it shows the marker rows like any other line. Marker rows are styled
  as muted separators and are excluded from find, filter and the level chips.
- **`linesReceived` batches** are appended by `ConsoleLineModel`, which coalesces UI updates to one per 50 ms
  (QTimer). Eviction from `ConsoleLog` maps to row removal.
- **Pause** freezes the view: new lines are counted, not shown. On resume they are appended up to the model bound.
- **Find/filter** run time-sliced on the GUI thread: 5 000 lines per slice, re-queued with
  `QTimer::singleShot(0)`. A new query cancels the old one by bumping its generation counter. An invalid regex
  sets `findError` / `filterError` and keeps the last valid filter result / search matches (ADR-0046; amended
  2026-10-16, user decision). Filter, search and the highlight ranges all use the text the row shows ("> " before
  a sent command, cut at 4 KiB). Rows are plain text; the `matchRanges` role gives the character ranges of the
  matches, drawn as rectangles behind the text (ADR-0045; never rich text from device data).
- **Command history:**
  - `HistoryStore` (owned by `AppViewModel`) holds one `CommandHistory` per (profile, target), shared by all tabs
    with that pair.
  - The M1 file path and limits are unchanged. Saves are locked and merged across processes (ADR-0058, applied in
    0002).
- **Batch runner:** a `QTimer` (200 ms) on the GUI thread calls `session.sendLine`. Queue-full
  (`internal.queue_full`) pauses the batch and retries on the next tick. The file is read and validated
  (size, line count) before the first send.
- **Open log file / folder:** uses the session's current console file path, `DeviceSession::consoleFile()` (0002
  amendment, 2026-10-16), or the tab's last one while no session is open; `QDesktopServices::openUrl` lives in
  `app/gui` only.

### Error cases (GUI-originated)

| Situation | Code | Message / cause / suggestion | Wizard |
|---|---|---|---|
| Batch file unreadable | `io.read_failed` | "The batch file could not be read." / missing or no permission / "Choose another file." | — |
| Batch file > 1 MiB or > 10 000 commands | `usage.invalid_argument` | "The batch file is too large (max. %1 commands)." / — / "Split it into smaller files." | — |
| Save view not writable | `io.write_failed` | "The file could not be saved." / folder not writable / "Choose another folder." | — |
| Invalid regex | (inline, not an error) | the field shows "Invalid regular expression: %1" | — |

### Settings keys added (registered via `SettingsStore::registerKeys`, ADR-0025)

| Key | Type | Default |
|---|---|---|
| `console/fontSize` | int 8–24 | 10 |
| `console/showTimestamps` | bool | `false` |
| `console/wrapLines` | bool | `false` |
| `console/splitRatio` | int 20–80 (%) | 50 |
| `console/stacked` | bool | `false` |
| `console/logToFile` | bool | `true` |

This plan contributes the **Console** section of the Settings page (`ConsoleSection.qml`, registered with
`addSettingsSection`): font size (8–24), show timestamps, wrap long lines, save console to log files, log folder
[Open], and the retention info "Last 50 files, max. 200 MB". The page itself is 0006's. The console reacts to
`SettingsStore::valueChanged`.

### Limits

| Item | Limit |
|---|---|
| Lines per channel | 100 000 (backend); the model mirrors it |
| Find/filter slice | 5 000 lines |
| Model UI updates | ≤ 20/s |
| Copy | ≤ 100 000 lines / 16 MiB |
| Displayed line | ≤ 4 KiB |
| Batch file | ≤ 1 MiB / 10 000 commands, 200 ms pacing |
| History popup | 50 results |

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Console rendering | ListView + C++ model / TextEdit | Virtualised ListView | ADR-0045 |
| Find / filter | find bar + filter / combined field | Find bar + separate filter (matching, Follow) | ADR-0046 |
| Search position (review) | bottom find, filter top / one bottom bar with Search \| Filter / both rows at bottom | One bottom bar like the old app | ADR-0056 |
| History across processes | merged under a file lock / last writer wins | Merged under a lock (backend, 0002) | ADR-0058 |
| Split stacking | < 1000 px / never | Stacks < 1000 px, user override | ADR-0061 |
| Reset API | operation with target / keep M1 `reset(bool)` | Operation with target (from M2) | ADR-0062 |
| Lines across Disconnect/Connect | tab owns the log / GUI copy / clear | Tab owns the `ConsoleLog`; marker rows | ADR-0063 |
| Partial selection within a row (review round 1) | rows only / selectable overlay for the focused row | Rows only for M3; later if users ask (2026-10-16) | ADR-0113 |

## Implementation steps

- [x] C0 Prerequisite check: 0004 is Done (page contract, `SettingsStore`, `ShortcutRegistry`); amendment applied
- [x] C1 `ConsoleLineModel` over the tab's `ConsoleLog` (coalesced append, eviction, follow / new-lines counter,
  pause, session change, markers) + GTest
- [x] C2 `FindFilterEngine` (plain/regex/case, level mask, time slicing, cancellation) + GTest on 100 000 lines
- [x] C3 ConsoleView, ConsoleRow, selection + copy, ConsoleSearchBar (log: Search | Filter + levels; shell: Search
  only), save view
- [x] C4 ShellInput, `HistoryStore` (shared per profile/target), HistoryPopup, BatchRunner, banners (dropped,
  warnings), empty states
- [x] C5 Toolbar (Reset ▾, Reconnect, Clear ▾, ⋯ menu with overflow), split / stacked layout, page registration,
  shortcuts (scope `console`), busy integration
- [x] C5b Console settings section (`ConsoleSection.qml`) + keys registered
- [x] C6 Accessibility (row announcements throttled), Czech translation, `.ts` fresh; screenshots of the console in
  both themes and both languages

## Tests and verification

- **ViewModel unit tests (GTest, fake `DeviceSession` via the fake J-Link library):**
  - console append / eviction / follow counters, pause and resume;
  - find/filter correctness and time slicing (100 000 lines, each slice < 8 ms in Release);
  - invalid regex;
  - copy limits;
  - batch pacing, queue-full, stop on disconnect, file limits;
  - history: shared between two tabs with the same profile/target, Up/Down, search;
  - session change: lines of the previous session kept, marker rows, no handlers left on the old session.
- **Qt Quick Tests:**
  - ConsoleSearchBar keyboard behaviour (Ctrl+F / Ctrl+L, Enter / Shift+Enter, Esc twice, mode switch keeps queries);
  - toolbar overflow at 960 px in Czech, stacked split < 1000 px (geometry assertions);
  - console rows in light and dark.
- **`qmllint`** clean; no logic in QML.
- **Performance (fake library, 5 000 lines/s into both channels for 10 min):**
  - the GUI stays responsive (< 16 ms frames on the reference laptop);
  - RSS stays bounded (< 400 MB);
  - the dropped-lines banner appears when overflow is forced.
- **Sanitizers:** ASan+UBSan and TSan run the ViewModel tests and the scripted console smoke test (offscreen).
- **HIL (CHESTER + J-Link):** console in two tabs while one streams heavily; device reset from the shell →
  re-attach; batch of 40 commands; history survives a restart and a parallel `hio chester app console`.
- **Manual on Linux, Windows, macOS (by the user, before Done):** the console mockup scenarios, keyboard-only
  walkthrough, screen reader spot check, live language switching.

## Risks and open points

- **Row-based selection** (ADR-0045, ADR-0113: rows only, also within a row) differs from free text selection. If
  users complain, a selectable text overlay for a row range can be added later (new plan and ADR).
- **Time-sliced search on the GUI thread** is bounded per slice but adds load during heavy streaming. Moving it to a
  worker over a snapshot is the fallback (needs an ADR).
- **Shared history per profile/target** across tabs and processes: commands from two devices of the same type are
  interleaved. This is accepted for M3; per-probe history is open question 17.

## Implementation notes

Recorded by the implementing agent (2026-10-16, branch `v2/m3-gui`, on top of 55e69f0). **All minor local choices
below were confirmed by the user on 2026-10-16**; the open points were decided the same day (see "Open points for
the user" and "Review round 1 fixes"). Bullets changed by the review round 1 fixes say so.

### Facts that postdate the plan

- **C0:** 0004 is implemented (all steps ticked, both review rounds fixed); its status stays *In progress* until its
  HIL and manual checks. The page contract (`PageRegistry`, `ensureSession()` without parameters, `sessionChanged`
  on tab close / quit, the shell's markers, F4 global) is used as amended.
- **M2 has landed:** Reset ▾ uses `DeviceSession::reset(tab.consoleTargetId(), ResetMode::Go | Halt)` (ADR-0062).
  Without a session, Reset calls `ensureSession()` first (the operation is queued behind the connect). Success shows
  a toast ("The device was reset." / "…reset and halted."), a failure the console's ErrorPanel. Only the console's
  own operation id is handled (`operationFinished` of other pages is ignored).
- **ADR-0090 / 0110 / 0111** (automatic speed, attach deadline, Advanced popover) are shell / backend matters; the
  console shows the session's states and needs no fixed connect timeout. **ADR-0109:** not relevant here.
- **ADR-0112:** the new icons are SVG text (no LFS); nothing binary was added.
- **`probe.io_failed`, `TargetProfile::displayName`:** no console-specific handling needed (errors arrive as
  `hio::Error` and are shown as they are; the tab title is the shell's).
- **H6** (stale bootloader RTT control block right after a reset): seen once in five HIL runs (see HIL below).
- **Plan 0007** (`ensureSession(bool connectTarget = true)`): the console calls `ensureSession()` with no argument,
  which stays valid with a defaulted parameter.

### Design as built

- **Modules:** `src/viewmodels/{console_view_model,console_line_model,find_filter_engine,history_store,
  console_settings}.*`, `src/workers/batch_runner.*`, `src/pages/console_page.*` (registration, called from
  `registerBuiltInPages()` instead of the console placeholder), `qml/pages/ConsolePage.qml`,
  `qml/console/{ConsoleView,ConsoleRow,ShellInput,HistoryPopup,ConsoleSearchBar,Banners}.qml`,
  `qml/settings/ConsoleSection.qml`. `AppViewModel` owns the `HistoryStore` (`historyStore()`, files in the
  registry's console directory = the CLI's `history-<profile>-<target>.txt`).
- **`ConsoleLineModel` indexes the tab's `ConsoleLog` without copying the text** (ADR-0063 option 1): it keeps only
  the sequence numbers of its rows (8 bytes per row, ≤ 100 000 per channel) and reads a shown line with
  `ConsoleLog::linesSince(seq − 1, channel, 1)` (binary search, one-line cache for the roles of one delegate). The
  public `ConsoleLog` API is unchanged (no `libs/*` change). Eviction and Clear are found by comparing the rows with
  the oldest line still in the log; the rows are removed from the top in the same update.
- **Updates:** `linesReceived` starts a 50 ms single-shot timer (≤ 20 model updates per second); a 250 ms poll of
  `ConsoleLog::lastSeq()` catches lines appended without a session signal (the shell's markers).
- **Find / filter:** `TextMatcher` (plain, case-insensitive by default; `.*` = `QRegularExpression`), `LineFilter`
  (text + level chips) and `LineScan` (5 000 lines per event-loop turn, generation counter, the end re-read before
  each slice so that lines arriving during a scan are covered). Both the filter rows and the search matches are
  rebuilt by one scan; new lines are matched as they arrive. "3 / 128" counts matching **rows**, not occurrences.
  *Changed by review round 1 (K4/K5):* rows are plain text only; the `matchRanges` role gives the character ranges of
  the matches on the shown text, which the row draws as rectangles behind a read-only plain-text `TextEdit`
  (`positionToRectangle`, also for wrapped lines); filter and search match the shown text ("> " prefix, 4 KiB cut);
  an invalid regular expression keeps the last valid result (ADR-0046). The rich-text `HighlightedRole` and the
  model's highlight colours are gone (the row uses `Theme.findMatch` / `Theme.findCurrent`).
- **Markers** are always shown (exempt from the filter and the level chips) and never match a search. Lines without
  a level tag are not affected by the level chips.
- **Esc:** the log's bar is always visible (ADR-0056), so "Esc closes the bar" is implemented as: the first Esc
  clears the search query and returns the focus to the log; the second Esc (in the log) clears the filter. The
  shell's Search-only bar is hidden by Esc (and its query cleared).
- **Shortcuts (scope `console`, `QT_TRANSLATE_NOOP("Shortcuts", …)`):** the plan's list plus the console entries of
  the ADR-0051 map that the plan did not repeat: F3 / Shift+F3, End (follow), Ctrl+E (focus input), Ctrl+= / Ctrl+-
  / Ctrl+0 (font size). End, Ctrl+A, Ctrl+C, Ctrl+R and `n` / Shift+N are handled inside the view / input and
  registered for the F1 overlay and tooltips only. F4 is not registered (the shell's).
- **Follow / Pause** act on both views; scrolling a view up (or moving to a match) turns that view's Follow off; the
  "▼ N new lines" pill or End turns it on. Pause is shown as a banner with [Resume] (ADR-0046).
- **Toolbar:** below 1100 px window width the actions are icons only (design system § 5 Toolbar); with that every
  action fits at 960 px in Czech (geometry test), so no action has to move into ⋯. ⋯ holds Open log file / folder,
  Show timestamps, Wrap lines, Swap panes, Stacked layout.
- **Split:** own splitter (ratio of the first pane, 20–80 %, saved to `console/splitRatio` on release, arrow keys
  ±5 % on the focused handle); stacked when the page is narrower than 1000 px or `console/stacked` is set; Swap
  panes is per tab and not saved (the plan defines no key for it).
- **Console settings:** `ConsoleSettings` (QML singleton, child of `AppViewModel`) registers the `console/*` keys,
  writes through `SettingsStore` and is used by the console pages and `ConsoleSection.qml` (registered with
  `addSettingsSection`, rendered by 0006's Settings page; a Qt Quick test instantiates it).
- **Errors:** GUI-originated errors (batch file, save view, reset, send) are shown in the console's own ErrorPanel
  (`ConsoleViewModel::error`; primary action Reconnect / Open Settings where the category says so, otherwise
  Dismiss). Additional GUI-originated texts: "The batch file contains no commands.", "…a command longer than 4096
  bytes.", "The batch file is too large (max. 1 MiB).", and on a disconnect during a batch `target.not_connected`
  "The batch stopped after %1 of %2 commands." / "the device disconnected." / "Reconnect and run the batch file
  again.". Batch commands are not added to the command history.
- **Input states:** "Not connected", "Connecting…", "<busy text> — the console resumes automatically", and for a
  target without an RTT console (CHESTER `lte`, a non-goal) "This target has no console".
- **Open log file** *(changed by review round 1, K10)* opens `DeviceSession::consoleFile()` (0002 amendment), or the
  tab's last console file while no session is open; without one a toast says "There is no console log file for this
  device yet." **Open log folder** creates and opens the directory. Both go through `ConsoleSettings::openUrl`
  (`QDesktopServices`, replaceable in tests).
- **Accessibility:** rows expose their text; new shell output is announced (`Accessible.announce`) at most once per
  2 s and only while the page is current; every icon button has a name and a tooltip with its shortcut; the
  splitter is focusable; the level chips and toggles are named checkable controls.
- **Icons (ADR-0060):** reset, reconnect, batch, clear, save, follow, pause, resume, up, down, stop — from the
  pinned Material Symbols commit, listed in `assets/icons/README.md`.
- **Tests:** `hio_gui_tests` (+40: `TextMatcher`, `LineFilter`, `LineScan`, `ConsoleLineModel`,
  `ConsoleViewModel`, `BatchRunner`), `tst_gui.tst_console` (14 Qt Quick tests), `tst_gui.tst_console_screenshots`
  (with `HIO_SCREENSHOT_DIR`), `tst_gui.tst_console_perf` (with `HIO_CONSOLE_PERF_SECONDS`) and the HIL executable
  `hio_gui_hil_tests` (label `hil`, `HIO_HIL=1` + `HIO_HIL_SN`). The fake J-Link library gained a test-only
  `"stream"` generator (N lines/s per up buffer) for the performance test.

### Verification (2026-10-16)

- **ctest:** linux-debug 700/700, linux-asan 700/700, linux-tsan 688/688 passed (13 skipped each: HIL, corpus and
  the opt-in screenshot / performance runs); 0 sanitizer reports. `linux-tidy` zero findings; `all_qmllint` clean;
  `check_translations` 0 unfinished Czech entries, catalogs fresh; clang-format-21 clean; `tools/check-lfs.sh
  --untracked` passed.
- **Slices (Release, 100 000 lines, filter regex + level chips + search):** median 3.9–4.1 ms per 5 000-line slice
  (the slowest 5.8–12 ms on this machine under a load average of ~20 from parallel builds; the unit test checks the
  median < 8 ms in optimized builds only).
- **Performance (Release, fake library, 5 000 lines/s into each channel, offscreen, 600 s):** 6 004 730 lines
  (10 000 lines/s), both views at their 100 000-row bound, peak RSS 127 MiB (start 92 MiB), 51.7 frames/s, 0 lines
  dropped; GUI-thread stalls (lateness of a 16 ms timer) p99 93 ms, max 335 ms, measured while other builds kept
  the machine at a load average of 17–24 (paused views under the same load: p99 30 ms). A forced 3 s GUI stall
  dropped ~20 000 lines and showed the banner. `refresh()` of both models: ~0.9 ms on average. The < 16 ms frame
  criterion needs the reference laptop (manual check).
- **HIL (CHESTER + J-Link, `hio_gui_hil_tests`, 5 runs):** connect 1.6–1.9 s; `help` and `app config show`
  answered; a batch of 40 `help` commands in 7.7–7.9 s with all 40 echoes; Reset (operation, ~0.1 s) followed by
  re-attach and an answering shell in 4 of 5 runs — in one run the shell did not answer within 10 s after the reset
  (consistent with H6, not investigated here); the history survived re-creating the application. Not run: two tabs
  (one probe only), a parallel `hio chester app console`.
- **Screenshots:** `tst_console_screenshots.qml` (light/dark × EN/CS): not connected, waiting for data, normal,
  search, filter, search with the filter chip, invalid regex, banners + error panel, new-lines pill, history
  popup, stacked at 990 px, 960 × 600.

### Open points for the user (decided 2026-10-16)

1. **Session warnings on the console page.** The shell (0004 `PageHost`) already shows every session warning
   (`probe.firmware_outdated`, the console file's `io.write_failed`, …) as a banner above every page, with the
   backend's text. The console therefore does not repeat them, and the plan's [Details] button and the console
   text "The console log file could not be written; logging to file is off for this session." are not shown.
   Options: (a) accept the shell banner as is; (b) give the shell banner a [Details] expander and map the console
   file's `io.write_failed` warning to the plan's text in `ConnectionViewModel::setWarning` (0004 code);
   (c) move session warnings from `PageHost` to the pages.
2. **Open log file.** `DeviceSession` does not expose its current console file, so the console opens the newest
   `<profile>-<target>-*-<sn>*.log` in the console directory (normally the session's file). Options: (a) accept;
   (b) a 0002 amendment: `DeviceSession::consoleFile()` (the sink's current file, reported by the worker).
3. **Partial text selection in a row** (ADR-0045: "a selectable TextEdit delegate on focus") is not implemented;
   the plan's UX lists row selection only. Options: (a) row selection only for M3 and amend ADR-0045; (b) add a
   selectable overlay for the focused row.
4. **Frame time:** the < 16 ms criterion could not be shown on this (loaded, offscreen) machine; please run
   `HIO_CONSOLE_PERF_SECONDS=600 ctest --preset linux-release -R tst_console_perf` on the reference laptop (or
   check it manually), or tell whether the stall numbers above are acceptable.
5. **Not persisted:** Swap panes (no key in the plan's table); `Ctrl+=` only (no `Ctrl++` alternative).

Decisions (Matej Strnad, 2026-10-16): **1 → (b)** the shell banner gets [Details] and the console file's
`io.write_failed` text (0004 code, amendment in 0004); the console page does not repeat session warnings.
**2 → (b)** `DeviceSession::consoleFile()` (0002 amendment). **3 → (a)** rows only for M3, ADR-0113. 4 and 5: the
small choices were accepted as they are; the frame-time check stays a manual check on the reference laptop.

### Review round 1 fixes (2026-10-16)

Implemented by the implementing agent after the user's decisions; statuses in the findings table below.

- **K1** `ConsoleLineModel::setPaused(false)` moves the rows and matches a running scan had put aside into the scan's
  rows (they precede everything the scan reads afterwards), and `finishScan()` does the same when the view is not
  paused; the rows and matches stay sorted. Test `ConsoleLineModelTest.ResumeDuringAScanKeepsTheRowsSorted` (fails
  without the fix).
- **K2** new signal `ConsoleLineModel::currentMatchMoved(int row)`, emitted only by find next / previous; the view
  scrolls on it (no longer on `matchesChanged`). Tests `ConsoleLineModelTest.OnlyFindNextMovesTheView`,
  `tst_console::test_newMatchesDoNotMoveTheView` (fails on the old QML).
- **K3** Follow is cleared on `movementStarted` (wheel, drag, flick) and when the scroll bar is pressed; the view
  only positions at the end while following and not moving / dragged / scroll bar pressed; at the end of a movement
  or on release Follow is on again when the view is at the end. Test `tst_console::test_scrollWhileStreaming`
  (wheel while lines stream, scroll bar held; fails on the old QML).
- **K4 / K5** see the find / filter bullet above. ADR-0046's "keeps the last valid result" is implemented for both
  the filter (last valid text query + the current level chips) and the search (last valid matches); a query that
  becomes valid again unchanged does not rescan. Tests `ConsoleLineModelTest.InvalidRegexKeepsTheLastValidResult`,
  `.MatchRangesAreRangesOfTheShownText`, `tst_console::test_searchHighlightRectangles` (incl. a wrapped match).
- **K6** `newLinesWhileNotFollowing` saturates at `INT_MAX` (by code; a test would need 2³¹ lines).
- **K7** `data()` of a row whose line left the log since the last refresh returns a value of the role's type
  (empty string, 0, empty list). Tests `ConsoleLineModelTest.EvictedRowsGiveTypedEmptyValues` and
  `.ModelContractUnderEvictionClearAndRebuild` (`QAbstractItemModelTester`, fatal mode, over appends, evictions,
  scan rebuilds, pause / resume, a clear found by refresh and `resetRows()`).
- **K8** a configuration change resets the whole history walk (index, revision, draft); the "not navigating"
  revision is a sentinel, which also fixes the first Up after a restart with two or more stored commands (it
  returned the oldest entry). Test `ConsoleViewModelTest.HistoryWalkStartsAgainAfterAConfigurationChange`.
- **K9** `ConsoleViewModel::send()` returns whether the command was sent; the input is cleared only then. A batch
  stopped because another page became busy shows "The batch stopped after %1 of %2 commands." (code
  `internal.busy`, cause "another operation started on the device.", suggestion "Run the batch file again when it
  has finished."). Tests `ConsoleViewModelTest.SendReportsWhetherTheCommandWasSent`, `.BatchStoppedByBusySaysSo`,
  `tst_console::test_unsentCommandStaysInTheInput`.
- **K10 / open point 2** `DeviceSession::consoleFile()` (libs/device): the worker reports the file it writes with a
  `ConsoleFileChanged` event (opened, continued in a `.partN` file, or empty after a write failure), applied on the
  owner thread like `ProbeIdentified`. Tests `Session.ConsoleFileIsReportedOnceOpened`,
  `.DefaultConsoleFileIsReported`, `.NoConsoleFileIsEmpty`, `.ConsoleFileIsEmptyAfterAWriteFailure` (`/dev/full`),
  `ConsoleViewModelTest.OpensTheSessionsConsoleFile`. The `.partN` continuation is not tested (it needs 10 MiB).
- **K11** the split handle has a 2 px accent focus ring. Test `tst_console::test_splitHandleFocusRing`.
- **K12** the console view model follows `AppViewModel::languageChanged` (the language manager's signal) instead of
  an application-wide event filter per tab; `ConsoleContext` got `AppViewModel* app`. Test
  `ConsoleViewModelTest.LanguageSwitchUpdatesTheTexts`.
- **Open point 1** (0004 code): `WarningBanner` has an optional `details` (ErrorInfo) with a [Details] toggle
  (cause, suggestion, code + detail, Copy), used by `PageHost` for the session warning;
  `ConnectionViewModel::setWarning` gives `io.write_failed` the console's text. Tests
  `ConnectionTest.ConsoleFileWarningText`, `tst_components::test_warningBannerDetails`.
- **TSan flake** (`tst_gui.tst_shell`, `tst_already_running`): the probe scanner logged through QtQuickTest's
  message handler while the view and its `TestResult` objects were destroyed at the end of a test file. The test
  runner now stops the scanner when the test file has finished (`QQmlEngine::quit`, emitted by `TestCase`), via the
  new `ProbeScanner::stop()` (also used by its destructor). No suppression added.
- **Verification (2026-10-16, after the fixes):** ctest linux-debug 713 (700 passed, 13 skipped), linux-asan 713 (700
  passed, 13 skipped), linux-tsan 701 (688 passed, 13 skipped), 0 sanitizer reports; TSan `tst_gui.*` repeated 10×
  and `tst_gui.tst_shell` + `tst_already_running` 25× without a failure (before the fix: 1 TSan report in 25 runs of
  `tst_shell`); `linux-tidy` zero findings; `all_qmllint` clean; `check_translations` 0 unfinished Czech entries,
  catalogs fresh; clang-format-21 clean; `tools/check-lfs.sh --untracked` passed. Screenshots retaken (search
  highlight, invalid regex keeping the last valid filter, the shell banner with [Details] expanded, the console file
  warning `08b`).
- **New finding — fixed (user decision 2026-10-16):** Down in the command input when not walking the history
  returned the stored draft (empty after a send), replacing typed text with an empty line. Now
  `historyNext(const QString& current)` mirrors `historyPrevious(current)`: it returns `current` when not navigating
  and the draft at the end of a walk; `ShellInput.qml` passes the field text. Tests
  `ConsoleViewModelTest.DownKeepsTheTypedTextWhenNotNavigating`, `tst_console::test_downKeepsTypedText`.

## Open findings (independent review round 1, 2026-10-16)

| ID | Severity | Where | Finding | Suggested fix | Status (2026-10-16) |
|---|---|---|---|---|---|
| K1 | high | `console_line_model.cpp` `setPaused` / `scanSlice` / `finishScan` | Resume while a filter/search scan runs leaves the scanned "pending" rows hidden; the next Pause→Resume appends lower seqs at the end, so `m_rows` / `m_matches` stop being sorted and every `lower_bound` lookup (eviction, rowOf, selection, current match) is wrong (reproduced) | On resume during a scan move `m_scanPending*` into `m_scanRows` / `m_scanMatches` (or restart the scan); unit test | Fixed (resume merges the scan's pending rows/matches; repro test) |
| K2 | medium | `console_line_model.cpp` `matchesChanged`, `ConsoleView.qml` | Every append/eviction touching matches re-centres the view on the current match → the view snaps back while the user scrolls | Separate `currentMatchMoved(row)` signal emitted only by find next/previous | Fixed (`currentMatchMoved`; GTest + Qt Quick test) |
| K3 | medium (plausible) | `ConsoleView.qml` follow handling | Follow is cleared only on `movementEnded`; while streaming, `positionViewAtEnd` pulls a wheel/scrollbar scroll back to the end, so the user cannot scroll up | Clear Follow on movement start / scrollbar press; don't auto-position while moving | Fixed (Follow off on movement start / scroll bar press; Qt Quick test) |
| K4 | medium (rule) | `HighlightedRole`, `ConsoleRow.qml` | Rich text built from device data (escaped) contradicts ADR-0045 "never rich text from device data", not raised; ADR-0046 says an invalid regex keeps the last valid result, code matches nothing | Decide: plain text + highlight rectangles from match ranges (ADR-0045), or a new ADR allowing escaped rich text; reconcile invalid-regex behaviour | Fixed per ADR-0045 (plain text + `matchRanges` rectangles); invalid regex keeps the last valid result (ADR-0046) |
| K5 | low | `console_line_model.cpp` | Match decided on the line text, highlight computed on the display text ("> " prefix, 4 KiB cut) → counted matches without a highlight | Same text for both or offset ranges | Fixed (match, filter and ranges on the shown text) |
| K6 | low | `console_line_model.cpp` | `m_newWhileNotFollowing` can overflow (signed int) on very long runs | Saturate | Fixed (saturating; by code) |
| K7 | low (plausible) | `console_line_model.cpp` `data()` | An evicted row returns an empty QVariant for required string roles → QML warnings / stale text in reused delegates | Return empty strings / 0, or drop evicted rows first | Fixed (typed empty values; tests incl. `QAbstractItemModelTester`) |
| K8 | low | `console_view_model.cpp` | On configuration change only the history pointer is reset; index, revision and draft stay | Reset all navigation state | Fixed (index, revision sentinel, draft; test) |
| K9 | low | `ShellInput.qml`, `console_view_model.cpp` | A failed send (queue full) clears the input and loses the command; a batch stopped by "busy" says nothing | `send` returns success, clear only then; message "The batch stopped after %1 of %2 commands." | Fixed (`send()` → bool; busy stop message; tests) |
| K10 | low | `latestLogFile` | Serial matched as a substring (…123 vs …1234) | Superseded by the user's decision to add `DeviceSession::consoleFile()` | Resolved by `DeviceSession::consoleFile()` (heuristic removed; tests) |
| K11 | low (a11y) | `ConsolePage.qml` split handle | No 2 px accent focus ring (design system § 6) | Add it | Fixed (focus ring; Qt Quick test) |
| K12 | low (perf) | `console_view_model.cpp` | One application-wide event filter per tab just for `LanguageChange` | Use the language manager's retranslate signal | Fixed (`AppViewModel::languageChanged`; test) |

Missing tests: K1, K2, K3, `QAbstractItemModelTester` under eviction/clear/rebuild, K8, batch stopped by busy —
all added (see "Review round 1 fixes" above).
