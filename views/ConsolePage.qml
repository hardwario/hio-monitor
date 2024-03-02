import QtCore
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Controls
// Qt.labs.folderlistmodel should be there because of linux's FileDialog bug on ubuntu
import Qt.labs.folderlistmodel
import QtQuick.Controls.Material

import hiomon 1.0

// ConsolePage is a page for console-like interaction with a device.
// It works over a J-Link RTT connection.
// It has two views: InteractiveShell and DeviceLog.
// InteractiveShell is a console-like input/output widget with mouse support and CommandHistory.
// DeviceLog is a log of all messages received from the device where log levels are colored.
Item {
    id: _root
    property string name: AppSettings.consoleName
    property int minItemWidth: 250

    Connections {
        target: chester

        function onAttachSucceeded() {
            notify.showInfo("Attach Succeeded.")
        }

        function onAttachFailed() {
            notify.showError("Attach Failed.")
        }

        function onDetachSucceeded() {
            notify.showInfo("Detach Succeeded.")
        }

        function onDetachFailed() {
            notify.showError("Detach Failed.")
        }
    }

    // select file with batch commands
    FileDialog {
        id: fileDialog
        nameFilters: ["All files (*)"]
        currentFolder: StandardPaths.standardLocations(
                           StandardPaths.HomeLocation)[0]
        onAccepted: {
            consoleShell.device.batchSendCommand(fileDialog.selectedFile)
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

        InteractiveShell {
            id: consoleShell
            SplitView.preferredWidth: _root.width / 2
            SplitView.minimumWidth: _root.minItemWidth
            device: chester
        }

        DeviceLog {
            id: deviceLog
            SplitView.preferredWidth: _root.width / 2
            SplitView.minimumWidth: _root.minItemWidth
        }
    }

    Connections {
        target: toolPanel

        function onBatchCliClicked() {
            fileDialog.open()
        }

        function onClearCliClicked() {
            consoleShell.clear()
            deviceLog.clear()
        }
    }
}
