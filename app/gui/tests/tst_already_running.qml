// The "already running but does not respond" dialog (ADR-0058).
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    function test_startAnyway() {
        const component = Qt.createComponent("HardwarioMonitor", "AlreadyRunningDialog");
        verify(component.status === Component.Ready, component.errorString());
        const dialog = component.createObject(null);
        waitForRendering(dialog.contentItem);
        compare(findChild(dialog.contentItem, "alreadyRunningText").text,
                "HARDWARIO Monitor is already running but does not respond.");
        compare(findChild(dialog.contentItem, "quitButton").text, "Quit");
        mouseClick(findChild(dialog.contentItem, "startAnywayButton"));
        compare(dialog.startAnyway, true);
        tryCompare(dialog, "visible", false);
        dialog.destroy();
        wait(50);
    }

    function test_quit() {
        const dialog = Qt.createComponent("HardwarioMonitor", "AlreadyRunningDialog").createObject(null);
        waitForRendering(dialog.contentItem);
        mouseClick(findChild(dialog.contentItem, "quitButton"));
        compare(dialog.startAnyway, false);
        tryCompare(dialog, "visible", false);
        dialog.destroy();
        wait(50);
    }

    name: "AlreadyRunning"
    when: windowShown
}
