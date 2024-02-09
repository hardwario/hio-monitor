import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

import hiomon 1.0

Item {
    UpdateChecker {
        id: updateChecker

        Component.onCompleted: {
            updateChecker.checkForUpdate(APP_VERSION)
        }
    }

    Dialog {
        id: updateDialog
        modal: true
        visible: updateChecker.updateAvailable
        x: (appWindow.width - width) / 2
        y: (appWindow.height - height) / 2

        Column {
            spacing: 10
            padding: 20

            Text {
                text: "A new version of the application is available for download!"
                font.family: textFont.name
                font.pixelSize: 26
                color: AppSettings.focusColor
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 10

                Button {
                    contentItem: Text {
                        text: "Close"
                        font.family: labelFont.name
                        font.pixelSize: 18
                        color: AppSettings.redColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: Material.background
                    }

                    onClicked: updateDialog.close()
                }

                Button {
                    contentItem: Text {
                        text: "Download"
                        font.family: labelFont.name
                        font.pixelSize: 18
                        color: AppSettings.greenColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: Material.background
                    }

                    onClicked: {
                        Qt.openUrlExternally(AppSettings.latestReleaseUrl)
                        Qt.quit()
                    }
                }
            }
        }
    }
}
