// Shown at start when another instance holds the single-instance lock but does not answer within
// 2 s (ADR-0058): [Quit] [Start anyway]. main() reads `startAnyway` when the window closes.
// It runs before AppViewModel exists (review S7): no Theme / AppViewModel; main() passes the
// theme, the texts are translated by the LanguageManager main() installs for it.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: dialog

    required property bool darkTheme
    property bool startAnyway: false

    objectName: "alreadyRunningDialog"
    width: 460
    height: 170
    minimumWidth: 360
    minimumHeight: 150
    visible: true
    title: qsTr("HARDWARIO Monitor")
    font.pixelSize: 13
    font.family: "Inter"
    Material.theme: dialog.darkTheme ? Material.Dark : Material.Light
    Material.accent: dialog.darkTheme ? "#79B8FF" : "#0B5FCC"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Label {
            objectName: "alreadyRunningText"
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: qsTr("HARDWARIO Monitor is already running but does not respond.")
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8

            Button {
                objectName: "startAnywayButton"
                text: qsTr("Start anyway")
                flat: true
                onClicked: {
                    dialog.startAnyway = true;
                    dialog.close();
                }
            }

            Button {
                objectName: "quitButton"
                text: qsTr("Quit")
                highlighted: true
                focus: true
                onClicked: dialog.close()
            }
        }
    }
}
