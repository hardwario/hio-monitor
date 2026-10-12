// Tool button with an icon and an optional label (labels hidden when `compact`, e.g. below
// 1100 px). The tooltip names the action and its shortcut: "Find (Ctrl+F)".
import QtQuick
import QtQuick.Controls
import HardwarioMonitor

ToolButton {
    id: control

    property string iconName: ""
    property string shortcutId: ""
    property bool compact: false
    property color iconColor: control.checked ? Theme.accent : Theme.text

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

    ToolTip.visible: control.hovered && control.text !== ""
    ToolTip.text: AppViewModel.shortcuts.tooltip(control.text, control.shortcutId)
    ToolTip.delay: 600
    Accessible.name: control.text

    FocusRing {
        control: control
        radiusOffset: Theme.radiusS
    }
}
