// The Flash page (plan 0006): kind cards, loading a file, the Flash bar, a flash through the fake
// J-Link library with the progress and result cards, the maintenance ConfirmDialog (ADR-0038:
// Cancel is the default and Esc cancels), "Guide me" hidden while the flash wizard is not
// registered, both themes, and the layout at 960 x 600 in Czech (geometry assertions).
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    name: "Flash"
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

    function rect(item) {
        const point = item.mapToItem(window.contentItem, 0, 0);
        return { x: point.x, y: point.y, w: item.width, h: item.height, name: item.objectName };
    }

    // Horizontally inside the window (the page scrolls vertically; with `vertical` also inside
    // the window height, for the sticky Flash bar).
    function checkInside(item, vertical) {
        const r = rect(item);
        verify(r.x >= -0.5 && r.x + r.w <= window.width + 0.5, `${r.name} is clipped: x=${r.x} w=${r.w}`);
        if (vertical)
            verify(r.y >= -0.5 && r.y + r.h <= window.height + 0.5, `${r.name} is clipped: y=${r.y} h=${r.h}`);
        if (item.implicitWidth !== undefined && item.text !== undefined)
            verify(item.width >= item.implicitWidth - 0.5, `${r.name} is narrower than its text`);
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

    function initTestCase() {
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
        window.requestActivate();
        tryVerify(() => window.active);
    }

    function cleanupTestCase() {
        // The tab stays configured: no probe picker (and no probe scan) while the test ends.
        window.destroy();
        wait(50); // the deferred delete runs before the engine goes away
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function init() {
        AppViewModel.setLanguage("en");
        AppViewModel.theme = "light";
        testSupport.resetTabs();
        // Tall enough that every card is on screen for the clicks (the recent list grows).
        window.width = 1280;
        window.height = 1400;
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.showPage("flash");
        tryVerify(() => find("flashPage") !== null);
        // A new tab starts with the kind of its target (ADR-0055; nothing is persisted).
        compare(vm().kind, FirmwareSourceViewModel.AppFirmware);
        // Laid out before the clicks.
        tryVerify(() => find("kindCard0") !== null && find("kindCard0").width > 100, 5000);
        waitForRendering(window.contentItem);
    }

    function vm() {
        return testSupport.flashPage(0);
    }

    function test_kindCardsAndSource() {
        const flash = vm();
        compare(flash.kind, FirmwareSourceViewModel.AppFirmware);
        // "Guide me" is wired but hidden until the flash wizard (0010) registers.
        verify(!find("guideMe"));
        compare(find("flashBarText").text, "Choose a firmware file or enter a catalog ID");
        verify(!find("flashButton").enabled);

        // 0021 (ADR-0174, ADR-0165): the LTE card - the designated LTE firmware by default.
        mouseClick(find("kindCard" + FirmwareSourceViewModel.LteFirmware));
        compare(flash.kind, FirmwareSourceViewModel.LteFirmware);
        verify(!find("catalogIdField")); // no catalog ID on the LTE card
        tryVerify(() => find("prerequisite") !== null, 10000); // nRF Util is missing
        verify(find("modemNote") !== null);
        verify(find("designatedRadio").checked);

        mouseClick(find("kindCard" + FirmwareSourceViewModel.AppFirmware));
        flash.source.loadFile(testSupport.writeHex("app.hex", 4096, false));
        tryVerify(() => flash.source.sourceState === FirmwareSourceViewModel.Ready, 5000);
        verify(find("summaryText").text.startsWith("app.hex · Intel HEX"));
        compare(find("flashBarText").text, "Application firmware · app.hex");
        verify(find("flashButton").enabled);
        verify(find("recentRow0") !== null);
    }

    // Review F3: a click on the selected card keeps it selected.
    function test_clickOnTheSelectedKindKeepsIt() {
        const flash = vm();
        const card = find("kindCard" + FirmwareSourceViewModel.AppFirmware);
        verify(card.checked);
        mouseClick(card);
        compare(flash.kind, FirmwareSourceViewModel.AppFirmware);
        verify(card.checked);
        keyClick(Qt.Key_Space);
        verify(card.checked);
        mouseClick(find("kindCard" + FirmwareSourceViewModel.LteFirmware));
        verify(!card.checked);
        verify(find("kindCard" + FirmwareSourceViewModel.LteFirmware).checked);
    }

    function test_kindHintSwitches() {
        const flash = vm();
        flash.source.loadFile(testSupport.writeModemZip());
        tryVerify(() => find("kindHint") !== null, 5000);
        compare(find("kindHint").Accessible.name, "This is LTE firmware — switch to LTE firmware?");
        mouseClick(find("switchKind"));
        compare(flash.kind, FirmwareSourceViewModel.LteFirmware);
        verify(!find("kindHint"));
        // The file is the LTE card's own file now.
        verify(find("ownFileRadio").checked);
        tryVerify(() => find("summaryText") !== null);
        verify(find("summaryText").text.indexOf("Modem firmware 2.0.2 (modem only)") >= 0);
    }

    // 0021 (ADR-0165): the designated LTE firmware downloads when its row is shown (a local
    // fixture here); "Use your own file…" shows the drop zone and the recent list.
    function test_lteDesignatedFirmware() {
        const flash = vm();
        mouseClick(find("kindCard" + FirmwareSourceViewModel.LteFirmware));
        verify(find("designatedRadio").checked);
        compare(find("designatedRadio").text, "HARDWARIO LTE firmware 1.7.0 (recommended)");
        tryVerify(() => flash.source.designatedState === FirmwareSourceViewModel.DesignatedReady, 10000);
        tryCompare(find("designatedStatusText"), "text", "Ready");
        verify(!find("dropText"));
        compare(find("flashBarText").text, "nRF Util was not found");
        mouseClick(find("ownFileRadio"));
        compare(flash.source.lteSource, FirmwareSourceViewModel.OwnFile);
        verify(find("dropText") !== null);
        verify(!find("designatedStatus"));
        mouseClick(find("designatedRadio"));
        compare(flash.source.lteSource, FirmwareSourceViewModel.Designated);
        verify(!find("dropText"));
    }

    // 0021 (ADR-0167): "Find firmware IDs ↗" is the documentation in the UI language.
    function test_docsLink() {
        const link = find("docsLink");
        verify(link !== null);
        compare(vm().source.docsUrl.toString(),
                "https://docs.hardwario.com/chester/catalog-applications/catalog-applications/#application-firmware");
        AppViewModel.setLanguage("cs");
        tryCompare(link, "text", "Najít ID firmwaru ↗");
        compare(vm().source.docsUrl.toString(),
                "https://docs.hardwario.com/cs/chester/catalog-applications/catalog-applications/#application-firmware");
        AppViewModel.setLanguage("en");
    }

    function test_catalogIdValidator() {
        const field = find("catalogIdField");
        field.forceActiveFocus();
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_X);
        tryVerify(() => find("catalogIdError") !== null);
        compare(find("catalogIdError").text, "The ID has 32 characters 0–9 a–f.");
        field.clear();
    }

    function test_flashShowsProgressAndResult() {
        const flash = vm();
        flash.source.loadFile(testSupport.writeHex("app.hex", 4096, false));
        tryVerify(() => flash.canFlash, 5000);
        keySequence("Ctrl+Return");
        tryVerify(() => find("progressCard") !== null || find("resultCard") !== null, 5000);
        tryVerify(() => flash.opState === FlashViewModel.Succeeded, 30000);
        tryVerify(() => find("resultText") !== null);
        verify(find("resultText").text.startsWith("Firmware flashed and verified in"));
        verify(find("openConsole") !== null);
        // Review F6: no drops on the result card.
        verify(!findChild(find("flashPage"), "flashDropArea").enabled);
        mouseClick(find("flashAgain"));
        verify(findChild(find("flashPage"), "flashDropArea").enabled);
        compare(flash.opState, FlashViewModel.Idle);
        verify(find("flashBar") !== null);
    }

    function test_uicrOption() {
        const flash = vm();
        flash.source.loadFile(testSupport.writeHex("uicr.hex", 64, true));
        tryVerify(() => find("uicrOption") !== null, 5000);
        verify(!find("flashButton").enabled);
        mouseClick(find("uicrOption"));
        verify(flash.source.allowUicr);
        verify(find("flashButton").enabled);
    }

    function test_maintenanceConfirmDialog() {
        const flash = vm();
        verify(!find("maintenanceActions"));
        mouseClick(find("maintenanceHeader"));
        verify(find("maintenanceActions") !== null);
        mouseClick(find("eraseAllButton"));
        const dialog = findChild(find("flashPage"), "maintenanceConfirm");
        tryVerify(() => dialog.opened);
        compare(dialog.title, "Erase everything on the application MCU?");
        compare(dialog.items.length, 3);
        // ADR-0038: Cancel has the focus (the destructive button is not the default); Esc cancels.
        const reject = findChild(dialog, "dialogReject");
        const accept = findChild(dialog, "dialogAccept");
        tryVerify(() => reject.activeFocus);
        compare(accept.kind, "danger");
        compare(accept.text, "Erase everything");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
        compare(flash.opState, FlashViewModel.Idle);
        // Recover asks too.
        mouseClick(find("recoverButton"));
        tryVerify(() => dialog.opened);
        compare(dialog.title, "Recover the application MCU?");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
    }

    // Review F1/F2: the LTE MCU has no code-only erase; its erase-all and recover name the PIB.
    function test_lteMaintenance() {
        const flash = vm();
        mouseClick(find("maintenanceHeader"));
        verify(find("eraseCodeButton") !== null);
        flash.kind = FirmwareSourceViewModel.LteFirmware;
        tryVerify(() => find("eraseCodeButton") === null);
        mouseClick(find("eraseAllButton"));
        const dialog = findChild(find("flashPage"), "maintenanceConfirm");
        tryVerify(() => dialog.opened);
        compare(dialog.title, "Erase everything on the LTE MCU?");
        compare(dialog.items.length, 3);
        verify(dialog.items[2].startsWith("Product Information Block"));
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
        mouseClick(find("recoverButton"));
        tryVerify(() => dialog.opened);
        compare(dialog.title, "Recover the LTE MCU?");
        compare(dialog.items.length, 3);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
        compare(flash.opState, FlashViewModel.Idle);
    }

    function test_connectorWarning() {
        const flash = vm();
        verify(!find("connectorWarning"));
        flash.kind = FirmwareSourceViewModel.LteFirmware;
        tryVerify(() => find("connectorWarning") !== null);
        compare(find("maintenanceHeader").Accessible.name, "Maintenance — LTE MCU");
    }

    function test_themes_data() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function test_themes(data) {
        AppViewModel.theme = data.theme;
        compare(Theme.dark, data.theme === "dark");
        verify(find("flashBar") !== null);
        verify(find("kindCard0") !== null);
    }

    function test_layoutCzech960() {
        AppViewModel.setLanguage("cs");
        window.width = 960;
        window.height = 600;
        const flash = vm();
        flash.source.loadFile(testSupport.writeHex("app.hex", 4096, true));
        tryVerify(() => flash.source.sourceState === FirmwareSourceViewModel.Ready, 5000);
        find("maintenance").expanded = true; // below the fold at 600 px
        tryVerify(() => find("maintenanceActions") !== null);
        waitForRendering(window.contentItem);
        checkInside(find("flashButton"), true);
        for (const name of ["browseButton", "downloadButton", "clearSource"])
            checkInside(find(name), false);
        checkNoOverlap(["flashButton", "flashBarText"].map(find));
        const buttons = ["resetButton", "resetHaltButton", "eraseCodeButton", "eraseAllButton", "recoverButton"].map(find);
        checkNoOverlap(buttons);
        for (const button of buttons)
            verify(button.width >= button.implicitWidth - 0.5, `${button.objectName} is narrower than its text`);
        const cards = [0, 1].map(kind => find("kindCard" + kind));
        checkNoOverlap(cards);
        for (const card of cards)
            checkInside(card, false);
    }
}
