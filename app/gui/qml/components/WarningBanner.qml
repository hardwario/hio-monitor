// One-line warning banner (warningPanelBg) with an optional action and Dismiss, e.g. "J-Link
// firmware is outdated" or "312 lines were dropped".
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: banner

    property string text: ""
    property string actionText: ""

    signal actionTriggered
    signal dismissed

    implicitHeight: row.implicitHeight + 2 * Theme.spacingS
    color: Theme.warningPanelBg
    Accessible.role: Accessible.AlertMessage
    Accessible.name: banner.text

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingS

        Icon {
            symbol: "warning"
            color: Theme.warning
        }

        Label {
            objectName: "bannerText"
            Layout.fillWidth: true
            text: banner.text
            color: Theme.text
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        HButton {
            visible: banner.actionText !== ""
            text: banner.actionText
            onClicked: banner.actionTriggered()
        }

        HToolButton {
            objectName: "bannerDismiss"
            iconName: "close"
            text: qsTr("Dismiss")
            display: AbstractButton.IconOnly
            onClicked: banner.dismissed()
        }
    }
}
