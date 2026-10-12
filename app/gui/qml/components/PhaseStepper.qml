// Horizontal phase stepper of long operations (design system "PhaseProgress", used by 0006):
// phases done (✓), current (spinner) and pending, a progress bar (indeterminate when the total is
// unknown, progress < 0), a detail line ("412 / 803 kB · 51 % · 0:07") and Cancel.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: stepper

    // [{title: "Programming", state: "done" | "current" | "pending"}]
    property var phases: []
    property real progress: -1
    property string detailText: ""
    property bool cancelVisible: true
    property bool cancelEnabled: true

    signal cancelRequested

    spacing: Theme.spacingM
    Accessible.role: Accessible.ProgressBar
    Accessible.name: stepper.detailText

    Flow {
        Layout.fillWidth: true
        spacing: Theme.spacingS

        Repeater {
            model: stepper.phases

            delegate: Row {
                id: phase

                required property var modelData
                required property int index

                spacing: Theme.spacingS

                Rectangle {
                    visible: phase.index > 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 2
                    color: Theme.border
                }

                Rectangle {
                    objectName: "phaseCircle"
                    width: 22
                    height: 22
                    radius: 11
                    color: phase.modelData.state === "done" ? Theme.success : "transparent"
                    border.width: 2
                    border.color: phase.modelData.state === "done" ? Theme.success
                                : phase.modelData.state === "current" ? Theme.accent : Theme.border

                    Icon {
                        anchors.centerIn: parent
                        visible: phase.modelData.state === "done"
                        symbol: "check"
                        size: 16
                        color: Theme.onAccent
                    }

                    BusyIndicator {
                        anchors.centerIn: parent
                        visible: phase.modelData.state === "current"
                        running: visible && !AppViewModel.reducedMotion
                        width: 20
                        height: 20
                    }
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: phase.modelData.title
                    font: phase.modelData.state === "current" ? Theme.fontUiStrong : Theme.fontUi
                    color: phase.modelData.state === "pending" ? Theme.textMuted : Theme.text
                    Accessible.name: phase.modelData.title
                }
            }
        }
    }

    ProgressBar {
        objectName: "phaseProgress"
        Layout.fillWidth: true
        from: 0
        to: 1
        value: Math.max(0, stepper.progress)
        indeterminate: stepper.progress < 0
    }

    RowLayout {
        Layout.fillWidth: true

        Label {
            Layout.fillWidth: true
            text: stepper.detailText
            color: Theme.textMuted
            elide: Text.ElideRight
        }

        HButton {
            objectName: "phaseCancel"
            visible: stepper.cancelVisible
            enabled: stepper.cancelEnabled
            text: qsTr("Cancel")
            onClicked: stepper.cancelRequested()
        }
    }
}
