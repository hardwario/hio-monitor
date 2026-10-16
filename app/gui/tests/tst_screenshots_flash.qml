// Screenshots of the Flash and Settings pages (plan 0006, E2) in both themes and both languages;
// only when HIO_SCREENSHOT_DIR is set (QT_QUICK_BACKEND=software for the offscreen platform).
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    name: "FlashScreenshots"
    when: windowShown

    readonly property string slowScenario: '{"libraryVersion": 81000, "probes": [{"serialNumber": 801000001, "product": "Test Probe", "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\\n"}]}, "chip": {"readDelayMs": 400}}]}'
    readonly property string faultScenario: '{"libraryVersion": 81000, "probes": [{"serialNumber": 801000001, "product": "Test Probe", "chip": {"writeFault": true}}]}'
    readonly property string normalScenario: '{"libraryVersion": 81000, "probes": [{"serialNumber": 801000001, "product": "Test Probe", "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted\\n"}]}}, {"serialNumber": 801000002, "product": "Test Probe 2"}]}'

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
        wait(50); // the deferred delete runs before the engine goes away
        testSupport.useScenario(normalScenario);
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

    function freshTab(target, kind) {
        testSupport.resetTabs();
        window.width = 1280;
        window.height = 800;
        verify(testSupport.configureTab(0, target, 801000001, "Test Probe"));
        AppViewModel.showPage("flash");
        tryVerify(() => find("flashPage") !== null);
        const vm = testSupport.flashPage(0);
        vm.kind = kind;
        vm.source.clear();
        tryVerify(() => find("kindCard0") !== null && find("kindCard0").width > 100, 5000);
        return vm;
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
        testSupport.useScenario(normalScenario);

        // Kind selection (nothing loaded).
        let vm = freshTab("app", FirmwareSourceViewModel.AppFirmware);
        save("f01-flash-kind");

        // Application hex loaded.
        vm.source.loadFile(testSupport.writeHex("chester-clime-v3.2.1.hex", 412000, false));
        tryVerify(() => vm.source.sourceState === FirmwareSourceViewModel.Ready, 10000);
        save("f02-flash-app-hex");

        // Catalog ID with the inline validator.
        vm.source.clear();
        const field = find("catalogIdField");
        field.text = "3f2a9c1b";
        vm.source.checkCatalogId(field.text);
        save("f03-flash-catalog-id");
        field.text = "";
        vm.source.checkCatalogId("");

        // Wrong connector (ADR-0057 Phase 0): LTE firmware on a tab of the application MCU.
        vm.kind = FirmwareSourceViewModel.LteFirmware;
        save("f04-flash-wrong-connector");

        // LTE bundle on an LTE tab, nRF Util missing (prerequisite).
        vm = freshTab("lte", FirmwareSourceViewModel.LteFirmware);
        vm.source.loadFile(testSupport.writeBundle());
        tryVerify(() => vm.source.sourceState === FirmwareSourceViewModel.Ready, 10000);
        tryVerify(() => vm.prerequisiteText !== "", 10000);
        save("f05-flash-lte-bundle");

        // Modem firmware, nRF Util missing.
        vm.kind = FirmwareSourceViewModel.ModemFirmware;
        vm.source.loadFile(testSupport.writeModemZip());
        tryVerify(() => vm.source.sourceState === FirmwareSourceViewModel.Ready, 10000);
        save("f06-flash-modem-nrfutil-missing");

        // Progress (slow verify), then cancel.
        testSupport.useScenario(slowScenario);
        vm = freshTab("app", FirmwareSourceViewModel.AppFirmware);
        vm.source.loadFile(testSupport.writeHex("chester-clime-v3.2.1.hex", 128 * 1024, false));
        tryVerify(() => vm.canFlash, 10000);
        vm.flash();
        tryVerify(() => vm.progressText.indexOf("%") >= 0, 30000);
        save("f07-flash-progress");
        vm.cancel();
        save("f08-flash-cancelling");
        tryVerify(() => vm.opState === FlashViewModel.Cancelled, 60000);
        save("f09-flash-cancelled");

        // Success.
        testSupport.useScenario(normalScenario);
        vm = freshTab("app", FirmwareSourceViewModel.AppFirmware);
        vm.source.loadFile(testSupport.writeHex("chester-clime-v3.2.1.hex", 4096, false));
        tryVerify(() => vm.canFlash, 10000);
        vm.flash();
        tryVerify(() => vm.opState === FlashViewModel.Succeeded, 30000);
        save("f10-flash-success");

        // Failure: an interrupted flash (ADR-0109).
        testSupport.useScenario(faultScenario);
        vm = freshTab("app", FirmwareSourceViewModel.AppFirmware);
        vm.source.loadFile(testSupport.writeHex("chester-clime-v3.2.1.hex", 4096, false));
        tryVerify(() => vm.canFlash, 10000);
        vm.flash();
        tryVerify(() => vm.opState === FlashViewModel.Failed, 30000);
        save("f11-flash-failed-interrupted");

        // Maintenance and the erase-all confirmation (ADR-0038).
        testSupport.useScenario(normalScenario);
        vm = freshTab("app", FirmwareSourceViewModel.AppFirmware);
        window.height = 900;
        find("maintenance").expanded = true;
        tryVerify(() => find("maintenanceActions") !== null);
        save("f12-flash-maintenance");
        vm.requestConfirmation(false);
        const dialog = findChild(find("flashPage"), "maintenanceConfirm");
        tryVerify(() => dialog.opened);
        save("f13-flash-confirm-erase-all");
        dialog.reject();
        tryVerify(() => !dialog.visible);
        vm.kind = FirmwareSourceViewModel.LteFirmware;
        vm.requestConfirmation(true);
        tryVerify(() => dialog.opened);
        save("f14-flash-confirm-recover-lte");
        dialog.reject();
        tryVerify(() => !dialog.visible);
        // The LTE MCU: no code-only erase; erase everything names the PIB (review F1/F2).
        save("f15-flash-maintenance-lte");
        vm.requestConfirmation(false);
        tryVerify(() => dialog.opened);
        save("f16-flash-confirm-erase-all-lte");
        dialog.reject();
        tryVerify(() => !dialog.visible);

        // Settings: the whole page.
        testSupport.resetTabs();
        window.width = 1280;
        window.height = 1500;
        AppViewModel.openGlobalPage("settings");
        tryVerify(() => find("jlinkDetected") !== null && find("jlinkDetected").text.indexOf("fake_jlink") >= 0, 10000);
        tryVerify(() => find("nrfUtilDetected") !== null && find("nrfUtilDetected").text !== "", 10000);
        save("s01-settings");
        // Fixed slow default speed (ADR-0110 warning) and a custom J-Link path.
        const settings = AppViewModel.globalPageViewModel("settings");
        settings.autoSpeed = false;
        settings.defaultSpeedKhz = 100;
        settings.jlinkLibraryMode = "custom";
        window.height = 800;
        save("s02-settings-jlink-custom-slow");
        settings.jlinkLibraryMode = "auto";
        settings.autoSpeed = true;
        window.width = 960;
        window.height = 600;
        save("s03-settings-960x600");
        AppViewModel.closeGlobalPage();
    }
}
