import QtCore
import QtQuick
import QtQuick.Dialogs
import Qt.labs.folderlistmodel
import QtQuick.Controls.Material

// FlashPage is a page for flashing the device with the selected hex file or the hex from the catalog application web url.
// If user input something it will try to download the hex from the web assuming that user has typed a valid hex, if not it will show network error.
// User can select file from the file dialog.
// User have to detach from the device via Console page before starting the flash process.
// User should not start the flash process if the RTT is running!!!
// User should confirm the flash process by clicking the Run button.
Item {
    id: _root
    property string name: AppSettings.flashName
    property int minItemWidth: 150
    property bool isRttRunning: false

    Component.onCompleted: {
        chester.attachSucceeded.connect(function () {
            isRttRunning = true
        })

        chester.detachSucceeded.connect(function () {
            isRttRunning = false
        })
    }

    Connections {
        target: toolPanel

        function onClearFlashClicked() {
            flashShell.clear()
        }

        function onBrowseClicked() {
            if (flash.running) {
                notify.showWarn(
                            "Please wait for the flashing process to complete.")
                return
            }
            notify.showInfo("Please choose the .hex file")
            fileDialog.open()
        }

        function onRunClicked() {
            if (flash.running) {
                notify.showWarn(
                            "Please wait for the flashing process to complete.")
                return
            }
            if (isRttRunning) {
                notify.showWarn(
                            "Please detach from a device via Console page then FLASH again.")
                return
            }
            if (!flash.ready) {
                notify.showWarn(
                            "Try to enter a hex from CATALOG or BROWSE for a file first.")
                return
            }

            notify.showInfo("Start flashing the device...")
            progress.visible = true
            flash.defaultFlash()
        }
    }

    Connections {
        target: flash

        function onFinished() {
            progress.value = 0.0
            progress.visible = false
        }

        function onErrorOccured() {
            progress.value = 0.0
            progress.visible = false
            notify.showError("Flash process failed")
        }
    }

    FileDialog {
        id: fileDialog
        nameFilters: ["All files (*.hex)", "All files (*)"]
        currentFolder: StandardPaths.standardLocations(
                           StandardPaths.HomeLocation)[0]
        onAccepted: {
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

        Item {
            id: welcome
            SplitView.preferredWidth: _root.width / 2
            SplitView.minimumWidth: _root.minItemWidth
            visible: true

            Image {
                id: img
                source: AppSettings.flashIcon
                width: 200
                height: 200
                anchors {
                    top: parent.top
                    topMargin: 75
                    horizontalCenter: parent.horizontalCenter
                }
            }

            Text {
                id: welcomeMessage
                text: qsTr("Welcome to the Flash page.")
                anchors {
                    top: img.bottom
                    topMargin: 40
                }
                color: AppSettings.whiteColor
                font.pixelSize: 24
                width: parent.width
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                id: steps
                text: qsTr("Enter the unique identifier of the CHESTER catalog application in the input field.\nOr click BROWSE and select the .hex file on your computer.\nThen click the FLASH button.")
                anchors {
                    top: welcomeMessage.bottom
                    topMargin: 24
                }
                color: AppSettings.whiteColor
                font.pixelSize: 20
                width: parent.width
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                id: pairingWarningMessage
                text: qsTr(
                          "Click the CATALOG button to browse the CHESTER Catalog Applications\n\n")
                anchors {
                    top: steps.bottom
                    topMargin: 48
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
            labelText: "FLASH SHELL"
            inputHint: "Enter unique identifier from CATALOG applications"
        }
    }

    ProgressBar {
        id: progress
        from: 0.0
        visible: false
        indeterminate: true
        width: splitView.width
    }
}
