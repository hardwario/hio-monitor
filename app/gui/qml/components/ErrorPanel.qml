// Inline error card for every hio::Error (UX 0003, design system § 5): headline (message),
// "Probably:" cause, "What to do:" suggestion, the primary action of the error category (a page
// may override `primaryAction`), an optional secondary action, "Help me fix this" when the error's
// wizard is registered (plan/features/0007: it opens the wizard with the error as context) and a
// Details expander (code, detail, Copy).
// `compact` (window height < 700 px) collapses it to the headline, the primary action and an
// expand chevron.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: panel

    property errorInfo error
    property int primaryAction: panel.error.primaryAction
    property string secondaryActionText: ""
    property bool compact: false
    // The tab whose wizard "Help me fix this" opens (-1 = the current tab).
    property int tabIndex: -1
    property bool detailsExpanded: false
    property bool userExpanded: false
    readonly property bool collapsed: panel.compact && !panel.userExpanded
    readonly property var actionTexts: [qsTr("Reconnect"), qsTr("Choose another file"),
                                        qsTr("Open Settings"), qsTr("Retry"), qsTr("Dismiss")]

    signal actionTriggered(int action)
    signal secondaryActionTriggered

    visible: panel.error.valid
    implicitHeight: content.implicitHeight + 2 * Theme.spacingM
    implicitWidth: content.implicitWidth + 2 * Theme.spacingM + 4
    color: Theme.errorPanelBg
    radius: Theme.radiusM
    border.color: Theme.border
    Accessible.role: Accessible.AlertMessage
    Accessible.name: panel.error.message
    Accessible.description: panel.error.cause + " " + panel.error.suggestion

    Rectangle {
        width: 4
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        radius: 2
        color: Theme.error
    }

    RowLayout {
        id: content

        anchors.fill: parent
        anchors.margins: Theme.spacingM
        anchors.leftMargin: Theme.spacingM + 4
        spacing: Theme.spacingM

        Icon {
            Layout.alignment: Qt.AlignTop
            symbol: "error"
            color: Theme.error
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS

                Label {
                    objectName: "errorHeadline"
                    Layout.fillWidth: true
                    text: panel.error.message
                    font: Theme.fontUiStrong
                    color: Theme.text
                    wrapMode: Text.Wrap
                    maximumLineCount: panel.collapsed ? 1 : 4
                    elide: Text.ElideRight
                }

                HButton {
                    objectName: "errorPrimaryCompact"
                    visible: panel.collapsed
                    kind: "primary"
                    text: panel.actionTexts[panel.primaryAction]
                    onClicked: panel.actionTriggered(panel.primaryAction)
                }

                HToolButton {
                    visible: panel.compact
                    iconName: panel.userExpanded ? "collapse" : "expand"
                    text: panel.userExpanded ? qsTr("Show less") : qsTr("Show more")
                    display: AbstractButton.IconOnly
                    onClicked: panel.userExpanded = !panel.userExpanded
                }
            }

            Label {
                objectName: "errorCause"
                Layout.fillWidth: true
                visible: !panel.collapsed && panel.error.cause !== ""
                text: qsTr("Probably: %1").arg(panel.error.cause)
                color: Theme.textMuted
                wrapMode: Text.Wrap
            }

            Label {
                objectName: "errorSuggestion"
                Layout.fillWidth: true
                visible: !panel.collapsed && panel.error.suggestion !== ""
                text: qsTr("What to do: %1").arg(panel.error.suggestion)
                color: Theme.text
                wrapMode: Text.Wrap
            }

            Flow {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingS
                visible: !panel.collapsed
                spacing: Theme.spacingS

                HButton {
                    objectName: "errorPrimary"
                    kind: "primary"
                    text: panel.actionTexts[panel.primaryAction]
                    onClicked: panel.actionTriggered(panel.primaryAction)
                }

                HButton {
                    objectName: "errorSecondary"
                    visible: panel.secondaryActionText !== ""
                    text: panel.secondaryActionText
                    onClicked: panel.secondaryActionTriggered()
                }

                // M4 (plan/features/0007): shown only when the error's wizard is registered.
                HButton {
                    objectName: "errorWizard"
                    visible: AppViewModel.launcher.canOpen(panel.error.wizard)
                    text: qsTr("Help me fix this")
                    onClicked: AppViewModel.launcher.open(panel.error.wizard, panel.error, panel.tabIndex)
                }

                HButton {
                    objectName: "errorDetailsToggle"
                    kind: "ghost"
                    text: qsTr("Details")
                    iconName: panel.detailsExpanded ? "collapse" : "expand"
                    Accessible.description: panel.detailsExpanded ? qsTr("Expanded") : qsTr("Collapsed")
                    onClicked: panel.detailsExpanded = !panel.detailsExpanded
                }
            }

            Rectangle {
                objectName: "errorDetails"
                Layout.fillWidth: true
                visible: !panel.collapsed && panel.detailsExpanded
                implicitHeight: detailsRow.implicitHeight + 2 * Theme.spacingS
                color: Theme.consoleBg
                border.color: Theme.border
                radius: Theme.radiusS

                RowLayout {
                    id: detailsRow

                    anchors.fill: parent
                    anchors.margins: Theme.spacingS

                    TextEdit {
                        objectName: "errorDetailsText"
                        Layout.fillWidth: true
                        text: panel.error.detailsText()
                        font: Theme.fontMonoSmall
                        color: Theme.text
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        Accessible.name: qsTr("Error details")
                    }

                    HToolButton {
                        Layout.alignment: Qt.AlignTop
                        iconName: "copy"
                        text: qsTr("Copy")
                        onClicked: AppViewModel.copyToClipboard(panel.error.detailsText())
                    }
                }
            }
        }
    }
}
