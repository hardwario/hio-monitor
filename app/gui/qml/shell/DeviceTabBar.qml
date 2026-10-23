// Device tab strip (ADR-0043): state dot + "CHESTER app · 683012345" + close ×, "+" at the end
// (Ctrl+T; disabled at 8 tabs with a tooltip). Middle-click closes a tab. Amended by 0021
// (ADR-0172): while open, the Settings tab (gear icon, "Settings", no state dot) follows the
// device tabs.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: bar

    implicitHeight: Theme.tabHeight
    color: Theme.surface
    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Device tabs")

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingS
        anchors.rightMargin: Theme.spacingS
        spacing: 2

        Repeater {
            model: AppViewModel.tabs

            delegate: AbstractButton {
                id: tabButton

                required property int index
                required property DeviceTabViewModel tab
                required property string title
                required property string tabKind
                readonly property bool settingsTab: tabButton.tabKind === "settings"
                readonly property bool selected: tabButton.index === AppViewModel.currentTab
                // The model clears `tab` when the row is removed; keep the last one until the
                // delegate is destroyed.
                property DeviceTabViewModel stableTab: null

                Binding on stableTab {
                    value: tabButton.tab
                    when: tabButton.tab !== null
                    restoreMode: Binding.RestoreNone
                }

                objectName: tabButton.settingsTab ? "settingsTab" : "deviceTab"
                Layout.alignment: Qt.AlignBottom
                Layout.fillWidth: true
                Layout.minimumWidth: 96
                Layout.maximumWidth: 260
                implicitHeight: tabButton.selected ? 31 : 30
                leftPadding: Theme.spacingS + 2
                rightPadding: Theme.spacingXs
                focusPolicy: Qt.StrongFocus
                checkable: false
                Accessible.role: Accessible.PageTab
                Accessible.name: tabButton.settingsTab ? tabButton.title
                                 : tabButton.title + ", " + (tabButton.stableTab ? tabButton.stableTab.connection.stateText : "")
                Accessible.selected: tabButton.selected
                onClicked: AppViewModel.currentTab = tabButton.index

                TapHandler {
                    acceptedButtons: Qt.MiddleButton
                    onTapped: AppViewModel.closeTab(tabButton.index)
                }

                background: Rectangle {
                    color: tabButton.selected ? Theme.bg : (tabButton.hovered ? Theme.bg : "transparent")
                    opacity: tabButton.selected || tabButton.hovered ? 1 : 0
                    radius: Theme.radiusM
                    border.color: tabButton.selected ? Theme.border : "transparent"

                    // Hides the bottom border and the lower corners of the selected tab.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 1
                        anchors.rightMargin: 1
                        height: Theme.radiusM + 1
                        color: parent.color
                    }

                    FocusRing {
                        control: tabButton
                    }
                }

                contentItem: RowLayout {
                    spacing: Theme.spacingS

                    StateDot {
                        visible: !tabButton.settingsTab
                        kind: !tabButton.stableTab || tabButton.stableTab.state === DeviceTabViewModel.ChoosingDevice
                              ? "idle" : tabButton.stableTab.connection.stateKind
                    }

                    Icon {
                        visible: tabButton.settingsTab
                        symbol: "settings"
                        size: 16
                        color: tabButton.selected ? Theme.text : Theme.textMuted
                    }

                    Label {
                        objectName: "tabTitle"
                        Layout.fillWidth: true
                        text: tabButton.title
                        font: tabButton.selected ? Theme.fontUiStrong : Theme.fontUi
                        color: tabButton.selected ? Theme.text : Theme.textMuted
                        elide: Text.ElideRight
                    }

                    HToolButton {
                        objectName: "tabClose"
                        implicitWidth: 22
                        implicitHeight: 22
                        padding: 2
                        iconName: "close"
                        icon.width: 16
                        icon.height: 16
                        iconColor: Theme.textMuted
                        text: qsTr("Close tab")
                        shortcutId: tabButton.selected ? "tab.close" : ""
                        display: AbstractButton.IconOnly
                        focusPolicy: Qt.NoFocus
                        onClicked: AppViewModel.closeTab(tabButton.index)
                    }
                }
            }
        }

        // A wrapper for the tooltip of the disabled "+" (a disabled control gets no hover events).
        Item {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: addButton.implicitWidth
            implicitHeight: addButton.implicitHeight

            HToolButton {
                id: addButton

                objectName: "tabAdd"
                iconName: "add"
                iconColor: Theme.textMuted
                enabled: AppViewModel.tabs.canAdd
                text: qsTr("New device")
                shortcutId: "tab.new"
                display: AbstractButton.IconOnly
                onClicked: AppViewModel.newTab()
                Accessible.description: addButton.enabled ? "" : addLimitTip.text
            }

            HoverHandler {
                id: addHover
            }

            ToolTip {
                id: addLimitTip

                objectName: "tabLimitToolTip"
                visible: !addButton.enabled && addHover.hovered
                text: qsTr("At most %1 devices can be open.").arg(AppViewModel.maxTabs)
            }
        }

        Item {
            Layout.fillWidth: true
        }
    }
}
