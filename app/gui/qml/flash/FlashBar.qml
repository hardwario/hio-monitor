// The sticky bottom bar of the Flash page (design system "FlashBar"): the summary of kind + file,
// or the reason Flash is disabled; the Ctrl+Enter hint and the primary Flash button.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: bar

    required property FlashViewModel vm

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

        anchors.fill: parent
        anchors.leftMargin: Theme.spacingXl
        anchors.rightMargin: Theme.spacingXl
        spacing: Theme.spacingM

        Icon {
            visible: bar.vm ? bar.vm.canFlash : false
            symbol: "success"
            color: Theme.success
            size: 18
        }

        Label {
            objectName: "flashBarText"
            Layout.fillWidth: true
            text: bar.vm ? (bar.vm.canFlash ? bar.vm.flashSummary : bar.vm.cannotFlashReason) : ""
            color: bar.vm ? (bar.vm.canFlash ? Theme.text : Theme.textMuted) : Theme.text
            elide: Text.ElideMiddle
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Label {
            text: AppViewModel.shortcuts.sequenceText("flash.start")
            font: Theme.fontUiSmall
            color: Theme.textMuted
        }

        HButton {
            objectName: "flashButton"
            kind: "primary"
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
