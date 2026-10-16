// The embedded control of the test wizard (plan/features/0007, C1): shows its view model's text.
import QtQuick
import QtQuick.Controls
import HardwarioMonitor

Label {
    property QtObject vm: null

    objectName: "testEmbedded"
    text: vm ? vm.text : ""
    color: Theme.text
}
