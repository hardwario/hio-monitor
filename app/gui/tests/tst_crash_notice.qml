// The crash notice in the shell banner area (plan/features/0015 B3, ADR-0123): hidden without a
// crash marker; after a crash shown in English and Czech; [Dismiss] and [Create diagnostic report]
// both acknowledge the crash, the latter opens the report dialog.
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    function click(item) {
        mouseClick(item, item.width / 2, item.height / 2, Qt.LeftButton, Qt.NoModifier,
                   Qt.styleHints.mouseDoubleClickInterval + 50);
    }

    function find(name) {
        return findChild(window.contentItem, name);
    }

    function findButton(parent, label) {
        if (!parent)
            return null;
        const children = parent.children ? parent.children : [];
        for (let i = 0; i < children.length; ++i) {
            if (children[i].text === label && children[i].visible)
                return children[i];
            const found = findButton(children[i], label);
            if (found)
                return found;
        }
        return null;
    }

    function init() {
        AppViewModel.setLanguage("en");
        testSupport.resetTabs();
        AppViewModel.dismissCrashNotice();
        waitForRendering(window.contentItem);
    }

    function test_hiddenWithoutACrash() {
        const notice = find("crashNotice");
        verify(notice);
        verify(!notice.visible);
        verify(!AppViewModel.crashedLastRun);
    }

    function test_dismiss() {
        testSupport.simulateCrash();
        const notice = find("crashNotice");
        tryVerify(() => notice.visible);
        compare(findChild(notice, "bannerText").text,
                "HARDWARIO Monitor quit unexpectedly last time.");
        click(findChild(notice, "bannerDismiss"));
        tryVerify(() => !notice.visible);
        verify(!testSupport.crashMarkerExists());
        verify(!AppViewModel.crashedLastRun);
    }

    function test_createReport() {
        testSupport.simulateCrash();
        const notice = find("crashNotice");
        tryVerify(() => notice.visible);
        const action = findButton(notice, "Create diagnostic report");
        verify(action);
        click(action);
        tryVerify(() => AppViewModel.report.open);
        verify(!notice.visible);
        verify(!testSupport.crashMarkerExists());
        AppViewModel.report.close();
        tryVerify(() => !AppViewModel.report.open);
    }

    function test_czech() {
        testSupport.simulateCrash();
        AppViewModel.setLanguage("cs");
        const notice = find("crashNotice");
        tryVerify(() => notice.visible);
        const text = findChild(notice, "bannerText").text;
        verify(text !== "HARDWARIO Monitor quit unexpectedly last time.", text);
        verify(text.length > 0);
        AppViewModel.setLanguage("en");
    }

    function initTestCase() {
        AppViewModel.setLanguage("en");
        testSupport.resetTabs();
        const component = Qt.createComponent("HardwarioMonitor", "Main");
        verify(component.status === Component.Ready, component.errorString());
        window = component.createObject(testCase);
        verify(window !== null);
        waitForRendering(window.contentItem);
    }

    function cleanupTestCase() {
        window.destroy();
    }
}
