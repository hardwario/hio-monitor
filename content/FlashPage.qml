import QtQuick 2.12
import QtQuick.Dialogs
import QtCore
import QtQuick.Controls.Material 2.15

Item {
    property string name: AppSettings.flashName
    property string filePath: ""

    FileDialog {
        id: fileDialog
        currentFolder: StandardPaths.standardLocations(StandardPaths.HomeLocation)[0]
        onAccepted: {
            console.log("Selected file:", selectedFile)
            filePath = selectedFile
        }
    }

    InteractiveShell {
        id: flashShell
        anchors.fill: parent
        device: flash
        labelText: "Flash Shell"
    }

    Connections {
        target: toolPanel
        onBrowseFilesClicked: {
            notify.showInfo("Please choose the .hex file")
            fileDialog.open()
        }
        onFlashClicked: {
            notify.showInfo("Start flashing the device...")
            loadingIndicator.open()
            console.log("flash clicked")
        }
    }
}
