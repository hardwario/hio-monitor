import QtCore
import QtQuick
import QtQuick.Dialogs
import Qt.labs.folderlistmodel
import QtQuick.Controls.Material

// BluetoothPage is the page for interacting with Chester via Bluetooth.
// It provides an InteractiveShell with the same functionality as the ConsolePage.
// Device's log is not supported via Bluetooth, thus DeviceLog is not included.
Item {
    property string name: AppSettings.bluetoothName

    InteractiveShell {
        id: bluetoothShell
        anchors.fill: parent
        device: bluetooth
    }

    // file dialog for choosing file with batch commands in it.
    FileDialog {
        id: fileDialog
        nameFilters: ["All files (*)"]
        currentFolder: StandardPaths.standardLocations(
                           StandardPaths.HomeLocation)[0]
        onAccepted: {
            bluetoothShell.device.batchSendCommand(selectedFile)
        }
    }

    Connections {
        target: toolPanel

        function onDisconnectClicked() {
            loadingIndicator.open()
            bluetooth.disconnect()
        }

        function onBatchBtClicked() {
            fileDialog.open()
        }

        function onClearBtClicked() {
            bluetoothShell.clear()
        }
    }
}
