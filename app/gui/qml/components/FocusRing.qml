// 2 px accent focus ring around keyboard-focused controls (design system § 6); never removed.
import QtQuick
import QtQuick.Templates as T
import HardwarioMonitor

Rectangle {
    property T.Control control: null
    property int radiusOffset: Theme.radiusM

    anchors.fill: parent
    anchors.margins: -Theme.focusRingWidth
    radius: radiusOffset + Theme.focusRingWidth
    color: "transparent"
    border.width: Theme.focusRingWidth
    border.color: Theme.accent
    visible: control !== null && control.visualFocus === true
    z: 10
}
