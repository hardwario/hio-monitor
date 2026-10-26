// The update check in the GUI (plan/features/0016 C2, ADR-0116): the banner after an automatic
// check ([Skip this version], [Later], one banner at a time with the crash notice first), Settings
// > About (automatic check and pre-release options, [Check now], the CLI path, the licence files),
// Czech texts. The release list comes from the local fixture (HIO_UPDATE_URL, tst_gui.cpp).
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    name: "Update"
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

    function click(item) {
        mouseClick(item, item.width / 2, item.height / 2, Qt.LeftButton, Qt.NoModifier,
                   Qt.styleHints.mouseDoubleClickInterval + 50);
    }

    function initTestCase() {
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        window.destroy();
        wait(50);
        AppViewModel.setLanguage("en");
    }

    function init() {
        AppViewModel.setLanguage("en");
        testSupport.resetTabs();
        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.dismissCrashNotice();
        testSupport.resetUpdate();
        AppViewModel.closeSettings();
        waitForRendering(window.contentItem);
    }

    function test_bannerAndSkip() {
        verify(!find("updateBanner"));
        testSupport.checkForUpdatesAutomatically();
        tryVerify(() => find("updateBanner") !== null, 10000);
        compare(find("updateBannerText").text, "HARDWARIO Monitor 2.999.0 is available (you have "
                + AppViewModel.update.currentVersion + ").");
        click(find("updateSkip"));
        tryVerify(() => find("updateBanner") === null);
        compare(testSupport.setting("update/skippedVersion"), "2.999.0");
        // The next automatic check does not announce the skipped version again.
        testSupport.checkForUpdatesAutomatically();
        tryVerify(() => AppViewModel.update.state !== UpdateViewModel.Checking);
        wait(200);
        verify(find("updateBanner") === null);
    }

    function test_later() {
        testSupport.checkForUpdatesAutomatically();
        tryVerify(() => find("updateBanner") !== null, 10000);
        click(find("updateLater"));
        tryVerify(() => find("updateBanner") === null);
        compare(testSupport.setting("update/skippedVersion"), "");
    }

    function test_crashNoticeFirst() {
        testSupport.checkForUpdatesAutomatically();
        tryVerify(() => find("updateBanner") !== null, 10000);
        testSupport.simulateCrash();
        tryVerify(() => find("crashNotice") !== null);
        verify(find("updateBanner") === null);
        AppViewModel.dismissCrashNotice();
        tryVerify(() => find("updateBanner") !== null);
    }

    function test_about() {
        AppViewModel.openSettings("about");
        tryVerify(() => find("updateCheckNow") !== null, 5000);
        verify(find("aboutText").text.indexOf("HARDWARIO Monitor " + AppViewModel.version + " (dev)") === 0,
               find("aboutText").text);
        verify(find("updateAutoCheck").checked);
        verify(!find("updatePrerelease").checked);
        // The About section may be below the visible part of the page: the buttons are activated
        // directly.
        find("updateCheckNow").clicked();
        tryVerify(() => AppViewModel.update.state === UpdateViewModel.Available, 10000);
        compare(find("updateStatus").text, "Last checked: " + AppViewModel.update.lastChecked
                + " — Version 2.999.0 is available.");
        verify(find("updateDownloadAbout") !== null);
        verify(find("updateBanner") === null); // manual checks show the result here only
        // Pre-releases.
        find("updatePrerelease").toggle();
        find("updatePrerelease").toggled();
        compare(testSupport.setting("update/includePrerelease"), "true");
        find("updateCheckNow").clicked();
        tryVerify(() => find("updateStatus").text.indexOf("Version 2.999.1-rc.1 is available.") > 0, 10000);
        find("updateAutoCheck").toggle();
        find("updateAutoCheck").toggled();
        compare(testSupport.setting("update/autoCheck"), "false");
        // The command line of this (development) build and the licence files.
        verify(find("cliPath").text.length > 0);
        verify(find("cliPath").text.indexOf("hio") >= 0);
        const licences = findAll(find("licenceFiles"), "").filter(item => item.text !== undefined
                                                                    && item.enabled !== undefined
                                                                    && item.hasOwnProperty("kind"));
        compare(licences.length, 10);
        for (let i = 0; i < licences.length; ++i)
            verify(licences[i].enabled, licences[i].text + " has no file");
    }

    function test_czech() {
        AppViewModel.setLanguage("cs");
        testSupport.checkForUpdatesAutomatically();
        tryVerify(() => find("updateBanner") !== null, 10000);
        const text = find("updateBannerText").text;
        verify(text.indexOf("2.999.0") >= 0, text);
        verify(text.indexOf("is available") < 0, text);
        AppViewModel.setLanguage("en");
    }
}
