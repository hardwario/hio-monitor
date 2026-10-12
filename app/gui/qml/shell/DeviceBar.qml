// Device bar of a tab (ADR-0043, ADR-0052, ADR-0053): probe, target (disabled while a session
// exists), state text ("Connected", "Connecting…", "Reconnecting… (target not powered)", the busy
// page's text), Connect / Cancel / Disconnect and the ⋯ Advanced popover.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: bar

    required property DeviceTabViewModel tab
    readonly property ConnectionViewModel connection: bar.tab.connection
    readonly property bool choosing: bar.tab.state === DeviceTabViewModel.ChoosingDevice

    implicitHeight: Theme.deviceBarHeight
    color: Theme.surface

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingM

        Label {
            objectName: "chooseDeviceLabel"
            visible: bar.choosing
            text: qsTr("Choose device")
            color: Theme.textMuted
        }

        // Probe chip.
        Rectangle {
            objectName: "probeChip"
            visible: !bar.choosing
            Layout.maximumWidth: 280
            Layout.minimumWidth: 120
            Layout.preferredWidth: probeRow.implicitWidth + 2 * Theme.spacingM
            implicitHeight: 30
            radius: Theme.radiusM
            color: Theme.bg
            border.color: Theme.border
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Probe %1").arg(bar.connection.probeLabel)

            RowLayout {
                id: probeRow

                anchors.fill: parent
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingM
                spacing: Theme.spacingS

                Label {
                    text: qsTr("Probe")
                    color: Theme.textMuted
                }

                Label {
                    objectName: "probeLabel"
                    Layout.fillWidth: true
                    text: bar.connection.probeLabel
                    color: Theme.text
                    elide: Text.ElideMiddle
                }
            }
        }

        ComboBox {
            id: targetBox

            objectName: "targetBox"
            visible: !bar.choosing
            implicitHeight: 30
            Layout.maximumWidth: 260
            model: bar.connection.targets
            textRole: "title"
            valueRole: "id"
            currentIndex: targetBox.indexOfValue(bar.connection.targetId)
            enabled: bar.connection.canChangeTarget
            font: Theme.fontUi
            displayText: qsTr("Target: %1").arg(bar.connection.targetId)
            Accessible.name: qsTr("Target")
            onActivated: bar.connection.targetId = targetBox.currentValue
            ToolTip.visible: targetBox.hovered
            ToolTip.text: bar.connection.canChangeTarget ? bar.connection.targetTitle
                                                         : qsTr("Disconnect to change the target")
            ToolTip.delay: 600

            FocusRing {
                control: targetBox
                radiusOffset: Theme.radiusS
            }
        }

        RowLayout {
            visible: !bar.choosing
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            spacing: Theme.spacingS

            StateDot {
                kind: bar.connection.stateKind
            }

            BusyIndicator {
                visible: bar.connection.stateKind === "connecting" || bar.connection.stateKind === "reconnecting"
                running: visible && !AppViewModel.reducedMotion
                implicitWidth: 18
                implicitHeight: 18
            }

            Label {
                id: stateLabel

                objectName: "stateText"
                Layout.fillWidth: true
                text: bar.connection.stateText
                color: bar.connection.stateKind === "error" ? Theme.error : Theme.textMuted
                // Two lines rather than an elided cause (Czech at 960 px, review S13).
                wrapMode: Text.Wrap
                maximumLineCount: 2
                lineHeight: 0.95
                elide: Text.ElideRight
                Accessible.role: Accessible.StatusBar
                Accessible.name: stateLabel.text
                onTextChanged: if (bar.tab === AppViewModel.currentTabViewModel) Accessible.announce(stateLabel.text)
                ToolTip.visible: stateHover.hovered && stateLabel.truncated
                ToolTip.text: stateLabel.text

                HoverHandler {
                    id: stateHover
                }
            }
        }

        Item {
            visible: bar.choosing
            Layout.fillWidth: true
        }

        HButton {
            objectName: "chooseAnotherProbe"
            visible: !bar.choosing && bar.connection.canChooseAnotherProbe
            text: qsTr("Choose another probe")
            onClicked: bar.connection.chooseAnotherProbe()
        }

        HButton {
            objectName: "connectionAction"
            visible: !bar.choosing
            kind: bar.connection.action === "connect" ? "primary" : "normal"
            enabled: bar.connection.actionEnabled
            text: bar.connection.action === "connect" ? qsTr("Connect")
                : bar.connection.action === "cancel" ? qsTr("Cancel") : qsTr("Disconnect")
            toolTipText: AppViewModel.shortcuts.tooltip(text, "device.toggle")
            onClicked: bar.connection.triggerAction()
        }

        HToolButton {
            id: moreButton

            objectName: "advancedButton"
            visible: !bar.choosing
            iconName: "more"
            text: qsTr("Advanced")
            display: AbstractButton.IconOnly
            checked: popover.visible
            onClicked: popover.visible ? popover.close() : popover.open()
        }
    }

    AdvancedPopover {
        id: popover

        connection: bar.connection
        x: bar.width - popover.width - Theme.spacingM
        y: bar.height - Theme.spacingXs
    }
}
