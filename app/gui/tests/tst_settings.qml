// The Settings page (plan 0006): the sections in registry order, live theme, the J-Link default
// speed with "Automatic speed" (ADR-0111) and its slow-speed warning (ADR-0110), the detected
// J-Link library, nRF Util "not found", "Run the setup wizard" hidden while the setup wizard is
// not registered, both themes, and the layout at 960 x 600 in Czech.
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    name: "Settings"
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

    function initTestCase() {
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
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
        // A configured tab: the probe picker does not scan behind the Settings page.
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        window.width = 1280;
        window.height = 800;
        AppViewModel.openGlobalPage("settings");
        tryVerify(() => find("settingsPage") !== null);
        tryVerify(() => find("defaultAutoSpeed") !== null && find("defaultAutoSpeed").width > 0, 5000);
        waitForRendering(window.contentItem);
    }

    function test_sectionsAndDetection() {
        const sections = findAll(window.contentItem, "sectionLoader").filter(effectivelyVisible);
        compare(sections.length, 5); // General, Console (0005), J-Link, nRF Util, About
        verify(find("section_general") !== null);
        verify(find("section_console") !== null);
        verify(find("section_about") !== null);
        verify(!find("readOnlyBanner"));
        tryVerify(() => find("jlinkDetected").text.indexOf("fake_jlink") >= 0, 10000);
        tryCompare(find("nrfUtilDetected"), "text", "Not found — needed only for LTE modem firmware", 10000);
        // Wired, hidden until the setup wizard (0008) registers.
        verify(!find("jlinkSetupWizard"));
        verify(!find("nrfUtilSetupWizard"));
        verify(find("aboutText").text.indexOf("HARDWARIO Monitor") === 0);
    }

    function test_themeIsLive() {
        const box = find("themeSelector");
        box.activated(2);
        compare(AppViewModel.theme, "dark");
        compare(Theme.dark, true);
        compare(box.currentIndex, 2);
    }

    function test_languageIsLive() {
        const box = find("languageSelector");
        const vm = AppViewModel.globalPageViewModel("settings");
        let czech = -1;
        for (let i = 0; i < vm.languages.length; ++i) {
            if (vm.languages[i].code === "cs")
                czech = i;
        }
        verify(czech > 0);
        box.activated(czech);
        compare(AppViewModel.language, "cs");
        tryCompare(find("settingsTitle"), "text", "Nastavení");
        compare(box.currentIndex, czech);
    }

    function test_defaultSpeed() {
        const vm = AppViewModel.globalPageViewModel("settings");
        // 0021 (ADR-0172): "Open Settings" scrolls to the section.
        AppViewModel.openSettings("jlink");
        const automatic = find("defaultAutoSpeed");
        tryVerify(() => automatic.mapToItem(window.contentItem, 0, 0).y + automatic.height < window.height);
        waitForRendering(window.contentItem);
        const speedBox = find("defaultSpeedBox");
        verify(automatic.checked);
        verify(!speedBox.enabled);
        mouseClick(automatic);
        verify(!vm.autoSpeed);
        verify(speedBox.enabled);
        vm.defaultSpeedKhz = 100;
        tryVerify(() => find("defaultSpeedWarning") !== null);
        mouseClick(automatic);
        verify(vm.autoSpeed);
        compare(speedBox.value, 100); // kept for later
        verify(!find("defaultSpeedWarning"));
    }

    function test_themes_data() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function test_themes(data) {
        AppViewModel.theme = data.theme;
        compare(Theme.dark, data.theme === "dark");
        verify(find("settingsTitle") !== null);
    }

    function test_layoutCzech960() {
        AppViewModel.setLanguage("cs");
        window.width = 960;
        window.height = 600;
        waitForRendering(window.contentItem);
        for (const name of ["languageSelector", "themeSelector", "defaultSpeedBox", "defaultLatencyBox", "copyAbout"]) {
            const item = find(name);
            verify(item !== null, name);
            const r = rect(item);
            verify(r.x >= -0.5 && r.x + r.w <= window.width + 0.5, `${name} is clipped: x=${r.x} w=${r.w}`);
        }
        compare(find("settingsTitle").text, "Nastavení");
    }
}
