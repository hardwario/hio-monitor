// Screenshots of the console page (plan 0005, C6) in both themes and both languages; only when
// HIO_SCREENSHOT_DIR is set (QT_QUICK_BACKEND=software for the offscreen platform): normal,
// search, filter, stacked below 1000 px, 960 px in Czech, the empty states, the banners and the
// error panel, the history popup.
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

    function shown(name) {
        return findAll(window.contentItem, name).filter(item => {
            for (let current = item; current; current = current.parent) {
                if (!current.visible)
                    return false;
            }
            return true;
        });
    }

    function save(name) {
        waitForRendering(window.contentItem, 2000);
        wait(250);
        const image = grabImage(window.contentItem);
        verify(image.width > 0, name);
        image.save(testSupport.screenshotDir + "/console-" + name + "-" + AppViewModel.theme + "-" + AppViewModel.language + ".png");
    }

    function fill() {
        const log = [["<inf> app_work: Sample triggered", 2], ["<dbg> ctr_therm: Temperature: 23.81 C", 1],
                     ["<inf> ctr_lte_v2: Attach state: registered", 2], ["<wrn> ctr_lte_v2: RSRP low: -112 dBm", 3],
                     ["<inf> ctr_cloud: Uplink started (512 B)", 2], ["<err> ctr_cloud: Uplink timeout, retry 1/3", 4],
                     ["<inf> ctr_cloud: Uplink started (512 B)", 2], ["<inf> ctr_cloud: Uplink done", 2],
                     ["<dbg> app_work: Next sample in 60 s", 1], ["<wrn> ctr_batt: Voltage 3.41 V under load", 3]];
        for (let round = 0; round < 3; ++round) {
            for (const [text, level] of log)
                testSupport.appendConsoleLines(0, 1, 1, "[00:0" + round + ":12.345,678] " + text, level, 0);
        }
        testSupport.appendConsoleLines(0, 0, 1, "app config show", 0, 1);
        for (const text of ["config interval-sample 60", "config interval-report 900", "config lte-enabled true",
                            "config backup-report true", "uart:~$ "])
            testSupport.appendConsoleLines(0, 0, 1, text, 0, 0);
        testSupport.appendConsoleLines(0, 0, 1, "lte state", 0, 1);
        testSupport.appendConsoleLines(0, 0, 1, "state: registered (roaming), RSRP -112 dBm", 0, 0);
    }

    function initTestCase() {
        if (testSupport.screenshotDir === "")
            skip("HIO_SCREENSHOT_DIR is not set");
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        if (window)
            window.destroy();
        wait(50);
        testSupport.resetTabs();
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function test_shots_data() {
        return [{ tag: "light-en", theme: "light", language: "en" }, { tag: "dark-en", theme: "dark", language: "en" },
                { tag: "light-cs", theme: "light", language: "cs" }, { tag: "dark-cs", theme: "dark", language: "cs" }];
    }

    function test_shots(data) {
        if (testSupport.screenshotDir === "")
            skip("HIO_SCREENSHOT_DIR is not set");
        AppViewModel.theme = data.theme;
        AppViewModel.setLanguage(data.language);
        testSupport.resetTabs();
        window.width = 1280;
        window.height = 800;
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.showPage("console");
        tryVerify(() => shown("consoleToolbar").length === 1, 5000);
        const vm = testSupport.consoleViewModel(0);
        save("01-not-connected");

        AppViewModel.currentTabViewModel.connection.connectDevice();
        tryCompare(AppViewModel.currentTabViewModel, "state", DeviceTabViewModel.Connected, 10000);
        vm.clear(3);
        save("02-waiting-for-data");

        fill();
        save("03-normal");

        vm.log.findText = "uplink";
        tryVerify(() => vm.log.matchCount > 0 && !vm.log.searching);
        vm.log.findNext();
        vm.log.findNext();
        vm.shell.findText = "config";
        save("04-search");
        vm.shell.findText = "";

        const bar = shown("logSearchBar")[0];
        bar.focusField("filter");
        vm.log.filterText = "ctr_cloud";
        vm.log.levelMask = 14;
        tryVerify(() => !vm.log.searching);
        save("05-filter");
        bar.focusField("search");
        save("06-search-with-filter-chip");
        vm.log.clearFilter();
        vm.log.findText = "";
        tryVerify(() => !vm.log.searching);

        vm.log.filterRegex = true;
        bar.focusField("filter");
        vm.log.filterText = "ctr_cloud";
        tryVerify(() => !vm.log.searching);
        vm.log.filterText = "ctr_cloud("; // invalid: the last valid result stays (ADR-0046)
        save("07-invalid-regex");
        vm.log.filterText = "";
        vm.log.filterRegex = false;
        bar.focusField("search");

        testSupport.addDroppedLines(0, 312);
        vm.paused = true;
        testSupport.appendConsoleLines(0, 1, 12, "<inf> app: late %1", 2, 0);
        vm.runBatch("file:///nonexistent/batch.txt");
        // The shell's session-warning banner with [Details] (0004 as amended by 0005).
        testSupport.setConnectionWarning(0, "probe.firmware_outdated");
        tryVerify(() => shown("connectionWarning").length === 1);
        shown("connectionWarning")[0].detailsExpanded = true;
        save("08-banners-and-error");
        testSupport.setConnectionWarning(0, "io.write_failed");
        save("08b-console-file-warning");
        AppViewModel.currentTabViewModel.connection.dismissWarning();
        vm.dismissError();
        vm.dismissDropped();
        vm.paused = false;

        vm.log.following = false;
        testSupport.appendConsoleLines(0, 1, 8, "<inf> app: new %1", 2, 0);
        save("09-new-lines-pill");
        vm.log.following = true;

        const input = shown("commandInput")[0];
        vm.send("app config show");
        vm.send("lte state");
        input.forceActiveFocus();
        keySequence("Ctrl+R");
        tryVerify(() => shown("shellInput")[0].historyOpen);
        save("10-history");
        keyClick(Qt.Key_Escape);

        window.width = 990; // page < 1000 px: stacked
        window.height = 760;
        save("11-stacked-990");

        window.width = 960;
        window.height = 600;
        save("12-minimum-960x600");
        testSupport.resetTabs();
    }

    name: "ConsoleScreenshots"
    when: windowShown
}
