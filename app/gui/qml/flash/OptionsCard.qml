// Flash step 3: options. "Halt the CPU after flashing" (not for modem firmware) and "Allow writing
// UICR" with a warning, only when the image has UICR records (ADR-0036).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

FlashCard {
    id: card

    required property FlashViewModel vm

    number: 3
    title: qsTr("Options")
    visible: card.vm ? card.vm.haltAvailable || card.vm.source.hasUicr : false

    CheckBox {
        objectName: "haltOption"
        visible: card.vm ? card.vm.haltAvailable : false
        text: qsTr("Halt the CPU after flashing")
        checked: card.vm ? card.vm.halt : false
        onToggled: card.vm.halt = checked
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: card.vm ? card.vm.source.hasUicr : false
        spacing: Theme.spacingXs

        CheckBox {
            objectName: "uicrOption"
            text: qsTr("Allow writing UICR")
            checked: card.vm ? card.vm.source.allowUicr : false
            onToggled: card.vm.source.allowUicr = checked
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingXl
            text: qsTr("The firmware contains UICR configuration. Writing it changes the device configuration; the Product Information Block is kept.")
            font: Theme.fontUiSmall
            color: Theme.warning
            wrapMode: Text.Wrap
        }
    }
}
