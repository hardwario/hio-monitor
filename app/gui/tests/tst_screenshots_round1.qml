// Screenshots of plan 0021 (GUI feedback round 1) at 960 x 600 in Czech, light and dark: the
// Flash page with both kinds (catalog info and the docs link; the designated LTE firmware and an
// own file), the Settings tab, and the console with the suggestion list and the H chip. Only when
// HIO_SCREENSHOT_DIR is set (QT_QUICK_BACKEND=software for the offscreen platform).
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    name: "Round1Screenshots"
    when: windowShown

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

    function find(name) {
        const items = findAll(window.contentItem, name).filter(effectivelyVisible);
        return items.length > 0 ? items[0] : null;
    }

    function initTestCase() {
        if (testSupport.screenshotDir === "")
            skip("HIO_SCREENSHOT_DIR is not set");
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
        window.requestActivate();
    }

    function cleanupTestCase() {
        if (window) {
            testSupport.resetTabs();
            window.destroy();
        }
        wait(50);
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function save(name) {
        waitForRendering(window.contentItem, 2000);
        wait(250);
        const image = grabImage(window.contentItem);
        verify(image.width > 0, name);
        image.save(testSupport.screenshotDir + "/" + name + "-" + AppViewModel.theme + "-" + AppViewModel.language + ".png");
    }

    // Scrolls the page so that `name` is near the top (960 x 600 shows only part of the page).
    function scrollTo(name) {
        const item = find(name);
        verify(item !== null, name);
        let flickable = item.parent;
        while (flickable && (flickable.contentY === undefined || flickable.contentItem === undefined))
            flickable = flickable.parent;
        verify(flickable !== null, name);
        const y = item.mapToItem(flickable.contentItem, 0, 0).y;
        flickable.contentY = Math.max(0, Math.min(y - 60, flickable.contentHeight - flickable.height));
    }

    function freshTab(target) {
        testSupport.resetTabs();
        window.width = 960;
        window.height = 600;
        verify(testSupport.configureTab(0, target, 801000001, "Test Probe"));
    }

    function test_shots_data() {
        return [{ tag: "light-cs", theme: "light", language: "cs" }, { tag: "dark-cs", theme: "dark", language: "cs" }];
    }

    function test_shots(data) {
        if (testSupport.screenshotDir === "")
            skip("HIO_SCREENSHOT_DIR is not set");
        AppViewModel.theme = data.theme;
        AppViewModel.setLanguage(data.language);

        // Flash, application firmware: a catalog ID with what the catalog page says.
        freshTab("app");
        AppViewModel.showPage("flash");
        tryVerify(() => find("flashPage") !== null);
        let vm = testSupport.flashPage(0);
        vm.kind = FirmwareSourceViewModel.AppFirmware;
        tryVerify(() => find("catalogIdField") !== null);
        const field = find("catalogIdField");
        field.text = "001543e218a04ffdbf65082532202736";
        vm.source.checkCatalogId(field.text);
        tryVerify(() => vm.source.catalogInfo.lookupState === "found", 10000);
        scrollTo("catalogIdField");
        save("r01-flash-app-catalog-info");
        field.text = "ffffffffffffffffffffffffffffffff";
        vm.source.checkCatalogId(field.text);
        tryVerify(() => vm.source.catalogInfo.lookupState === "unknown", 10000);
        save("r02-flash-app-unknown-firmware");
        field.text = "";
        vm.source.checkCatalogId("");

        // Flash, LTE firmware: the designated firmware (ready), then an own file.
        freshTab("lte");
        AppViewModel.showPage("flash");
        tryVerify(() => find("flashPage") !== null);
        vm = testSupport.flashPage(0);
        vm.kind = FirmwareSourceViewModel.LteFirmware;
        vm.source.lteSource = FirmwareSourceViewModel.Designated;
        tryVerify(() => vm.source.designatedState === FirmwareSourceViewModel.DesignatedReady, 10000);
        scrollTo("lteSources");
        save("r03-flash-lte-designated");
        vm.source.loadFile(testSupport.writeBundle());
        tryVerify(() => vm.source.sourceState === FirmwareSourceViewModel.Ready, 10000);
        scrollTo("lteSources");
        save("r04-flash-lte-own-file");
        vm.source.lteSource = FirmwareSourceViewModel.Designated;

        // The Settings tab (gear, no rail, no device bar), scrolled to the Console section.
        AppViewModel.openSettings("console");
        tryVerify(() => find("settingsPage") !== null);
        tryVerify(() => find("consoleSuggestions") !== null);
        save("r05-settings-tab");
        AppViewModel.closeSettings();

        // Console: the suggestion list and the ghost text; the H chip (off, then on).
        freshTab("app");
        AppViewModel.showPage("console");
        tryVerify(() => find("consolePage") !== null);
        AppViewModel.currentTabViewModel.connection.connectDevice();
        tryCompare(AppViewModel.currentTabViewModel, "state", DeviceTabViewModel.Connected, 10000);
        const consoleVm = testSupport.consoleViewModel(0);
        tryVerify(() => consoleVm.inputEnabled);
        testSupport.appendConsoleLines(0, 1, 1, "[00:00:12.345,678] <inf> app_lrw: Received:", 2, 0);
        testSupport.appendConsoleLines(0, 1, 3, "                                 44 74 4f 34 78 54 73 31  4b 36 69 41 73 41 53 4a |DtO4xTs1 K6iAsASJ", 5, 0);
        testSupport.appendConsoleLines(0, 1, 1, "[00:00:12.346,000] <wrn> ctr_lte_v2: RSRP low: -112 dBm", 3, 0);
        testSupport.addConsoleHistory(0, "lte state");
        testSupport.addConsoleHistory(0, "lte config show");
        testSupport.setConsoleCommands(0, ["lte", "led", "lrw", "app", "info"]);
        const input = find("commandInput");
        input.forceActiveFocus();
        input.text = "";
        keyClick(Qt.Key_L);
        tryVerify(() => findAll(window.contentItem, "shellInput")[0].suggestionList.opened);
        save("r06-console-suggestions");
        keyClick(Qt.Key_Escape);
        input.text = "";
        consoleVm.log.levelMask = consoleVm.log.levelMask | 16;
        tryVerify(() => consoleVm.log.visibleLines === consoleVm.log.totalLines);
        save("r07-console-hexdump-shown");
        consoleVm.log.clearFilter();
        save("r08-console-hexdump-hidden");
    }
}
