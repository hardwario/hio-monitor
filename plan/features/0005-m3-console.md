# 0005 — M3 console page: shell + log, search/filter, history, batch

- **Status:** Draft
- **Milestone:** M3 (plan 2 of 3, ADR-0059)
- **Author:** Matej Strnad (with Claude)
- **Related ADRs:** ADR-0003, ADR-0025, ADR-0027, ADR-0031, ADR-0033, ADR-0034, ADR-0045, ADR-0046, ADR-0051,
  ADR-0056, ADR-0058, ADR-0059, ADR-0061, ADR-0062, ADR-0063
- **Depends on:** [0004](0004-m3-gui-shell.md) (shell, page-extension contract), 0002 (M1: `DeviceSession`,
  `ConsoleLog`, `CommandHistory`). It uses the M2 `reset(targetId, ResetMode)` operation (ADR-0062) when 0003 has
  landed; before that, M1's `reset(bool)`.
- **Can run in parallel with:** [0006](0006-m3-flash-settings.md)
- **Design system:** [plan/gui/design-system.md](../gui/design-system.md) · **Mockups:** [plan/gui/mockups/](../gui/mockups/index.html)

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
  are shown).
- **Banners:**
  - "312 lines were dropped because the device sent data faster than it could be displayed." [Dismiss]
  - `probe.firmware_outdated` warning: "The J-Link firmware is outdated." [Details] (the update button comes in M4).
  - Console file write failure (`io.write_failed`): "The console log file could not be written; logging to file is
    off for this session."
- **Batch** opens a file dialog (`.txt`, any).
  - It sends one non-empty, non-`#` line every 200 ms; the progress chip shows "Batch 12 / 40 [Stop]".
  - It stops on disconnect.
  - Limits: 1 MiB file and 10 000 commands; above that, an error.
- **Empty states:**
  - Not connected and no lines: "Not connected. Connect to see the device console." [Connect]
  - Connected and nothing yet: "Waiting for data from the device…"
- **Busy integration:** while another page (Flash) runs an operation on the session, the input is disabled. On the
  tab's `busyChanged` a marker row is added ("— paused: Flashing… —" / "— resumed —"; ADR-0063). The backend reports
  no disconnect for the paused RTT (ADR-0034). The console never calls `setBusy` itself. The batch runner and
  Reset/Reconnect are disabled while the tab is busy.

### Shortcuts (ADR-0051, registered in scope `console`)

Ctrl+F, Ctrl+L, Ctrl+K, Ctrl+S, Ctrl+P, Ctrl+B, F4, Ctrl+R (in the input), Ctrl+A / Ctrl+C (in a view).

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
    Q_INVOKABLE void send(const QString& line);
    Q_INVOKABLE QString historyPrevious(); Q_INVOKABLE QString historyNext();
    Q_INVOKABLE QStringList historySearch(const QString& text, int max = 50);
    Q_INVOKABLE void runBatch(const QUrl& file); Q_INVOKABLE void stopBatch();
    Q_INVOKABLE void clear(int channelMask); Q_INVOKABLE void saveView(int channel, const QUrl& file);
    Q_INVOKABLE void openLogFile(); Q_INVOKABLE void openLogFolder();
    Q_INVOKABLE void reset(bool halt);          // M2: session->reset(tab.consoleTargetId(), Go|Halt) (ADR-0062)
    Q_INVOKABLE void reconnect();
};

class ConsoleLineModel : public QAbstractListModel {   // one channel, filtered view
    enum Roles { TextRole = Qt::UserRole + 1, LevelRole, DirectionRole, TimeRole, SeqRole, MatchesRole, SelectedRole };
    Q_PROPERTY(bool following ...) Q_PROPERTY(bool paused ...) Q_PROPERTY(int newLinesWhileNotFollowing ...)
    Q_PROPERTY(int totalLines ...) Q_PROPERTY(int visibleLines ...)
    // filter
    Q_PROPERTY(QString filterText ...) Q_PROPERTY(bool filterRegex ...) Q_PROPERTY(bool filterCase ...)
    Q_PROPERTY(int levelMask ...) Q_PROPERTY(QString filterError ...)
    // find
    Q_PROPERTY(QString findText ...) Q_PROPERTY(bool findRegex ...) Q_PROPERTY(bool findCase ...)
    Q_PROPERTY(int matchCount ...) Q_PROPERTY(int currentMatch ...) Q_PROPERTY(int currentMatchRow ...)
    Q_PROPERTY(QString findError ...) Q_PROPERTY(bool searching ...)
    Q_INVOKABLE void findNext(); Q_INVOKABLE void findPrevious();
    Q_INVOKABLE void select(int row, bool extend); Q_INVOKABLE void selectAll(); Q_INVOKABLE void copySelection(bool withTime);
};
```

### Data flow and threading

- **GUI main thread:** `ConsoleViewModel`, `ConsoleLineModel`, `FindFilterEngine`, `HistoryStore`, `BatchRunner`.
  The tab owns one `ConsoleLog` for all its sessions (ADR-0063; 0004 contract `consoleLog()`); the models index it.
- **Session change:** on `DeviceTabViewModel::sessionChanged` the view model disconnects from the old session and
  connects to the new one. The models are not reset: earlier lines stay until Clear or eviction.
- **Markers:** on connection changes and on the tab's `busyChanged` the view model calls
  `ConsoleLog::appendMarker` ("— connected —", "— disconnected: target has no power —", "— paused: Flashing… —",
  "— resumed —"). Marker rows are styled as muted separators and are excluded from find, filter and the level chips.
- **`linesReceived` batches** are appended by `ConsoleLineModel`, which coalesces UI updates to one per 50 ms
  (QTimer). Eviction from `ConsoleLog` maps to row removal.
- **Pause** freezes the view: new lines are counted, not shown. On resume they are appended up to the model bound.
- **Find/filter** run time-sliced on the GUI thread: 5 000 lines per slice, re-queued with
  `QTimer::singleShot(0)`. A new query cancels the old one by bumping its generation counter. An invalid regex
  sets `findError` / `filterError` and matches nothing.
- **Command history:**
  - `HistoryStore` (owned by `AppViewModel`) holds one `CommandHistory` per (profile, target), shared by all tabs
    with that pair.
  - The M1 file path and limits are unchanged. Saves are locked and merged across processes (ADR-0058, applied in
    0002).
- **Batch runner:** a `QTimer` (200 ms) on the GUI thread calls `session.sendLine`. Queue-full
  (`internal.queue_full`) pauses the batch and retries on the next tick. The file is read and validated
  (size, line count) before the first send.
- **Open log file / folder:** uses the session's current console file path; `QDesktopServices::openUrl` lives in
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

## Implementation steps

- [ ] C0 Prerequisite check: 0004 is Done (page contract, `SettingsStore`, `ShortcutRegistry`)
- [ ] C1 `ConsoleLineModel` over the tab's `ConsoleLog` (coalesced append, eviction, follow / new-lines counter,
  pause, session change, markers) + GTest
- [ ] C2 `FindFilterEngine` (plain/regex/case, level mask, time slicing, cancellation) + GTest on 100 000 lines
- [ ] C3 ConsoleView, ConsoleRow, selection + copy, ConsoleSearchBar (log: Search | Filter + levels; shell: Search
  only), save view
- [ ] C4 ShellInput, `HistoryStore` (shared per profile/target), HistoryPopup, BatchRunner, banners (dropped,
  warnings), empty states
- [ ] C5 Toolbar (Reset ▾, Reconnect, Clear ▾, ⋯ menu with overflow), split / stacked layout, page registration,
  shortcuts (scope `console`), busy integration
- [ ] C5b Console settings section (`ConsoleSection.qml`) + keys registered
- [ ] C6 Accessibility (row announcements throttled), Czech translation, `.ts` fresh; screenshots of the console in
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

- **Row-based selection** (ADR-0045) differs from free text selection. If users complain, a selectable text overlay for
  a row range can be added later.
- **Time-sliced search on the GUI thread** is bounded per slice but adds load during heavy streaming. Moving it to a
  worker over a snapshot is the fallback (needs an ADR).
- **Shared history per profile/target** across tabs and processes: commands from two devices of the same type are
  interleaved. This is accepted for M3; per-probe history is open question 17.
