// The global shortcuts (ADR-0051): the map has no duplicates and the Shortcut items call
// AppViewModel.
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    width: 400
    height: 300
    visible: true

    Shortcuts {
        id: shortcuts
    }

    function init() {
        testSupport.resetTabs();
    }

    function cleanupTestCase() {
        testSupport.resetTabs();
    }

    function test_noDuplicates() {
        const seen = {};
        for (const entry of AppViewModel.shortcuts.entries()) {
            const key = entry.scope + " " + entry.keys;
            verify(seen[key] === undefined, key);
            seen[key] = true;
        }
        // A page-scope shortcut never repeats a global one.
        const global = AppViewModel.shortcuts.entries().filter(entry => entry.scope === "global").map(entry => entry.keys);
        for (const entry of AppViewModel.shortcuts.entries()) {
            if (entry.scope !== "global")
                verify(global.indexOf(entry.keys) < 0, entry.id);
        }
    }

    function test_tabShortcuts() {
        keyClick(Qt.Key_T, Qt.ControlModifier);
        compare(AppViewModel.tabs.count, 2);
        compare(AppViewModel.currentTab, 1);
        keyClick(Qt.Key_Tab, Qt.ControlModifier);
        compare(AppViewModel.currentTab, 0);
        keyClick(Qt.Key_PageDown, Qt.ControlModifier);
        compare(AppViewModel.currentTab, 1);
        keyClick(Qt.Key_W, Qt.ControlModifier);
        compare(AppViewModel.tabs.count, 1);
    }

    function test_globalShortcuts() {
        keyClick(Qt.Key_F1);
        compare(AppViewModel.shortcutOverlayVisible, true);
        AppViewModel.shortcutOverlayVisible = false;
        keyClick(Qt.Key_Slash, Qt.ControlModifier);
        compare(AppViewModel.shortcutOverlayVisible, true);
        AppViewModel.shortcutOverlayVisible = false;
        keyClick(Qt.Key_Comma, Qt.ControlModifier);
        compare(AppViewModel.globalPage, "settings");
        verify(testSupport.configureTab(0, "app", 801000001, ""));
        keyClick(Qt.Key_2, Qt.ControlModifier);
        compare(AppViewModel.globalPage, "");
        compare(AppViewModel.currentTabViewModel.currentPage, "flash");
        keyClick(Qt.Key_1, Qt.ControlModifier);
        compare(AppViewModel.currentTabViewModel.currentPage, "console");
        keyClick(Qt.Key_D, Qt.ControlModifier | Qt.ShiftModifier);
        verify(AppViewModel.currentTabViewModel.state !== DeviceTabViewModel.Disconnected);
        tryCompare(AppViewModel.currentTabViewModel, "state", DeviceTabViewModel.Connected, 10000);
        keyClick(Qt.Key_D, Qt.ControlModifier | Qt.ShiftModifier);
        compare(AppViewModel.currentTabViewModel.state, DeviceTabViewModel.Disconnected);
    }

    name: "Shortcuts"
    when: windowShown
}
