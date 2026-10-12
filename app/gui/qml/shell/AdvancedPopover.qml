// ⋯ Advanced popover of the device bar (ADR-0052): J-Link speed, RTT latency, auto-reconnect,
// reset on open and "Save console to log file", per tab and remembered per probe. The values apply
// on the next connect.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Popup {
    id: popover

    required property ConnectionViewModel connection

    objectName: "advancedPopover"
    width: 340
    padding: Theme.spacingL
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusM
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS

        Label {
            text: qsTr("Advanced")
            font: Theme.fontUiStrong
            color: Theme.text
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingM
            rowSpacing: Theme.spacingXs

            Label {
                Layout.fillWidth: true
                text: qsTr("J-Link speed (kHz)")
                color: Theme.text
                wrapMode: Text.Wrap
            }

            StepSpinBox {
                id: speedBox

                objectName: "speedBox"
                from: popover.connection.minSpeedKhz
                to: popover.connection.maxSpeedKhz
                step: 100
                value: popover.connection.speedKhz
                Accessible.name: qsTr("J-Link speed (kHz)")
                onValueModified: popover.connection.speedKhz = speedBox.value
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("RTT latency (ms)")
                color: Theme.text
                wrapMode: Text.Wrap
            }

            StepSpinBox {
                id: latencyBox

                objectName: "latencyBox"
                from: popover.connection.minLatencyMs
                to: popover.connection.maxLatencyMs
                step: 10
                value: popover.connection.latencyMs
                Accessible.name: qsTr("RTT latency (ms)")
                onValueModified: popover.connection.latencyMs = latencyBox.value
            }
        }

        Switch {
            objectName: "autoReconnectSwitch"
            Layout.fillWidth: true
            text: qsTr("Auto-reconnect")
            checked: popover.connection.autoReconnect
            onToggled: popover.connection.autoReconnect = checked
        }

        Switch {
            objectName: "resetOnOpenSwitch"
            Layout.fillWidth: true
            text: qsTr("Reset on connect")
            checked: popover.connection.resetOnOpen
            onToggled: popover.connection.resetOnOpen = checked
        }

        Switch {
            objectName: "logToFileSwitch"
            Layout.fillWidth: true
            text: qsTr("Save console to log file")
            checked: popover.connection.logToFile
            onToggled: popover.connection.logToFile = checked
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Changes apply on the next connect.")
            font: Theme.fontUiSmall
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }
}
