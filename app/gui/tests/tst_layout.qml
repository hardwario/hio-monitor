// Responsive layout (ADR-0061): at 960 x 600 and at 1099 px width, in Czech, no button of the
// shell overlaps another or is clipped (geometry assertions).
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

    function visibleItems(name) {
        return findAll(window.contentItem, name).filter(item => item.visible && item.width > 0);
    }

    function effectivelyVisible(item) {
        for (let current = item; current; current = current.parent) {
            if (!current.visible)
                return false;
        }
        return true;
    }

    function rect(item) {
        const point = item.mapToItem(window.contentItem, 0, 0);
        return { x: point.x, y: point.y, w: item.width, h: item.height, name: item.objectName };
    }

    function checkInside(items) {
        for (const item of items) {
            const r = rect(item);
            verify(r.x >= -0.5 && r.x + r.w <= window.width + 0.5, `${r.name} is clipped: x=${r.x} w=${r.w}`);
            verify(r.y >= -0.5 && r.y + r.h <= window.height + 0.5, `${r.name} is clipped: y=${r.y} h=${r.h}`);
            if (item.implicitWidth !== undefined && item.text !== undefined && item.kind !== undefined)
                verify(item.width >= item.implicitWidth - 0.5, `${r.name} is narrower than its text`);
        }
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

    function deviceBarItems() {
        const names = ["probeChip", "targetBox", "stateText", "chooseAnotherProbe", "connectionAction", "advancedButton"];
        let items = [];
        for (const name of names)
            items = items.concat(findAll(window.contentItem, name).filter(effectivelyVisible));
        return items;
    }

    function sizes_data() {
        return [{ tag: "960x600", width: 960, height: 600 }, { tag: "1099x700", width: 1099, height: 700 }];
    }

    function initTestCase() {
        AppViewModel.setLanguage("cs");
        testSupport.resetTabs();
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        testSupport.resetTabs();
        AppViewModel.setLanguage("en");
        window.destroy();
        wait(50); // the deferred delete runs before the engine goes away
    }

    function init() {
        testSupport.resetTabs();
    }

    function test_deviceBar_data() {
        return sizes_data();
    }

    function test_deviceBar(data) {
        window.width = data.width;
        window.height = data.height;
        verify(testSupport.configureTab(0, "app", 801000001, "J-Link EDU Mini"));
        testSupport.setConnectionError(0, "probe.not_found");
        waitForRendering(window.contentItem);
        compare(AppViewModel.railCompact, true);
        const items = deviceBarItems();
        verify(items.length >= 5, items.length);
        checkInside(items);
        checkNoOverlap(items);
        // The ErrorPanel at a small height: headline + primary action, inside the window.
        const compact = findAll(window.contentItem, "errorPrimaryCompact").filter(effectivelyVisible);
        if (data.height < 700)
            compare(compact.length, 1);
        checkInside(findAll(window.contentItem, "errorPrimary").filter(effectivelyVisible).concat(compact));
        checkInside(visibleItems("tabAdd"));
        checkInside(findAll(window.contentItem, "railItem").filter(effectivelyVisible));
        checkNoOverlap(findAll(window.contentItem, "railItem").filter(effectivelyVisible));
    }

    function test_chooseDevice_data() {
        return sizes_data();
    }

    function test_chooseDevice(data) {
        window.width = data.width;
        window.height = data.height;
        tryVerify(() => findAll(window.contentItem, "probeRow").filter(effectivelyVisible).length === 2, 5000);
        waitForRendering(window.contentItem);
        const buttons = findAll(window.contentItem, "targetButton").filter(effectivelyVisible)
                        .concat(findAll(window.contentItem, "connectButton").filter(effectivelyVisible))
                        .concat(findAll(window.contentItem, "profileBox").filter(effectivelyVisible));
        verify(buttons.length >= 4, buttons.length);
        checkInside(buttons);
        checkNoOverlap(buttons);
        checkInside(findAll(window.contentItem, "probeRow").filter(effectivelyVisible));
    }

    function test_manyTabs_data() {
        return sizes_data();
    }

    function test_manyTabs(data) {
        window.width = data.width;
        window.height = data.height;
        for (let i = 1; i < AppViewModel.maxTabs; ++i)
            AppViewModel.newTab();
        waitForRendering(window.contentItem);
        const tabs = findAll(window.contentItem, "deviceTab").filter(effectivelyVisible);
        compare(tabs.length, 8);
        checkInside(tabs.concat(visibleItems("tabAdd")));
        checkNoOverlap(tabs.concat(visibleItems("tabAdd")));
    }

    name: "Layout"
    when: windowShown
}
