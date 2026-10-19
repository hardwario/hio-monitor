// One-line warning banner (warningPanelBg) with an optional action and Dismiss, e.g. "J-Link
// firmware is outdated" or "312 lines were dropped". A banner for a backend warning (`details`, an
// ErrorInfo) gets a [Details] toggle that shows the cause, the suggestion and the code with the
// technical detail (Copy), like the ErrorPanel (plan 0005 amendment of 0004, 2026-10-16).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: banner

    property string text: ""
    property string actionText: ""
    property errorInfo details
    readonly property bool hasDetails: banner.details.valid
                                       && (banner.details.detail !== "" || banner.details.cause !== ""
                                           || banner.details.suggestion !== "")
    property bool detailsExpanded: false

    signal actionTriggered
    signal dismissed

    implicitHeight: column.implicitHeight + 2 * Theme.spacingS
    color: Theme.warningPanelBg
    Accessible.role: Accessible.AlertMessage
    Accessible.name: banner.text

    onDetailsChanged: banner.detailsExpanded = false

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    ColumnLayout {
        id: column

        anchors.fill: parent
        anchors.topMargin: Theme.spacingS
        anchors.bottomMargin: Theme.spacingS
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingXs

        RowLayout {
            Layout.fillWidth: true
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
                objectName: "bannerDetailsToggle"
                visible: banner.hasDetails
                kind: "ghost"
                text: qsTr("Details")
                iconName: banner.detailsExpanded ? "collapse" : "expand"
                Accessible.description: banner.detailsExpanded ? qsTr("Expanded") : qsTr("Collapsed")
                onClicked: banner.detailsExpanded = !banner.detailsExpanded
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

        Label {
            objectName: "bannerCause"
            Layout.fillWidth: true
            visible: banner.detailsExpanded && banner.details.cause !== ""
            text: qsTr("Probably: %1").arg(banner.details.cause)
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }

        Label {
            objectName: "bannerSuggestion"
            Layout.fillWidth: true
            visible: banner.detailsExpanded && banner.details.suggestion !== ""
            text: qsTr("What to do: %1").arg(banner.details.suggestion)
            color: Theme.text
            wrapMode: Text.Wrap
        }

        Rectangle {
            objectName: "bannerDetails"
            Layout.fillWidth: true
            visible: banner.detailsExpanded
            implicitHeight: detailsRow.implicitHeight + 2 * Theme.spacingS
            color: Theme.consoleBg
            border.color: Theme.border
            radius: Theme.radiusS

            RowLayout {
                id: detailsRow

                anchors.fill: parent
                anchors.margins: Theme.spacingS

                TextEdit {
                    objectName: "bannerDetailsText"
                    Layout.fillWidth: true
                    text: banner.details.valid ? banner.details.detailsText() : ""
                    textFormat: TextEdit.PlainText
                    font: Theme.fontMonoSmall
                    color: Theme.text
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    Accessible.name: qsTr("Warning details")
                }

                HToolButton {
                    Layout.alignment: Qt.AlignTop
                    iconName: "copy"
                    text: qsTr("Copy")
                    onClicked: AppViewModel.copyToClipboard(banner.details.detailsText())
                }
            }
        }
    }
}
