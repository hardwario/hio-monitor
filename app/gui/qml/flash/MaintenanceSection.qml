// Maintenance of the MCU of the selected kind (collapsible, collapsed by default; ADR-0049):
// [Reset] [Reset and halt] [Erase application] - no confirmation; [Erase everything…] and
// [Recover…] - ConfirmDialog (ADR-0038) with the texts of the view model. [Erase application]
// exists only where the profile erases code only by default (not on the LTE MCU).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

FlashCard {
    id: card

    required property FlashViewModel vm
    property bool expanded: false

    signal confirmRequested(bool recover)

    RowLayout {
        Layout.fillWidth: true

        AbstractButton {
            id: header

            objectName: "maintenanceHeader"
            Layout.fillWidth: true
            focusPolicy: Qt.StrongFocus
            checkable: true
            checked: card.expanded
            Accessible.role: Accessible.Button
            Accessible.name: card.vm ? card.vm.maintenanceTitle : ""
            Accessible.description: card.expanded ? qsTr("Collapse") : qsTr("Expand")
            onToggled: card.expanded = header.checked

            contentItem: RowLayout {
                spacing: Theme.spacingS

                Label {
                    Layout.fillWidth: true
                    text: card.vm ? card.vm.maintenanceTitle : ""
                    font: Theme.fontUiStrong
                    color: Theme.text
                    wrapMode: Text.Wrap
                }

                Icon {
                    symbol: card.expanded ? "collapse" : "expand"
                    color: Theme.text
                }
            }

            background: Item {
                FocusRing {
                    control: header
                    radiusOffset: Theme.radiusS
                }
            }
        }
    }

    Flow {
        objectName: "maintenanceActions"
        Layout.fillWidth: true
        visible: card.expanded
        spacing: Theme.spacingS

        HButton {
            objectName: "resetButton"
            text: qsTr("Reset")
            enabled: card.vm ? card.vm.canRunMaintenance : false
            onClicked: card.vm.reset(false)
        }

        HButton {
            objectName: "resetHaltButton"
            text: qsTr("Reset and halt")
            enabled: card.vm ? card.vm.canRunMaintenance : false
            onClicked: card.vm.reset(true)
        }

        HButton {
            objectName: "eraseCodeButton"
            visible: card.vm ? card.vm.eraseCodeAvailable : false
            text: qsTr("Erase application")
            toolTipText: qsTr("Erases the firmware only; the UICR configuration and the Product Information Block are kept.")
            enabled: card.vm ? card.vm.canRunMaintenance : false
            onClicked: card.vm.eraseCode()
        }

        HButton {
            objectName: "eraseAllButton"
            text: qsTr("Erase everything…")
            enabled: card.vm ? card.vm.canRunMaintenance : false
            onClicked: card.confirmRequested(false)
        }

        HButton {
            objectName: "recoverButton"
            text: qsTr("Recover…")
            enabled: card.vm ? card.vm.canRunMaintenance : false
            onClicked: card.confirmRequested(true)
        }
    }
}
