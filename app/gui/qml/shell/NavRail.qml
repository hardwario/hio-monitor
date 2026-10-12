// The rail (ADR-0043, ADR-0061): the per-tab pages (Console, Flash, ...) with icon and label, a
// busy badge on the page that runs an operation; Settings (global pages) and Help at the bottom.
// Below 1100 px window width it shows icons only, with the labels as tooltips.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: rail

    readonly property DeviceTabViewModel tab: AppViewModel.currentTabViewModel
    readonly property bool compact: AppViewModel.railCompact

    implicitWidth: rail.compact ? Theme.railWidthCompact : Theme.railWidth
    color: Theme.surface
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Pages")

    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 1
        color: Theme.border
    }

    component RailItem: AbstractButton {
        id: item

        property string iconName: ""
        property bool selected: false
        property bool busy: false
        property string shortcutId: ""

        objectName: "railItem"
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: rail.compact ? 44 : 64
        implicitHeight: rail.compact ? 40 : Theme.railItemSize
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.Button
        Accessible.name: item.text
        Accessible.checked: item.selected
        Accessible.description: item.busy ? qsTr("Busy") : ""
        ToolTip.visible: item.hovered
        ToolTip.text: AppViewModel.shortcuts.tooltip(item.text, item.shortcutId)
        ToolTip.delay: 400

        background: Item {
            FocusRing {
                control: item
            }
        }

        contentItem: ColumnLayout {
            spacing: 2

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 40
                implicitHeight: 26
                radius: 13
                color: item.selected ? Theme.accentSoft : (item.hovered ? Theme.bg : "transparent")

                Icon {
                    anchors.centerIn: parent
                    symbol: item.iconName
                    color: item.selected ? Theme.accent : (item.enabled ? Theme.textMuted : Theme.border)
                }

                BusyIndicator {
                    objectName: "railBusy"
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: -4
                    width: 16
                    height: 16
                    visible: item.busy
                    running: item.busy && !AppViewModel.reducedMotion
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: 64
                visible: !rail.compact
                text: item.text
                font: item.selected ? Theme.fontUiStrong : Theme.fontUiSmall
                color: item.selected ? Theme.text : Theme.textMuted
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.spacingS
        anchors.bottomMargin: Theme.spacingS
        spacing: Theme.spacingXs

        Repeater {
            model: AppViewModel.pages

            delegate: RailItem {
                required property var modelData
                required property int index

                visible: !modelData.global
                text: modelData.title
                iconName: modelData.icon
                shortcutId: "page." + modelData.id
                enabled: rail.tab !== null && rail.tab.state !== DeviceTabViewModel.ChoosingDevice
                selected: enabled && AppViewModel.globalPage === "" && rail.tab.currentPage === modelData.id
                busy: rail.tab !== null && rail.tab.busyPage === modelData.id
                onClicked: AppViewModel.showPage(modelData.id)
            }
        }

        Item {
            Layout.fillHeight: true
        }

        Repeater {
            model: AppViewModel.pages

            delegate: RailItem {
                required property var modelData

                visible: modelData.global
                text: modelData.title
                iconName: modelData.icon
                shortcutId: modelData.id === "settings" ? "settings.open" : ""
                selected: AppViewModel.globalPage === modelData.id
                onClicked: AppViewModel.globalPage === modelData.id ? AppViewModel.closeGlobalPage()
                                                                    : AppViewModel.openGlobalPage(modelData.id)
            }
        }

        RailItem {
            objectName: "railHelp"
            text: qsTr("Help")
            iconName: "help"
            shortcutId: "help.shortcuts"
            onClicked: AppViewModel.shortcutOverlayVisible = true
        }
    }
}
