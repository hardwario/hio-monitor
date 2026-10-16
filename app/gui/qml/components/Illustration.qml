// A wizard illustration slot (ADR-0076): app/gui/assets/wizard/<slot>[-dark].svg, 480 × 270
// logical px, scaled down to fit; no text in images (the caption is a separate text). A slot
// without a dark variant uses the light one on a `surface` background; an unknown slot shows the
// placeholder.
import QtQuick
import HardwarioMonitor

Rectangle {
    id: illustration

    property string slot: ""
    property string caption: ""
    readonly property url source: AppViewModel.wizard.illustrationUrl(illustration.slot, Theme.dark)

    implicitWidth: 480
    implicitHeight: 270
    color: Theme.surface
    radius: Theme.radiusM
    Accessible.role: Accessible.Graphic
    Accessible.name: illustration.caption

    Image {
        objectName: "illustrationImage"
        anchors.fill: parent
        anchors.margins: Theme.spacingS
        source: illustration.source
        sourceSize: Qt.size(480, 270)
        fillMode: Image.PreserveAspectFit
        asynchronous: false
    }
}
