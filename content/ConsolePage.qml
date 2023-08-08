import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.15
import hiomon 1.0

Item {
    id: _root
    property string name: AppSettings.consoleName
    property int minItemWidth: 250

    Connections {
        target: chester
        onAttachSucceeded: {
            notify.showInfo("Attach Succeeded!")
        }
        onAttachFailed: {
            notify.showError("Attach Failed!")
        }
        onDetachSucceeded: {
            notify.showInfo("Detach Succeeded!")
        }
        onDetachFailed: {
            notify.showError("Detach Failed!")
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
}
