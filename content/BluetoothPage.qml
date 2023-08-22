import QtQuick 2.12
import QtQuick.Controls.Material 2.15

Item {
    property string name: AppSettings.bluetoothName

    InteractiveShell {
        id: bluetoothShell
        anchors.fill: parent
        device: bluetooth
    }

    Connections {
        target: toolPanel
        onDisconnectClicked: {
            loadingIndicator.open()
            bluetooth.disconnect()
        }
    }
}
