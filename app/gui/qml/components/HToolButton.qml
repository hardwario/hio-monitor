// Tool button with an icon and an optional label (labels hidden when `compact`, e.g. below
// 1100 px). The tooltip names the action and its shortcut: "Find (Ctrl+F)". Plan 0028
// (ADR-0203): an optional `description` follows on the next lines of the tooltip (also shown on
// keyboard focus) and is the accessible description.
import QtQuick
import QtQuick.Controls
import HardwarioMonitor

ToolButton {
    id: control

    property string iconName: ""
    property string shortcutId: ""
    property bool compact: false
    property string description: ""
    property color iconColor: control.checked ? Theme.accent : Theme.text
    // The tooltip (tests read its text).
    readonly property alias toolTip: tip

    implicitHeight: Theme.toolButtonSize
    implicitWidth: control.display === AbstractButton.IconOnly ? Theme.toolButtonSize : implicitContentWidth + leftPadding + rightPadding
    padding: Theme.spacingXs
    display: control.compact || control.text === "" ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
    font: Theme.fontUi
    focusPolicy: Qt.StrongFocus
    icon.source: Theme.iconUrl(control.iconName)
    icon.color: control.iconColor
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize

    HToolTip {
        id: tip

        visible: (control.hovered || (control.description !== "" && control.visualFocus)) && control.text !== ""
        text: AppViewModel.shortcuts.tooltip(control.text, control.shortcutId) + (control.description !== "" ? "\n" + control.description : "")
        delay: 600
    }
    Accessible.name: control.text
    Accessible.description: control.description

    FocusRing {
        control: control
        radiusOffset: Theme.radiusS
    }
}
