// The Help menu at the rail's Help item (plan/features/0007): Keyboard shortcuts (F1), the wizards
// that are registered (Setup, Connect a device, Troubleshoot connection, Flash firmware), Create
// diagnostic report and Documentation ↗. Items whose wizard is not registered are hidden.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import HardwarioMonitor

Menu {
    id: menu

    objectName: "helpMenu"

    component HelpItem: MenuItem {
        id: helpItem

        // Hidden items take no space.
        height: helpItem.visible ? implicitHeight : 0
    }

    HelpItem {
        objectName: "helpShortcuts"
        text: qsTr("Keyboard shortcuts (F1)")
        onTriggered: AppViewModel.shortcutOverlayVisible = true
    }

    HelpItem {
        objectName: "helpSetup"
        visible: AppViewModel.launcher.canOpen("first-run-setup")
        text: qsTr("Setup wizard…")
        onTriggered: AppViewModel.launcher.start("first-run-setup", -1)
    }

    HelpItem {
        objectName: "helpConnect"
        visible: AppViewModel.launcher.canOpen("connect-device")
        text: qsTr("Connect a device…")
        onTriggered: AppViewModel.launcher.start("connect-device", -1)
    }

    HelpItem {
        objectName: "helpTroubleshoot"
        visible: AppViewModel.launcher.canOpen("troubleshooting")
        text: qsTr("Troubleshoot connection…")
        onTriggered: AppViewModel.launcher.start("troubleshooting", -1)
    }

    HelpItem {
        objectName: "helpFlash"
        visible: AppViewModel.launcher.canOpen("flash-firmware")
        text: qsTr("Flash firmware (guided)…")
        onTriggered: AppViewModel.launcher.start("flash-firmware", -1)
    }

    MenuSeparator {}

    HelpItem {
        objectName: "helpReport"
        text: qsTr("Create diagnostic report…")
        onTriggered: AppViewModel.report.show()
    }

    HelpItem {
        objectName: "helpDocumentation"
        text: qsTr("Documentation ↗")
        onTriggered: AppViewModel.openDocumentation()
    }
}
