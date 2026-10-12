// Shown at start when another instance holds the single-instance lock but does not answer within
// 2 s (ADR-0058): [Quit] [Start anyway]. main() reads `startAnyway` when the window closes.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import HardwarioMonitor

ApplicationWindow {
    id: dialog

    property bool startAnyway: false

    objectName: "alreadyRunningDialog"
    width: 460
    height: 170
    minimumWidth: 360
    minimumHeight: 150
    visible: true
    title: qsTr("HARDWARIO Monitor")
    color: Theme.surfaceRaised
    font: Theme.fontUi
    Material.theme: Theme.dark ? Material.Dark : Material.Light
    Material.accent: Theme.accent

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXl
        spacing: Theme.spacingL

        Label {
            objectName: "alreadyRunningText"
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: qsTr("HARDWARIO Monitor is already running but does not respond.")
            color: Theme.text
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacingS

            HButton {
                objectName: "startAnywayButton"
                text: qsTr("Start anyway")
                onClicked: {
                    dialog.startAnyway = true;
                    dialog.close();
                }
            }

            HButton {
                objectName: "quitButton"
                kind: "primary"
                text: qsTr("Quit")
                focus: true
                onClicked: dialog.close()
            }
        }
    }
}
