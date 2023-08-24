import QtQuick 2.12
import Qt.labs.folderlistmodel
import QtQuick.Dialogs
import QtCore
import QtQuick.Controls.Material 2.15

Item {
    id: _root
    property string name: AppSettings.flashName
    property int minItemWidth: 150
    property bool isRttRunning: false

    Component.onCompleted: {
        chester.attachSucceeded.connect(function() {
            isRttRunning = true
        })
        chester.detachSucceeded.connect(function() {
            isRttRunning = false
        })
    }

    FileDialog {
        id: fileDialog
        nameFilters: [ "All files (*.hex)", "All files (*)" ]
        currentFolder: StandardPaths.standardLocations(StandardPaths.HomeLocation)[0]
        onAccepted: {
            console.log("Selected file:", selectedFile)
            flash.setHexPath(selectedFile)
        }
    }

    SplitView {
        id: splitView
        anchors.fill: parent
        orientation: Qt.Horizontal

        handle: Rectangle {
            id: handleDelegate
            implicitWidth: 1
            implicitHeight: 1
            color: SplitHandle.pressed ? AppSettings.grayColor : AppSettings.borderColor
            containmentMask: Item {
                x: (handleDelegate.width - width) / 2
                width: 5
                height: splitView.height
            }
        }

        // Welcome message and icon
        Item {
            id: welcome
            SplitView.preferredWidth: _root.width / 2
            SplitView.minimumWidth: _root.minItemWidth
            visible: true

            Image {
                id: img
                source: AppSettings.flashIcon
                width: 100
                height: 200
                anchors {
                    top: parent.top
                    topMargin: 75
                    horizontalCenter: parent.horizontalCenter
                }
            }

            Text {
                id: welcomeMessage
                text: qsTr("Welcome to the Flash page!\n\n")
                anchors {
                    top: img.bottom
                    topMargin: 35
                }
                color: AppSettings.grayColor
                font.pixelSize: 22
                width: parent.width
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                id: steps
                text: qsTr("Enter the hex value of the CHESTER catalog application in the input field.\nOr click Browse and select the .hex file on your computer.\nThen click the Run button.")
                anchors {
                    top: welcomeMessage.bottom
                }
                color: AppSettings.grayColor
                font.pixelSize: 22
                width: parent.width
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                id: pairingWarningMessage
                text: qsTr("Click the Catalog button to browse the CHESTER Catalog Applications\n\n")
                anchors {
                    top: steps.bottom
                    topMargin: 55
                }
                color: AppSettings.wrnColor
                font.pixelSize: 16
                width: parent.width
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }

        InteractiveShell {
            id: flashShell
            SplitView.preferredWidth: welcome.visible ? _root.width / 2 : _root.width
            SplitView.minimumWidth: _root.minItemWidth
            device: flash
            enableHistory: false
            labelText: "Flash Shell"
        }
    }

    ProgressBar {
        id: progress
        from: 0.0
        visible: false
        indeterminate: true
        width: splitView.width
    }

    Connections {
        target: toolPanel
        onBrowseFilesClicked: {
            if (flash.running) {
                notify.showWrn("Please wait for the flashing process to complete!")
                return
            }
            notify.showInfo("Please choose the .hex file")
            fileDialog.open()
        }
        onRunClicked: {
            if (flash.running) {
                notify.showWrn("Please wait for the flashing process to complete!")
                return
            }
            if (isRttRunning) {
                notify.showWrn("Please detach from a device via Console page then Run flash process again!")
                return
            }
            if (!flash.ready) {
                notify.showWrn("Try to enter a hex from Catalog or Browse for a file first!")
                return
            }
            notify.showInfo("Start flashing the device...")
            progress.visible = true
            flash.defaultFlash()
        }
    }

    Connections {
        target: flash
        onFinished: {
            progress.value = 0.0
            progress.visible = false
        }
        onErrorOccured: {
            progress.value = 0.0
            progress.visible = false
            notify.showErr("Flash process failed")
        }
    }
}
