// The console page (plan 0005): search bar keys (Ctrl+F / Ctrl+L, Enter / Shift+Enter, Esc twice,
// the mode switch keeps both queries, the shell's Search-only bar), rows in light and dark, the
// toolbar at 960 px in Czech, the stacked split below 1000 px, banners and the empty state; review
// round 1: plain-text rows with highlight rectangles (K4/K5), the view is not re-centred on the
// current match by new lines (K2), scrolling while lines stream stops following (K3), the split
// handle's focus ring (K11), a command that was not sent stays in the input (K9); Down keeps the
// typed text when not walking the history.
// Lines are added to the tab's log by testSupport (no device needed).
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    function findAll(parent, name) {
        let result = [];
        const children = parent && parent.children ? parent.children : [];
        for (let i = 0; i < children.length; ++i) {
            if (children[i].objectName === name)
                result.push(children[i]);
            result = result.concat(findAll(children[i], name));
        }
        return result;
    }

    function effectivelyVisible(item) {
        for (let current = item; current; current = current.parent) {
            if (!current.visible)
                return false;
        }
        return true;
    }

    function shown(name) {
        return findAll(window.contentItem, name).filter(effectivelyVisible);
    }

    function one(name) {
        const items = shown(name);
        verify(items.length === 1, name + ": " + items.length);
        return items[0];
    }

    function rect(item) {
        const point = item.mapToItem(window.contentItem, 0, 0);
        return { x: point.x, y: point.y, w: item.width, h: item.height, name: item.objectName };
    }

    function checkInside(items) {
        for (const item of items) {
            const r = rect(item);
            verify(r.x >= -0.5 && r.x + r.w <= window.width + 0.5, `${r.name} is clipped: x=${r.x} w=${r.w}`);
            verify(r.y >= -0.5 && r.y + r.h <= window.height + 0.5, `${r.name} is clipped: y=${r.y} h=${r.h}`);
        }
    }

    function checkNoOverlap(items) {
        for (let i = 0; i < items.length; ++i) {
            for (let j = i + 1; j < items.length; ++j) {
                const a = rect(items[i]);
                const b = rect(items[j]);
                const overlap = a.x < b.x + b.w - 0.5 && b.x < a.x + a.w - 0.5
                              && a.y < b.y + b.h - 0.5 && b.y < a.y + a.h - 0.5;
                verify(!overlap, `${a.name} overlaps ${b.name}`);
            }
        }
    }

    function vm() {
        return testSupport.consoleViewModel(0);
    }

    function openConsole() {
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.showPage("console");
        tryVerify(() => shown("consolePage").length === 1, 5000);
        tryVerify(() => shown("consoleToolbar").length === 1, 5000);
    }

    function addLogLines() {
        testSupport.appendConsoleLines(0, 1, 1, "<dbg> therm: 23.8 C", 1, 0);
        testSupport.appendConsoleLines(0, 1, 1, "<inf> ctr_cloud: Uplink started", 2, 0);
        testSupport.appendConsoleLines(0, 1, 1, "— disconnected —", 0, 2);
        testSupport.appendConsoleLines(0, 1, 1, "<wrn> ctr_lte: RSRP low", 3, 0);
        testSupport.appendConsoleLines(0, 1, 1, "<err> ctr_cloud: Uplink timeout", 4, 0);
        testSupport.appendConsoleLines(0, 0, 1, "uart:~$ ", 0, 0);
        testSupport.appendConsoleLines(0, 0, 1, "version", 0, 1);
    }

    function initTestCase() {
        AppViewModel.setLanguage("en");
        testSupport.resetTabs();
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        testSupport.resetTabs();
        AppViewModel.setLanguage("en");
        AppViewModel.theme = "system";
        window.destroy();
        wait(50); // the deferred delete runs before the engine goes away
    }

    function init() {
        testSupport.resetTabs();
        AppViewModel.setLanguage("en");
        window.width = 1280;
        window.height = 800;
        openConsole();
        vm().clear(3);
        vm().log.clearFilter();
        vm().log.findText = "";
        vm().shell.findText = "";
        vm().paused = false;
        vm().following = true;
    }

    function test_notConnectedEmptyState() {
        verify(vm().showConnectState);
        one("notConnectedState");
        addLogLines();
        tryVerify(() => shown("notConnectedState").length === 0);
        one("consoleSplit");
    }

    function test_rows_data() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function test_rows(data) {
        AppViewModel.theme = data.theme;
        addLogLines();
        waitForRendering(window.contentItem);
        const rows = shown("consoleRow");
        verify(rows.length >= 7, rows.length);
        let warning = null;
        let marker = null;
        let echo = null;
        for (const row of rows) {
            if (row.text === "<wrn> ctr_lte: RSRP low")
                warning = row;
            else if (row.text === "— disconnected —")
                marker = row;
            else if (row.text === "> version")
                echo = row;
        }
        verify(warning !== null && marker !== null && echo !== null);
        compare(warning.textColour, Theme.warning);
        compare(warning.levelTag, "W");
        compare(marker.textColour, Theme.textMuted);
        compare(echo.textColour, Theme.shellEcho);
        AppViewModel.theme = "system";
    }

    function test_searchBarKeys() {
        addLogLines();
        const log = vm().log;
        const logList = shown("consoleList").filter(l => l.model === log)[0];
        verify(logList !== undefined);
        logList.forceActiveFocus();
        const bar = one("logSearchBar");

        // Ctrl+F: Search mode, field focused; Enter / Shift+Enter step through the matches.
        keySequence("Ctrl+F");
        const field = findAll(bar, "searchField")[0];
        tryVerify(() => field.activeFocus);
        compare(bar.mode, "search");
        keyClick(Qt.Key_U);
        keyClick(Qt.Key_P);
        keyClick(Qt.Key_L);
        compare(log.findText, "upl");
        tryVerify(() => log.matchCount === 2);
        keyClick(Qt.Key_Return);
        compare(log.currentMatch, 1);
        verify(!log.following); // moving to a match stops following
        keyClick(Qt.Key_Return, Qt.ShiftModifier);
        compare(log.currentMatch, 2);

        // Ctrl+L: Filter mode with its own query; Ctrl+F returns to the search query.
        keySequence("Ctrl+L");
        compare(bar.mode, "filter");
        compare(field.text, "");
        keyClick(Qt.Key_C);
        keyClick(Qt.Key_T);
        keyClick(Qt.Key_R);
        compare(log.filterText, "ctr");
        tryVerify(() => log.visibleLines === 4 && !log.searching); // 3 matches + the marker
        keySequence("Ctrl+F");
        compare(bar.mode, "search");
        compare(field.text, "upl");
        one("filterChip"); // the active filter stays visible in Search mode

        // Esc clears the search and returns to the log; a second Esc clears the filter.
        keyClick(Qt.Key_Escape);
        compare(log.findText, "");
        tryVerify(() => logList.activeFocus);
        verify(log.filterActive);
        keyClick(Qt.Key_Escape);
        verify(!log.filterActive);
        tryVerify(() => log.visibleLines === 5);
    }

    function logList() {
        return shown("consoleList").filter(l => l.model === vm().log)[0];
    }

    function rowWithText(text) {
        return shown("consoleRow").filter(r => r.text === text)[0];
    }

    // K4/K5 (ADR-0045): rows stay plain text; matches are rectangles behind the text.
    function test_searchHighlightRectangles() {
        addLogLines();
        vm().log.findText = "uplink";
        tryVerify(() => vm().log.matchCount === 2 && !vm().log.searching);
        waitForRendering(window.contentItem);
        const row = rowWithText("<inf> ctr_cloud: Uplink started");
        verify(row !== undefined);
        compare(row.matchRanges.length, 1);
        compare(row.matchRanges[0].start, 17);
        compare(row.matchRanges[0].length, 6);
        const highlights = findAll(row, "matchHighlight");
        compare(highlights.length, 1);
        verify(highlights[0].width > 10 && highlights[0].x > 10, `${highlights[0].x} ${highlights[0].width}`);
        compare(highlights[0].color, Theme.findMatch);
        compare(findAll(rowWithText("<dbg> therm: 23.8 C"), "matchHighlight").length, 0);
        // The current match is drawn more strongly.
        vm().log.findNext();
        tryVerify(() => findAll(rowWithText("<inf> ctr_cloud: Uplink started"), "matchHighlight")[0].color
                  === Theme.findCurrent);
        // Wrapped lines: a match across a line break gets a rectangle per line.
        ConsoleSettings.wrapLines = true;
        const longText = "x".repeat(400) + "uplink" + "y".repeat(400);
        testSupport.appendConsoleLines(0, 1, 1, longText, 2, 0);
        tryVerify(() => vm().log.matchCount === 3);
        vm().log.findText = "x".repeat(100) + "uplink" + "y".repeat(100);
        tryVerify(() => vm().log.matchCount === 1 && !vm().log.searching);
        waitForRendering(window.contentItem);
        const wrapped = rowWithText(longText);
        verify(wrapped !== undefined);
        verify(findAll(wrapped, "matchHighlight").length >= 2, findAll(wrapped, "matchHighlight").length);
        ConsoleSettings.wrapLines = false;
    }

    // K2: new lines (and their matches) never move the view back to the current match.
    function test_newMatchesDoNotMoveTheView() {
        testSupport.appendConsoleLines(0, 1, 300, "<inf> uplink %1", 2, 0);
        vm().log.findText = "uplink";
        tryVerify(() => vm().log.matchCount === 300 && !vm().log.searching);
        const list = logList();
        tryVerify(() => list.height > 0 && list.contentHeight > list.height);
        vm().log.findNext(); // the first match, at the top
        waitForRendering(window.contentItem);
        list.contentY = list.contentHeight / 2; // the user scrolls elsewhere
        const position = list.contentY;
        testSupport.appendConsoleLines(0, 1, 20, "<inf> uplink more %1", 2, 0);
        waitForRendering(window.contentItem);
        compare(vm().log.matchCount, 320);
        compare(list.contentY, position);
    }

    // K3: a wheel scroll or holding the scroll bar stops following while lines stream in.
    function test_scrollWhileStreaming() {
        testSupport.appendConsoleLines(0, 1, 300, "<inf> line %1", 2, 0);
        const list = logList();
        tryVerify(() => list.height > 0 && list.contentHeight > list.height && list.atYEnd);
        verify(vm().log.following);
        mouseWheel(list, list.width / 2, list.height / 2, 0, 360);
        testSupport.appendConsoleLines(0, 1, 5, "<inf> during %1", 2, 0); // while it moves
        tryVerify(() => !list.moving, 3000);
        verify(!vm().log.following);
        verify(!list.atYEnd);
        const position = list.contentY;
        testSupport.appendConsoleLines(0, 1, 5, "<inf> after %1", 2, 0);
        waitForRendering(window.contentItem);
        compare(list.contentY, position);
        verify(vm().log.newLinesWhileNotFollowing >= 5);
        one("newLinesPill");

        // The scroll bar: pressing it stops following at once.
        vm().log.following = true;
        tryVerify(() => list.atYEnd);
        const bar = findAll(list.parent, "consoleScrollBar")[0];
        verify(bar !== undefined);
        mousePress(bar, bar.width / 2, bar.height - 4);
        verify(!vm().log.following);
        testSupport.appendConsoleLines(0, 1, 5, "<inf> held %1", 2, 0);
        waitForRendering(window.contentItem);
        verify(!vm().log.following);
        mouseRelease(bar, bar.width / 2, bar.height - 4);
    }

    // ---- 0021 -----------------------------------------------------------------------------

    function topIndex(list) {
        return list.indexAt(1, list.contentY + 1);
    }

    // ADR-0169: a wheel notch moves whole rows (the system's lines per scroll), no coasting, the
    // top row aligned; keys move by rows and pages.
    function test_wheelAndKeysScrollWholeRows() {
        testSupport.appendConsoleLines(0, 1, 300, "<inf> line %1", 2, 0);
        const list = logList();
        tryVerify(() => list.height > 0 && list.contentHeight > list.height && list.atYEnd);
        const lines = Math.max(1, Qt.styleHints.wheelScrollLines);
        const before = topIndex(list);
        mouseWheel(list, list.width / 2, list.height / 2, 0, 120);
        verify(!list.moving); // no coasting
        verify(!vm().log.following);
        compare(topIndex(list), before - lines);
        const row = list.itemAtIndex(topIndex(list));
        verify(row !== null);
        compare(row.y, list.contentY); // aligned to a row boundary

        list.forceActiveFocus();
        const top = topIndex(list);
        keyClick(Qt.Key_Up);
        compare(topIndex(list), top - 1);
        keyClick(Qt.Key_Down);
        compare(topIndex(list), top);
        keyClick(Qt.Key_PageUp);
        verify(topIndex(list) < top - 1);
        keyClick(Qt.Key_Home, Qt.ControlModifier);
        compare(topIndex(list), 0);
        keyClick(Qt.Key_End, Qt.ControlModifier);
        verify(vm().log.following);
        tryVerify(() => list.atYEnd);
    }

    // Review F3: a diagonal wheel event keeps its vertical part (the dominant axis decides).
    function test_diagonalWheelScrollsVertically() {
        testSupport.appendConsoleLines(0, 1, 300, "<inf> line %1 " + "wide ".repeat(100), 2, 0);
        const list = logList();
        tryVerify(() => list.height > 0 && list.contentHeight > list.height && list.atYEnd);
        const lines = Math.max(1, Qt.styleHints.wheelScrollLines);
        const before = topIndex(list);
        const x = list.contentX;
        mouseWheel(list, list.width / 2, list.height / 2, 20, 120);
        compare(topIndex(list), before - lines);
        compare(list.contentX, x);
        // Mostly horizontal: scrolls sideways only.
        mouseWheel(list, list.width / 2, list.height / 2, -120, 10);
        compare(topIndex(list), before - lines);
        verify(list.contentX > x);
    }

    // ADR-0173 (N10): at the end of a log with a line wider than the view, the last row is fully
    // above the horizontal bar, a press selects it and Ctrl+C copies it; a press suspends Follow.
    function test_lastRowIsSelectableAtTheEnd() {
        testSupport.appendConsoleLines(0, 1, 100, "<inf> line %1", 2, 0);
        testSupport.appendConsoleLines(0, 1, 1, "<inf> " + "wide ".repeat(200), 2, 0);
        testSupport.appendConsoleLines(0, 1, 1, "<inf> the last line", 2, 0);
        const list = logList();
        tryVerify(() => list.atYEnd && list.count === 102);
        const hbar = findAll(list.parent, "consoleHorizontalBar")[0];
        tryVerify(() => hbar.visible && hbar.height > 0);
        waitForRendering(window.contentItem);
        const last = list.itemAtIndex(list.count - 1);
        verify(last !== null);
        const lastBottom = last.mapToItem(window.contentItem, 0, last.height).y;
        const barTop = hbar.mapToItem(window.contentItem, 0, 0).y;
        verify(lastBottom <= barTop + 0.5, `last row ends at ${lastBottom}, bar starts at ${barTop}`);
        const vbar = findAll(list.parent, "consoleScrollBar")[0];
        verify(vbar.mapToItem(window.contentItem, 0, 0).x >= list.mapToItem(window.contentItem, list.width, 0).x - 0.5);
        mousePress(last, 10, last.height / 2);
        // Follow is suspended while pressed: new lines do not move the rows.
        const position = list.contentY;
        testSupport.appendConsoleLines(0, 1, 5, "<inf> during %1", 2, 0);
        waitForRendering(window.contentItem);
        compare(list.contentY, position);
        mouseRelease(last, 10, last.height / 2);
        verify(last.selected);
        verify(vm().log.hasSelection);
        keySequence(StandardKey.Copy);
        // Follow resumes after the release (the view was at the end).
        verify(vm().log.following);
        tryVerify(() => list.atYEnd);
    }

    // ADR-0173: dragging a selection below the view scrolls by rows and extends it to the end.
    function test_dragPastTheEdgeExtendsTheSelection() {
        testSupport.appendConsoleLines(0, 1, 200, "<inf> line %1", 2, 0);
        const list = logList();
        tryVerify(() => list.atYEnd && list.count === 200);
        vm().following = false;
        waitForRendering(window.contentItem);
        list.positionViewAtIndex(100, ListView.Beginning);
        waitForRendering(window.contentItem);
        verify(topIndex(list) >= 90, topIndex(list));
        const first = list.itemAtIndex(topIndex(list) + 1);
        mousePress(first, 10, first.height / 2);
        for (let y = 0; y < 400; y += 40)
            mouseMove(first, 10, first.height / 2 + list.height + y, -1, Qt.LeftButton);
        tryVerify(() => list.atYEnd, 10000);
        mouseMove(first, 10, first.height / 2 + list.height + 400, -1, Qt.LeftButton);
        wait(150);
        mouseRelease(first, 10, first.height / 2 + list.height + 400);
        const lastRow = list.itemAtIndex(list.count - 1);
        verify(lastRow !== null && lastRow.selected);
    }

    // ADR-0171: hexdump lines are hidden by the default chips; H shows them muted.
    function test_hexdumpChip() {
        testSupport.appendConsoleLines(0, 1, 1, "<inf> app: Received:", 2, 0);
        testSupport.appendConsoleLines(0, 1, 2, "    44 74 4f 34 |DtO4 %1", 5, 0);
        const log = vm().log;
        const chip = one("levelHexdump");
        verify(!chip.checked);
        tryVerify(() => log.visibleLines === 1 && log.totalLines === 3);
        verify(!log.filterActive);
        mouseClick(chip);
        tryVerify(() => log.visibleLines === 3 && !log.searching);
        waitForRendering(window.contentItem);
        const row = shown("consoleRow").filter(r => r.levelTag === "H")[0];
        verify(row !== undefined);
        compare(row.textColour, Theme.textMuted);
        mouseClick(chip);
        tryVerify(() => log.visibleLines === 1);
    }

    function ghost() {
        return findAll(window.contentItem, "ghostText")[0];
    }

    // ADR-0170: the automatic list above the input (commands, then history), ghost text, the
    // ↑ ↓ / Tab / Esc / Enter rules, Tab completion; off by the setting.
    function test_suggestions() {
        AppViewModel.currentTabViewModel.connection.connectDevice();
        tryCompare(AppViewModel.currentTabViewModel, "state", DeviceTabViewModel.Connected, 10000);
        tryVerify(() => vm().inputEnabled);
        testSupport.addConsoleHistory(0, "lte test");
        testSupport.addConsoleHistory(0, "led on");
        testSupport.setConsoleCommands(0, ["lte", "led", "app"]);
        const input = one("shellInput");
        const field = one("commandInput");
        field.forceActiveFocus();
        field.text = "";
        keyClick(Qt.Key_L);
        const list = input.suggestionList;
        tryVerify(() => list.opened);
        compare(list.rows.length, 4); // lte, led, then "led on", "lte test"
        compare(list.rows[0].label, "lte");
        compare(list.rows[2].kind, "history");
        compare(findAll(list.contentItem, "suggestionRow0")[0].highlighted, true);
        // The ghost text is the rest of the highlighted row.
        compare(ghost().text, "te ");
        verify(ghost().visible);
        // Enter sends the typed line unless the highlight was moved.
        keyClick(Qt.Key_Down);
        compare(list.currentIndex, 1);
        keyClick(Qt.Key_Return);
        compare(field.text, "led ");
        // Esc closes the list until the next keystroke.
        tryVerify(() => list.opened); // "led " lists "led on"
        keyClick(Qt.Key_Escape);
        verify(!list.opened);
        verify(!ghost().visible);
        keyClick(Qt.Key_O);
        tryVerify(() => list.opened);
        compare(ghost().text, "n");
        // → accepts the ghost text / the highlighted row.
        keyClick(Qt.Key_Right);
        compare(field.text, "led on");
        // Tab completes and never leaves the field.
        field.text = "";
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_Tab);
        compare(field.text, "app ");
        verify(field.activeFocus);
        // Off by the setting: no list, the history ghost text stays.
        ConsoleSettings.suggestions = false;
        field.text = "";
        keyClick(Qt.Key_L);
        keyClick(Qt.Key_E);
        verify(!list.opened);
        compare(ghost().text, "d on");
        ConsoleSettings.suggestions = true;
        field.text = "";
    }

    // Review F6 and the ADR-0170 key rules: ↑ on the first row does not count as moving the
    // highlight (Enter sends the typed line); Esc with nothing to close is not consumed and Tab then
    // leaves the field; Ctrl+Tab still switches tabs.
    function test_inputKeys() {
        AppViewModel.currentTabViewModel.connection.connectDevice();
        tryCompare(AppViewModel.currentTabViewModel, "state", DeviceTabViewModel.Connected, 10000);
        tryVerify(() => vm().inputEnabled);
        testSupport.setConsoleCommands(0, ["lte", "led"]);
        const input = one("shellInput");
        const field = one("commandInput");
        const list = input.suggestionList;
        field.forceActiveFocus();
        field.text = "";
        keyClick(Qt.Key_L);
        tryVerify(() => list.opened);
        keyClick(Qt.Key_Up); // already on the first row
        compare(list.currentIndex, 0);
        verify(!input.highlightMoved);
        keyClick(Qt.Key_Return); // sends "l"
        compare(field.text, "");
        tryVerify(() => vm().shell.totalLines > 0 && vm().historySearch("l").indexOf("l") >= 0);
        // Esc with nothing to close, then Tab leaves the field.
        verify(field.activeFocus);
        keyClick(Qt.Key_Escape);
        verify(field.activeFocus);
        keyClick(Qt.Key_Tab);
        verify(!field.activeFocus);
        // Tab in the field completes and stays.
        field.forceActiveFocus();
        keyClick(Qt.Key_L);
        keyClick(Qt.Key_T);
        keyClick(Qt.Key_Tab);
        compare(field.text, "lte ");
        verify(field.activeFocus);
        // Ctrl+Tab switches tabs.
        field.text = "";
        AppViewModel.newTab();
        AppViewModel.currentTab = 0;
        field.forceActiveFocus();
        keyClick(Qt.Key_Tab, Qt.ControlModifier);
        compare(AppViewModel.currentTab, 1);
    }

    // K11: the split handle has the 2 px accent focus ring.
    function test_splitHandleFocusRing() {
        addLogLines();
        const handle = one("splitHandle");
        const ring = findAll(handle, "splitHandleFocusRing")[0];
        verify(!ring.visible);
        handle.forceActiveFocus();
        verify(ring.visible);
        compare(ring.border.width, Theme.focusRingWidth);
        compare(ring.border.color, Theme.accent);
    }

    // K9: the input keeps a command that could not be sent.
    function test_unsentCommandStaysInTheInput() {
        addLogLines(); // the split with the input is shown
        const field = one("commandInput");
        verify(!vm().inputEnabled); // no device: nothing is sent
        field.text = "app config show";
        mouseClick(one("sendButton")); // disabled: nothing happens
        compare(field.text, "app config show");
        compare(vm().send("app config show"), false);
        field.text = "";
    }

    // Down in the input keeps the typed text when not walking the history (decision 2026-10-16).
    function test_downKeepsTypedText() {
        AppViewModel.currentTabViewModel.connection.connectDevice();
        tryCompare(AppViewModel.currentTabViewModel, "state", DeviceTabViewModel.Connected, 10000);
        tryVerify(() => vm().inputEnabled);
        const field = one("commandInput");
        field.forceActiveFocus();
        field.text = "app conf";
        keyClick(Qt.Key_Down);
        compare(field.text, "app conf");
        verify(vm().send("version"));
        field.text = "typed";
        keyClick(Qt.Key_Up);
        compare(field.text, "version");
        keyClick(Qt.Key_Down);
        compare(field.text, "typed");
        keyClick(Qt.Key_Down);
        compare(field.text, "typed");
        field.text = "";
    }

    function test_levelChips() {
        addLogLines();
        const log = vm().log;
        mouseClick(one("levelDebug"));
        compare(log.levelMask, 14);
        tryVerify(() => log.visibleLines === 4 && !log.searching);
        mouseClick(one("levelDebug"));
        compare(log.levelMask, 15);
    }

    function test_shellSearch() {
        addLogLines();
        compare(shown("shellSearchBar").length, 0);
        const shellList = shown("consoleList").filter(l => l.model === vm().shell)[0];
        shellList.forceActiveFocus();
        keySequence("Ctrl+F");
        const bar = one("shellSearchBar");
        const field = findAll(bar, "searchField")[0];
        tryVerify(() => field.activeFocus);
        compare(findAll(bar, "searchModeButton")[0].visible, false); // Search only
        keyClick(Qt.Key_V);
        compare(vm().shell.findText, "v");
        tryVerify(() => vm().shell.matchCount === 1);
        keyClick(Qt.Key_Escape);
        compare(shown("shellSearchBar").length, 0);
        compare(vm().shell.findText, "");
    }

    function test_banners() {
        testSupport.addDroppedLines(0, 312);
        const dropped = one("droppedBanner");
        verify(dropped.text.startsWith("312 lines were dropped"), dropped.text);
        mouseClick(findAll(dropped, "bannerDismiss")[0]);
        compare(vm().droppedLines, 0);
        vm().paused = true;
        testSupport.appendConsoleLines(0, 1, 3, "late %1", 2, 0);
        const paused = one("pausedBanner");
        verify(paused.text.indexOf("3") >= 0, paused.text);
        vm().paused = false;
        compare(shown("pausedBanner").length, 0);
    }

    function test_stackedBelow1000() {
        addLogLines();
        window.width = 1280;
        waitForRendering(window.contentItem);
        let shell = rect(one("shellPane"));
        let log = rect(one("logPane"));
        verify(shell.x + shell.w <= log.x, "side by side");
        window.width = 960; // page width < 1000 px
        tryVerify(() => vm().stackedLayout);
        waitForRendering(window.contentItem);
        shell = rect(one("shellPane"));
        log = rect(one("logPane"));
        verify(shell.y + shell.h <= log.y, "stacked, shell on top");
        vm().swapPanes = true;
        waitForRendering(window.contentItem);
        shell = rect(one("shellPane"));
        log = rect(one("logPane"));
        verify(log.y + log.h <= shell.y, "swapped");
        vm().swapPanes = false;
    }

    function test_toolbar960Czech_data() {
        return [{ tag: "960x600-cs", width: 960, height: 600, language: "cs" },
                { tag: "1099x700-cs", width: 1099, height: 700, language: "cs" },
                { tag: "1280x800-cs", width: 1280, height: 800, language: "cs" }];
    }

    function test_toolbar960Czech(data) {
        AppViewModel.setLanguage(data.language);
        window.width = data.width;
        window.height = data.height;
        addLogLines();
        vm().log.filterText = "ctr"; // the filter count text is shown
        testSupport.appendConsoleLines(0, 1, 1, "x", 2, 0);
        waitForRendering(window.contentItem);
        const names = ["resetButton", "reconnectButton", "batchButton", "clearButton", "saveButton",
                       "followButton", "pauseButton", "consoleMore"];
        let items = [];
        for (const name of names)
            items.push(one(name));
        checkInside(items);
        checkNoOverlap(items);
        // The bottom bar of the log and the command input stay inside the window.
        checkInside([one("logSearchBar"), one("commandInput"), one("sendButton")]);
        checkInside(findAll(one("logSearchBar"), "levelError").concat(findAll(one("logSearchBar"), "searchField")));
    }

    function test_settingsSection() {
        const component = Qt.createComponent("HardwarioMonitor", "ConsoleSection");
        compare(component.status, Component.Ready, component.errorString());
        const section = component.createObject(window.contentItem);
        verify(section !== null);
        const timestamps = findAll(section, "consoleTimestamps")[0];
        compare(timestamps.checked, ConsoleSettings.showTimestamps);
        ConsoleSettings.showTimestamps = !ConsoleSettings.showTimestamps;
        compare(timestamps.checked, ConsoleSettings.showTimestamps);
        ConsoleSettings.showTimestamps = false;
        verify(findAll(section, "consoleRetention")[0].text.indexOf("50") >= 0);
        section.destroy();
    }

    name: "Console"
    when: windowShown
}
