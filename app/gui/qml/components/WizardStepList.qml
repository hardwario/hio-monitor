// The step list of a wizard (plan/features/0007, ADR-0066): done (✓), current (●) and pending (○)
// steps with their titles in one row; the compact form "Step 2 of 4 · Cable" below 800 px window
// width (`compact`) and whenever the row does not fit (long Czech titles at 960 px).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import HardwarioMonitor

Item {
    id: stepList

    // [{id, title, state: "done" | "current" | "pending"}]
    property var steps: []
    property bool compact: false
    property string compactText: ""
    readonly property bool showCompact: stepList.compact || flow.implicitWidth > stepList.width

    implicitHeight: stepList.showCompact ? compactLabel.implicitHeight : flow.implicitHeight
    Accessible.role: Accessible.List
    Accessible.name: stepList.compactText

    Label {
        id: compactLabel

        objectName: "wizardStepCompact"
        anchors.left: parent.left
        anchors.right: parent.right
        visible: stepList.showCompact
        text: stepList.compactText
        color: Theme.textMuted
        elide: Text.ElideRight
    }

    Row {
        id: flow

        objectName: "wizardStepFlow"
        anchors.left: parent.left
        visible: !stepList.showCompact
        spacing: Theme.spacingS

        Repeater {
            model: stepList.steps

            delegate: Row {
                id: entry

                required property var modelData
                required property int index

                objectName: "wizardStep"
                spacing: Theme.spacingXs
                Accessible.role: Accessible.ListItem
                Accessible.name: entry.modelData.title

                Label {
                    visible: entry.index > 0
                    text: "─"
                    color: Theme.border
                }

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    size: 16
                    symbol: entry.modelData.state === "done" ? "check" : "pending"
                    color: entry.modelData.state === "pending" ? Theme.textMuted : Theme.success
                    visible: entry.modelData.state !== "current"
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: entry.modelData.state === "current"
                    width: 10
                    height: 10
                    radius: 5
                    color: Theme.accent
                }

                Label {
                    text: entry.modelData.title
                    font: entry.modelData.state === "current" ? Theme.fontUiStrong : Theme.fontUi
                    color: entry.modelData.state === "pending" ? Theme.textMuted : Theme.text
                }
            }
        }
    }
}
