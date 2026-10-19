// Console performance (plan 0005, Tests and verification): the fake J-Link library streams
// 5 000 lines/s into the shell and 5 000 lines/s into the log for HIO_CONSOLE_PERF_SECONDS
// (skipped when unset; the plan's run is 600 s). Measured: GUI-thread stalls (a 16 ms timer's
// lateness), frames per second, peak RSS (< 400 MB), lines received; then the GUI thread is blocked
// for 3 s so that the session's event queue overflows and the dropped-lines banner must appear.
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null
    property real lastTick: 0
    property real maxGap: 0
    property var gaps: []
    property int frames: 0

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

    Timer {
        id: ticker

        interval: 16
        repeat: true
        onTriggered: {
            const now = Date.now();
            if (testCase.lastTick > 0) {
                const gap = now - testCase.lastTick;
                testCase.maxGap = Math.max(testCase.maxGap, gap);
                testCase.gaps.push(gap);
            }
            testCase.lastTick = now;
        }
    }

    Connections {
        target: testCase.window

        function onFrameSwapped() {
            testCase.frames += 1;
        }
    }

    function initTestCase() {
        if (testSupport.perfSeconds <= 0)
            skip("HIO_CONSOLE_PERF_SECONDS is not set");
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        verify(testSupport.useScenario(""));
        if (window)
            window.destroy();
        wait(50);
        testSupport.resetTabs();
    }

    function test_highLineRate() {
        if (testSupport.perfSeconds <= 0)
            skip("HIO_CONSOLE_PERF_SECONDS is not set");
        verify(testSupport.useScenario(JSON.stringify({
            libraryVersion: 81000,
            probes: [{ serialNumber: 801000001, product: "Test Probe",
                       rtt: { up: [{ name: "Terminal", size: 65536 }, { name: "Logger", size: 65536 }],
                              stream: [{ buffer: 0, linesPerSecond: 5000, text: "uart:~$ shell output line" },
                                       { buffer: 1, linesPerSecond: 5000, text: "[00:00:01.000,000] <inf> perf: log line" }] } }]
        })));
        testSupport.resetTabs();
        window.width = 1280;
        window.height = 800;
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.showPage("console");
        const vm = testSupport.consoleViewModel(0);
        AppViewModel.currentTabViewModel.connection.connectDevice();
        tryCompare(AppViewModel.currentTabViewModel, "state", DeviceTabViewModel.Connected, 15000);
        // A search and a filter run on the log while it streams (time-sliced, never pausing it).
        vm.log.findText = "99";
        vm.log.levelMask = 15;

        const startRss = testSupport.rssMegabytes();
        let peakRss = startRss;
        const startSeq = testSupport.consoleLastSeq(0);
        const start = Date.now();
        frames = 0;
        maxGap = 0;
        gaps = [];
        lastTick = 0;
        ticker.start();
        let lastReport = start;
        while (Date.now() - start < testSupport.perfSeconds * 1000) {
            wait(1000);
            peakRss = Math.max(peakRss, testSupport.rssMegabytes());
            if (Date.now() - lastReport >= 60000) {
                lastReport = Date.now();
                console.log("[ perf ] t", ((lastReport - start) / 1000).toFixed(0), "s: timer gap max so far", maxGap,
                            "ms, log rows", vm.log.rowCount(), "matches", vm.log.matchCount, "searching", vm.log.searching,
                            "RSS", testSupport.rssMegabytes().toFixed(0), "MiB");
            }
        }
        ticker.stop();
        const seconds = (Date.now() - start) / 1000;
        const lines = testSupport.consoleLastSeq(0) - startSeq;
        const sorted = gaps.slice().sort((a, b) => a - b);
        const p99 = sorted.length > 0 ? sorted[Math.floor(sorted.length * 0.99)] : 0;
        console.log("[ perf ] seconds", seconds.toFixed(1), "lines", lines, "lines/s", (lines / seconds).toFixed(0),
                    "shell rows", vm.shell.rowCount(), "log rows", vm.log.rowCount(), "log matches", vm.log.matchCount,
                    "timer gap max", maxGap, "ms p99", p99, "ms", "frames/s", (frames / seconds).toFixed(1),
                    "RSS start", startRss.toFixed(0), "MiB peak", peakRss.toFixed(0), "MiB",
                    "dropped", vm.droppedLines);
        verify(lines > seconds * 9000, "received " + lines + " lines");
        verify(peakRss < 400, "peak RSS " + peakRss + " MiB");
        verify(vm.log.rowCount() <= 100000 && vm.shell.rowCount() <= 100000);

        // Forced overflow: the GUI thread does not drain the session's events for 3 s.
        testSupport.blockGuiThread(3000);
        tryVerify(() => vm.droppedLines > 0, 5000);
        tryVerify(() => findAll(window.contentItem, "droppedBanner").filter(b => b.visible).length === 1, 5000);
        console.log("[ perf ] forced overflow: dropped", vm.droppedLines, "lines");
        AppViewModel.currentTabViewModel.connection.disconnectDevice();
    }

    name: "ConsolePerformance"
    when: windowShown
}
