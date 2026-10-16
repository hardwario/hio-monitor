// The rows of a Checks step (plan/features/0007): a status icon (spinner while running, ✓, !, ✗,
// –), the title and the result text; a failed row shows the error's cause and suggestion. Results
// are announced to screen readers when they arrive.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: list

    property CheckListModel model: null

    spacing: Theme.spacingS
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Checks")

    Connections {
        target: list.model

        function onResultAnnounced(text: string) {
            list.Accessible.announce(text);
        }
    }

    Repeater {
        model: list.model

        delegate: ColumnLayout {
            id: row

            required property string checkId
            required property string status
            required property string title
            required property string result
            required property errorInfo error

            objectName: "checkRow"
            Layout.fillWidth: true
            spacing: Theme.spacingXs
            Accessible.role: Accessible.ListItem
            Accessible.name: row.title + ": " + row.result

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS

                Item {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: Theme.iconSize
                    implicitHeight: Theme.iconSize

                    BusyIndicator {
                        anchors.fill: parent
                        visible: row.status === "running"
                        running: row.status === "running" && !AppViewModel.reducedMotion
                    }

                    Icon {
                        objectName: "checkIcon"
                        anchors.centerIn: parent
                        visible: row.status !== "running" && row.status !== "skipped"
                        symbol: row.status === "ok" ? "success"
                              : row.status === "warning" ? "warning"
                              : row.status === "failed" ? "error" : "pending"
                        color: row.status === "ok" ? Theme.success
                             : row.status === "warning" ? Theme.warning
                             : row.status === "failed" ? Theme.error : Theme.textMuted
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: row.status === "skipped"
                        text: "–"
                        color: Theme.textMuted
                    }
                }

                Label {
                    objectName: "checkTitle"
                    Layout.preferredWidth: Math.min(implicitWidth, list.width * 0.45)
                    Layout.alignment: Qt.AlignTop
                    text: row.title
                    font: Theme.fontUiStrong
                    color: Theme.text
                    wrapMode: Text.Wrap
                }

                Label {
                    objectName: "checkResult"
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    text: row.result
                    color: row.status === "failed" ? Theme.error
                         : row.status === "warning" ? Theme.warning : Theme.textMuted
                    wrapMode: Text.Wrap
                }
            }

            Label {
                objectName: "checkCause"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.iconSize + Theme.spacingS
                visible: (row.status === "failed" || row.status === "warning") && row.error.cause !== ""
                text: qsTr("Probably: %1").arg(row.error.cause)
                color: Theme.textMuted
                wrapMode: Text.Wrap
            }

            Label {
                objectName: "checkSuggestion"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.iconSize + Theme.spacingS
                visible: (row.status === "failed" || row.status === "warning") && row.error.suggestion !== ""
                text: qsTr("What to do: %1").arg(row.error.suggestion)
                color: Theme.text
                wrapMode: Text.Wrap
            }
        }
    }
}
