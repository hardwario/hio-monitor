// The running operation (ADR-0049): title, "Step 1 of 2" for LTE bundles, the PhaseStepper with the
// phases of OperationProgress::plannedPhases, bytes, percent, elapsed time and Cancel
// ("Cancelling…" until the operation ends), and the console note.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

FlashCard {
    id: card

    required property FlashViewModel vm

    Label {
        objectName: "operationTitle"
        Layout.fillWidth: true
        text: card.vm ? card.vm.operationTitle : ""
        font: Theme.fontTitle
        color: Theme.text
        wrapMode: Text.Wrap
    }

    Label {
        objectName: "stepText"
        visible: card.vm ? card.vm.stepText !== "" : false
        text: card.vm ? card.vm.stepText : ""
        font: Theme.fontUiStrong
        color: Theme.textMuted
    }

    PhaseStepper {
        objectName: "phaseStepper"
        Layout.fillWidth: true
        phases: card.vm ? card.vm.phases : []
        progress: card.vm ? card.vm.progress : -1
        detailText: card.vm ? (card.vm.opState === FlashViewModel.Cancelling ? qsTr("Cancelling…") : card.vm.progressText) : ""
        cancelVisible: card.vm ? card.vm.cancelAvailable || card.vm.opState === FlashViewModel.Cancelling : false
        cancelEnabled: card.vm ? card.vm.cancelAvailable : false
        onCancelRequested: card.vm.cancel()
    }

    RowLayout {
        Layout.fillWidth: true
        visible: card.vm ? card.vm.consoleNote !== "" : false
        spacing: Theme.spacingS

        Icon {
            Layout.alignment: Qt.AlignTop
            symbol: "info"
            color: Theme.textMuted
            size: 18
        }

        Label {
            objectName: "consoleNote"
            Layout.fillWidth: true
            text: card.vm ? card.vm.consoleNote : ""
            font: Theme.fontUiSmall
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }
}
