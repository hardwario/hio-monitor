// The contract test page (plan/features/0004-m3-gui-shell.md): gets `tab` and `vm` from PageHost.
import QtQuick
import QtQuick.Controls
import HardwarioMonitor

Item {
    id: page

    property DeviceTabViewModel tab
    property QtObject vm

    objectName: "testPage"

    Label {
        objectName: "testPageLabel"
        anchors.centerIn: parent
        text: page.vm ? "sessions " + page.vm.sessionChanges : "no vm"
    }
}
