// F1 / Ctrl+/ overlay: every registered shortcut, grouped (Tabs, General, then per page)
// (ADR-0051). Shown while AppViewModel.shortcutOverlayVisible.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Dialog {
    id: overlay

    objectName: "shortcutOverlay"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, (parent ? parent.width : 560) - 2 * Theme.spacingXl)
    height: Math.min(implicitHeight, (parent ? parent.height : 600) - 2 * Theme.spacingXl)
    padding: Theme.spacingXl
    title: qsTr("Keyboard shortcuts")
    visible: AppViewModel.shortcutOverlayVisible
    onClosed: AppViewModel.shortcutOverlayVisible = false
    onOpened: closeButton.forceActiveFocus()
    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusL
        border.color: Theme.border
    }

    header: Label {
        text: overlay.title
        font: Theme.fontTitle
        color: Theme.text
        padding: Theme.spacingXl
        bottomPadding: 0
    }

    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: Theme.spacingL

            Repeater {
                model: AppViewModel.shortcuts.groups

                delegate: ColumnLayout {
                    id: group

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Theme.spacingS

                    Label {
                        text: group.modelData.title
                        font: Theme.fontUiStrong
                        color: Theme.textMuted
                    }

                    Repeater {
                        model: group.modelData.items

                        delegate: RowLayout {
                            id: item

                            required property var modelData

                            Layout.fillWidth: true
                            spacing: Theme.spacingL

                            Label {
                                objectName: "shortcutKeys"
                                Layout.preferredWidth: 180
                                text: item.modelData.keys
                                font: Theme.fontMonoSmall
                                color: Theme.text
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: item.modelData.description
                                color: Theme.text
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
    }

    footer: Flow {
        layoutDirection: Qt.RightToLeft
        padding: Theme.spacingXl
        topPadding: 0

        HButton {
            id: closeButton

            kind: "primary"
            text: qsTr("Close")
            onClicked: overlay.close()
        }
    }
}
