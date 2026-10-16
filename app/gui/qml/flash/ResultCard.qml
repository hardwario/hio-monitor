// The result of an operation (ADR-0049): success (text, the identified chip, [Open console]
// [Flash again]), failure (an ErrorPanel with [Try again] / "Choose another file"; an interrupted
// flash says so, ADR-0109) or cancel ("The device may not start until you flash it again.").
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: result

    required property FlashViewModel vm
    readonly property bool succeeded: result.vm !== null && result.vm.opState === FlashViewModel.Succeeded
    property errorInfo noError // default-constructed: invalid (no error)
    readonly property bool failed: result.vm !== null && result.vm.opState === FlashViewModel.Failed

    spacing: Theme.spacingM

    FlashCard {
        objectName: "resultCard"
        Layout.fillWidth: true
        visible: result.vm ? !result.failed || result.vm.resultText !== "" : false

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingS

            Icon {
                Layout.alignment: Qt.AlignTop
                symbol: result.succeeded ? "success" : "warning"
                color: result.succeeded ? Theme.success : Theme.warning
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                Label {
                    objectName: "resultText"
                    Layout.fillWidth: true
                    text: result.vm ? result.vm.resultText : ""
                    font: Theme.fontUiStrong
                    color: Theme.text
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: text
                }

                Label {
                    objectName: "chipText"
                    Layout.fillWidth: true
                    visible: result.vm ? result.vm.chipText !== "" : false
                    text: result.vm ? result.vm.chipText : ""
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            visible: !result.failed
            spacing: Theme.spacingS

            HButton {
                objectName: "openConsole"
                visible: result.vm ? result.succeeded && result.vm.flashOperation : false
                kind: "primary"
                text: qsTr("Open console")
                onClicked: result.vm.openConsole()
            }

            HButton {
                objectName: "flashAgain"
                text: result.vm ? (result.vm.flashOperation ? qsTr("Flash again") : qsTr("Done")) : ""
                onClicked: result.vm.dismissResult()
            }
        }
    }

    ErrorPanel {
        objectName: "resultError"
        Layout.fillWidth: true
        visible: result.failed
        error: result.vm ? result.vm.error : result.noError
        primaryAction: result.vm ? result.vm.errorAction : ErrorAction.Dismiss
        secondaryActionText: result.vm ? (result.vm.flashOperation ? qsTr("Back") : "") : ""
        onSecondaryActionTriggered: result.vm.dismissResult()
        onActionTriggered: action => result.vm.triggerErrorAction(action)
    }
}
