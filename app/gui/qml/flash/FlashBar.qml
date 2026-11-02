// The sticky bottom bar of the Flash page (design system "FlashBar"): the summary of kind + file,
// or the reason Flash is disabled; the Ctrl+Enter hint and the primary Flash button (bolt icon).
// Plan 0028 (ADR-0204, D22): the background and the top border span the page; the row is laid out
// in the form column the page passes (columnX, columnWidth), so the button ends under the form.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: bar

    required property FlashViewModel vm
    // The form column of the page (x relative to the bar, width); default: 24 px margins.
    property real columnX: Theme.spacingXl
    property real columnWidth: bar.width - 2 * Theme.spacingXl

    implicitHeight: row.implicitHeight + 2 * Theme.spacingM
    color: Theme.surface

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.border
    }

    RowLayout {
        id: row

        objectName: "flashBarRow"
        x: bar.columnX
        width: bar.columnWidth
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: Theme.spacingM

        Icon {
            visible: bar.vm ? bar.vm.canFlash : false
            symbol: "success"
            color: Theme.success
            size: 18
        }

        Label {
            textFormat: Text.PlainText
            objectName: "flashBarText"
            Layout.fillWidth: true
            text: bar.vm ? (bar.vm.canFlash ? bar.vm.flashSummary : bar.vm.cannotFlashReason) : ""
            color: bar.vm ? (bar.vm.canFlash ? Theme.text : Theme.textMuted) : Theme.text
            elide: Text.ElideMiddle
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Label {
            textFormat: Text.PlainText
            text: AppViewModel.shortcuts.sequenceText("flash.start")
            font: Theme.fontUiSmall
            color: Theme.textMuted
        }

        HButton {
            objectName: "flashButton"
            kind: "primary"
            iconName: "flash"
            implicitHeight: 36
            leftPadding: Theme.spacingXl
            rightPadding: Theme.spacingXl
            text: qsTr("Flash")
            enabled: bar.vm ? bar.vm.canFlash : false
            toolTipText: bar.vm ? (bar.vm.canFlash ? "" : bar.vm.cannotFlashReason) : ""
            Accessible.description: bar.vm ? (bar.vm.canFlash ? bar.vm.flashSummary : bar.vm.cannotFlashReason) : ""
            onClicked: bar.vm.flash()
        }
    }
}
