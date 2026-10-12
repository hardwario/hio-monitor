// The application shell with the contract test page (plan 0004, B1-B5): tabs and the 8-tab limit,
// the probe list, Connect / Disconnect against the fake J-Link library, the rail and its busy
// badge, close confirmation, the connection ErrorPanel, the global Settings page, the shortcut
// overlay, toasts.
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    // A click that is never merged with the previous one into a double click.
    function click(item, button) {
        mouseClick(item, item.width / 2, item.height / 2, button === undefined ? Qt.LeftButton : button,
                   Qt.NoModifier, Qt.styleHints.mouseDoubleClickInterval + 50);
    }

    function find(name) {
        return findChild(window.contentItem, name);
    }

    function findAll(parent, name) {
        let result = [];
        if (!parent)
            return result;
        const children = parent.children ? parent.children : [];
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

    function visibleAll(name) {
        return findAll(window.contentItem, name).filter(effectivelyVisible);
    }

    function currentBar(name) {
        // The visible device bar of the current tab.
        return visibleAll(name)[0];
    }

    function init() {
        AppViewModel.setLanguage("en");
        testSupport.resetTabs();
        window.width = 1280;
        window.height = 800;
        waitForRendering(window.contentItem);
    }

    function initTestCase() {
        const component = Qt.createComponent("HardwarioMonitor", "Main");
        verify(component.status === Component.Ready, component.errorString());
        window = component.createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        testSupport.resetTabs();
        window.destroy();
        wait(50); // the deferred delete runs before the engine goes away
    }

    function test_tabsAndLimit() {
        compare(visibleAll("deviceTab").length, 1);
        for (let i = 1; i < AppViewModel.maxTabs; ++i)
            AppViewModel.newTab();
        tryCompare(AppViewModel.tabs, "count", 8);
        compare(visibleAll("deviceTab").length, 8);
        const add = find("tabAdd");
        compare(add.enabled, false);
        compare(add.Accessible.description, "At most 8 devices can be open.");
        compare(AppViewModel.currentTab, 7);
        // Middle-click closes a tab.
        const tabs = visibleAll("deviceTab");
        click(tabs[2], Qt.MiddleButton);
        tryCompare(AppViewModel.tabs, "count", 7);
        verify(add.enabled);
        // The × of a tab.
        click(findAll(tabs[0], "tabClose")[0]);
        tryCompare(AppViewModel.tabs, "count", 6);
        // A click selects a tab.
        const remaining = visibleAll("deviceTab");
        click(remaining[1]);
        compare(AppViewModel.currentTab, 1);
    }

    function test_chooseDeviceAndConnect() {
        const tab = AppViewModel.currentTabViewModel;
        tryVerify(() => visibleAll("probeRow").length === 2, 5000);
        verify(!find("noProbesState").visible);
        compare(find("connectButton").enabled, false); // two free probes: none preselected
        click(visibleAll("probeRow")[1]);
        compare(tab.picker.selectedRow, 1);
        verify(find("connectButton").enabled);
        click(find("connectButton"));
        tryCompare(tab, "state", DeviceTabViewModel.Connected, 10000);
        compare(tab.title, "CHESTER app · 801000002");
        compare(currentBar("probeLabel").text, "Test Probe 2 · 801000002");
        tryCompare(currentBar("stateText"), "text", "Connected · 3.30 V"); // ADR-0091
        compare(currentBar("connectionAction").text, "Disconnect");
        compare(currentBar("targetBox").enabled, false); // disabled while connected
        click(currentBar("connectionAction"));
        tryCompare(tab, "state", DeviceTabViewModel.Disconnected);
        compare(currentBar("connectionAction").text, "Connect");
        verify(currentBar("targetBox").enabled);
    }

    function test_probeInUseInAnotherTab() {
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.newTab();
        compare(AppViewModel.currentTab, 1);
        tryVerify(() => visibleAll("probeInUse").length === 1, 5000);
        compare(visibleAll("probeInUse")[0].text, "In use (tab 1)");
        // Choosing it switches to that tab.
        click(visibleAll("probeRow")[0]);
        compare(AppViewModel.currentTab, 0);
    }

    function test_contractPageBusyAndCloseConfirmation() {
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.newTab();
        AppViewModel.currentTab = 0;
        AppViewModel.showPage("test");
        const tab = AppViewModel.tabs.at(0);
        compare(tab.currentPage, "test");
        const page = find("page_test");
        tryVerify(() => page.item !== null);
        compare(page.item.objectName, "testPage");
        verify(page.item.vm !== null);
        compare(page.item.tab, tab);
        compare(findChild(page.item, "testPageLabel").text, "sessions 0");
        // A session change reaches the page.
        tab.connection.connectDevice();
        tryCompare(findChild(page.item, "testPageLabel"), "text", "sessions 1");
        tryCompare(tab, "state", DeviceTabViewModel.Connected, 10000);
        tab.connection.disconnectDevice();
        // Busy: the rail badge and the device bar text.
        const vm = testSupport.testPage(0);
        vm.startWork();
        compare(tab.state, DeviceTabViewModel.Busy);
        tryVerify(() => visibleAll("railBusy").length === 1);
        compare(currentBar("stateText").text, "Working…");
        compare(currentBar("connectionAction").enabled, false);
        // Closing asks with the page's text.
        AppViewModel.closeTab(0);
        const dialog = findChild(window, "closeTabDialog");
        tryVerify(() => dialog.opened);
        compare(dialog.body, "Work is in progress. Cancel it and close the tab?");
        click(findChild(dialog.footer, "dialogReject")); // [Keep running]
        tryVerify(() => !dialog.visible);
        compare(AppViewModel.tabs.count, 2);
        AppViewModel.closeTab(0);
        tryVerify(() => dialog.opened);
        click(findChild(dialog.footer, "dialogAccept")); // [Cancel and close]
        tryCompare(AppViewModel.tabs, "count", 1);
    }

    function test_quitConfirmation() {
        verify(testSupport.configureTab(0, "app", 801000001, ""));
        testSupport.testPage(0).startWork();
        compare(AppViewModel.canQuit(), false);
        const dialog = findChild(window, "quitDialog");
        tryVerify(() => dialog.opened);
        compare(dialog.body, "Work is in progress. Cancel it and close the tab?");
        click(findChild(dialog.footer, "dialogReject"));
        tryVerify(() => !dialog.visible);
        testSupport.finishWork(0);
        verify(AppViewModel.canQuit());
    }

    function test_connectionErrorPanel() {
        verify(testSupport.configureTab(0, "app", 123456, ""));
        testSupport.setConnectionError(0, "probe.not_found");
        const panel = visibleAll("connectionError")[0];
        verify(panel !== undefined);
        compare(findChild(panel, "errorPrimary").text, "Reconnect");
        const secondary = findChild(panel, "errorSecondary");
        verify(secondary.visible);
        compare(secondary.text, "Choose another probe");
        verify(currentBar("chooseAnotherProbe").visible);
        click(secondary);
        compare(AppViewModel.currentTabViewModel.state, DeviceTabViewModel.ChoosingDevice);
    }

    function test_railAndPages() {
        verify(testSupport.configureTab(0, "app", 801000001, ""));
        const items = findAll(find("navRail"), "railItem").filter(item => item.visible);
        // Console, Flash, Test page, Settings (+ Help).
        compare(items.length, 4);
        compare(items[0].text, "Console");
        compare(items[1].text, "Flash");
        compare(items[3].text, "Settings");
        verify(items[0].selected);
        click(items[1]);
        compare(AppViewModel.currentTabViewModel.currentPage, "flash");
        // Responsive: icons only below 1100 px.
        compare(find("navRail").width, Theme.railWidth);
        window.width = 1099;
        tryCompare(AppViewModel, "railCompact", true);
        compare(find("navRail").width, Theme.railWidthCompact);
        window.width = 1280;
        tryCompare(AppViewModel, "railCompact", false);
    }

    function test_settingsPage() {
        AppViewModel.openGlobalPage("settings");
        const loader = find("globalPage");
        tryVerify(() => loader.item !== null);
        verify(!find("tabStack").visible);
        compare(findChild(loader.item, "placeholderTitle").text, "Settings");
        tryVerify(() => findChild(loader.item, "libraryPath").text.indexOf("fake_jlink") >= 0, 5000);
        verify(findChild(loader.item, "themeSelector") !== null);
        AppViewModel.closeGlobalPage();
        verify(find("tabStack").visible);
    }

    function test_shortcutOverlayFromRail() {
        click(find("railHelp"));
        compare(AppViewModel.shortcutOverlayVisible, true);
        const overlay = findChild(window, "shortcutOverlay");
        tryVerify(() => overlay.opened);
        AppViewModel.shortcutOverlayVisible = false;
        tryVerify(() => !overlay.visible);
    }

    function test_toast() {
        AppViewModel.showToast("Firmware flashed and verified in 14 s.");
        tryVerify(() => findChild(find("toasts"), "toast") !== null);
    }

    name: "Shell"
    when: windowShown
}
