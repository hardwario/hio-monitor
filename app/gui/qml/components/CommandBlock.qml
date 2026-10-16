// Copyable command lines of a Commands step (plan/features/0007, ADR-0072): monospace, each line
// with [Copy].
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: block

    property list<string> commands: []

    signal copyRequested(string text)

    implicitHeight: lines.implicitHeight + 2 * Theme.spacingS
    color: Theme.consoleBg
    border.color: Theme.border
    radius: Theme.radiusS
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Commands")

    ColumnLayout {
        id: lines

        anchors.fill: parent
        anchors.margins: Theme.spacingS
        spacing: Theme.spacingXs

        Repeater {
            model: block.commands

            delegate: RowLayout {
                id: line

                required property string modelData

                objectName: "commandLine"
                Layout.fillWidth: true
                spacing: Theme.spacingS

                TextEdit {
                    Layout.fillWidth: true
                    text: line.modelData
                    font: Theme.fontMonoSmall
                    color: Theme.text
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    Accessible.name: line.modelData
                }

                HToolButton {
                    objectName: "commandCopy"
                    Layout.alignment: Qt.AlignTop
                    iconName: "copy"
                    text: qsTr("Copy")
                    display: AbstractButton.IconOnly
                    onClicked: block.copyRequested(line.modelData)
                }
            }
        }
    }
}
