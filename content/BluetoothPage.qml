import QtQuick 2.12
import QtQuick.Controls.Material 2.15
import Qt.labs.folderlistmodel
import QtQuick.Dialogs
import QtCore

Item {
    property string name: AppSettings.bluetoothName

    InteractiveShell {
        id: bluetoothShell
        anchors.fill: parent
        device: bluetooth
    }

    FileDialog {
        id: fileDialog
        nameFilters: [ "All files (*)" ]
        currentFolder: StandardPaths.standardLocations(StandardPaths.HomeLocation)[0]
        onAccepted: {
            console.log("Selected file:", selectedFile)
            bluetoothShell.device.batchSendCommand(selectedFile)
        }
    }

    Connections {
        target: toolPanel
        onDisconnectClicked: {
            loadingIndicator.open()
            bluetooth.disconnect()
        }
        onBatchBtClicked: {
            fileDialog.open()
        }
        onClearBtClicked: {
            bluetoothShell.clear()
        }
    }
}
