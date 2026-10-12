// Confirmation of a destructive or interrupting action (ADR-0038, design system § 5): the title
// as a question, what will be lost (bulleted), the consequence in bold, Cancel (default, Esc) and
// a button that repeats the action ("Erase everything"), red when destructive. No "don't ask
// again".
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Dialog {
    id: dialog

    property string body: ""
    property list<string> items: []
    property string consequence: ""
    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Cancel")
    property bool destructive: true

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(480, (parent ? parent.width : 480) - 2 * Theme.spacingXl)
    padding: Theme.spacingXl
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusL
        border.color: Theme.border
    }
    onOpened: rejectButton.forceActiveFocus()

    header: Label {
        objectName: "dialogTitle"
        text: dialog.title
        font: Theme.fontTitle
        color: Theme.text
        wrapMode: Text.Wrap
        padding: Theme.spacingXl
        bottomPadding: 0
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS

        Label {
            objectName: "dialogBody"
            Layout.fillWidth: true
            visible: dialog.body !== ""
            text: dialog.body
            color: Theme.text
            wrapMode: Text.Wrap
        }

        Repeater {
            model: dialog.items

            delegate: Label {
                required property string modelData

                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingM
                text: "• " + modelData
                color: Theme.text
                wrapMode: Text.Wrap
            }
        }

        Label {
            Layout.fillWidth: true
            visible: dialog.consequence !== ""
            text: dialog.consequence
            font: Theme.fontUiStrong
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }

    footer: Flow {
        layoutDirection: Qt.RightToLeft
        spacing: Theme.spacingS
        padding: Theme.spacingXl
        topPadding: 0

        HButton {
            objectName: "dialogAccept"
            kind: dialog.destructive ? "danger" : "primary"
            text: dialog.acceptText
            onClicked: dialog.accept()
        }

        HButton {
            id: rejectButton

            objectName: "dialogReject"
            text: dialog.rejectText
            onClicked: dialog.reject()
        }
    }
}
