// Button of the design system (ADR-0044 wrapper; design system § 5): flat with a 1 px border,
// radius 6, no elevation. kind: "normal", "primary", "danger" (destructive actions) or "ghost".
// Sizes to its content (Czech texts are longer). Disabled buttons are shown at 45 % opacity; the
// focus ring is never removed.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import HardwarioMonitor

Button {
    id: control

    property string kind: "normal"
    property string iconName: ""
    property string toolTipText: ""

    readonly property color baseColor: control.kind === "primary" ? Theme.accent
                                     : control.kind === "danger" ? Theme.error
                                     : control.kind === "ghost" ? "transparent" : Theme.bg
    readonly property color contentColor: control.kind === "primary" || control.kind === "danger"
                                          ? Theme.onAccent : (control.kind === "ghost" ? Theme.accent : Theme.text)

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(64, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: Theme.spacingM + 2
    rightPadding: Theme.spacingM + 2
    topPadding: 0
    bottomPadding: 0
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    spacing: Theme.spacingXs + 2
    flat: control.kind === "ghost"
    highlighted: control.kind === "primary" || control.kind === "danger"
    font: Theme.fontUiStrong
    focusPolicy: Qt.StrongFocus
    opacity: control.enabled ? 1 : 0.45
    icon.source: Theme.iconUrl(control.iconName)
    icon.color: control.contentColor
    icon.width: 18
    icon.height: 18

    ToolTip.visible: control.hovered && control.toolTipText !== ""
    ToolTip.text: control.toolTipText
    ToolTip.delay: 600
    Accessible.name: control.text !== "" ? control.text : control.toolTipText
    Accessible.description: control.toolTipText

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: control.contentColor
    }

    background: Rectangle {
        implicitWidth: 64
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusM
        color: control.baseColor
        border.width: control.kind === "normal" ? 1 : 0
        border.color: Theme.border

        // Hover and press feedback (no ripple, no elevation).
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: control.kind === "primary" || control.kind === "danger" ? Theme.onAccent : Theme.text
            opacity: control.down || control.checked && control.kind === "normal" ? 0.14 : (control.hovered ? 0.07 : 0)

            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.animationMs
                }
            }
        }

        FocusRing {
            control: control
        }
    }
}
