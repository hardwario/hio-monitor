// "Create diagnostic report" (plan/features/0007, ADR-0075): collecting (spinner, Cancel), then
// the read-only summary.txt, the privacy note and [Save report…] (native file dialog, Documents,
// hio-diagnostics-YYYYMMDD-HHMMSS.zip) · [Copy summary] · [Close]. A failed save shows an
// ErrorPanel (diag.report_write_failed); after saving, [Show in folder]. AppViewModel.report.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import HardwarioMonitor

Dialog {
    id: dialog

    readonly property ReportViewModel report: AppViewModel.report
    readonly property bool collecting: dialog.report.state === "collecting"

    objectName: "reportDialog"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(720, (parent ? parent.width : 720) - 48)
    height: Math.min(560, (parent ? parent.height : 560) - 48)
    padding: Theme.spacingXl
    closePolicy: Popup.CloseOnEscape
    visible: dialog.report.open
    title: qsTr("Create diagnostic report")
    onClosed: if (dialog.report.open) dialog.report.close()
    onOpened: closeButton.forceActiveFocus()
    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusL
        border.color: Theme.border
    }

    header: Label {
        objectName: "reportTitle"
        text: dialog.title
        font: Theme.fontTitle
        color: Theme.text
        padding: Theme.spacingXl
        bottomPadding: 0
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingM

        RowLayout {
            objectName: "reportCollecting"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingXl
            visible: dialog.collecting
            spacing: Theme.spacingS

            BusyIndicator {
                running: dialog.collecting && !AppViewModel.reducedMotion
            }

            Label {
                text: qsTr("Collecting information…")
                color: Theme.text
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        ScrollView {
            id: summaryScroll

            objectName: "reportSummary"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !dialog.collecting
            clip: true

            TextArea {
                objectName: "reportSummaryText"
                text: dialog.report.summary
                readOnly: true
                selectByMouse: true
                font: Theme.fontMonoSmall
                color: Theme.text
                wrapMode: TextEdit.NoWrap
                padding: Theme.spacingS
                topPadding: Theme.spacingS
                bottomPadding: Theme.spacingS
                Accessible.name: qsTr("Report summary")
                background: Rectangle {
                    color: Theme.consoleBg
                    border.color: Theme.border
                    radius: Theme.radiusS
                }
            }
        }

        Label {
            objectName: "reportNote"
            Layout.fillWidth: true
            visible: !dialog.collecting
            text: qsTr("The report contains this summary, the recent application logs and the settings file. Your user name and home folder are replaced. The report stays on your computer — attach it to your support request.")
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }

        ErrorPanel {
            objectName: "reportError"
            Layout.fillWidth: true
            visible: dialog.report.state === "error"
            error: dialog.report.error
            primaryAction: ErrorAction.Dismiss
            onActionTriggered: saveDialog.open()
        }

        Label {
            objectName: "reportSaved"
            Layout.fillWidth: true
            visible: dialog.report.state === "saved"
            text: qsTr("Saved to %1").arg(dialog.report.savedPath)
            color: Theme.success
            wrapMode: Text.WrapAnywhere
        }
    }

    footer: Flow {
        layoutDirection: Qt.RightToLeft
        spacing: Theme.spacingS
        padding: Theme.spacingXl
        topPadding: 0

        HButton {
            id: closeButton

            objectName: "reportClose"
            text: dialog.collecting ? qsTr("Cancel") : qsTr("Close")
            onClicked: dialog.report.close()
        }

        HButton {
            objectName: "reportCopy"
            visible: !dialog.collecting
            text: qsTr("Copy summary")
            onClicked: dialog.report.copySummary()
        }

        HButton {
            objectName: "reportShowFolder"
            visible: dialog.report.state === "saved"
            text: qsTr("Show in folder")
            onClicked: dialog.report.showInFolder()
        }

        HButton {
            objectName: "reportSave"
            visible: !dialog.collecting
            enabled: dialog.report.state !== "saving"
            kind: "primary"
            text: qsTr("Save report…")
            onClicked: saveDialog.open()
        }
    }

    FileDialog {
        id: saveDialog

        title: qsTr("Save diagnostic report")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Zip archives (*.zip)")]
        currentFolder: dialog.report.defaultFolder
        selectedFile: dialog.report.defaultFolder + "/" + dialog.report.defaultFileName
        onAccepted: dialog.report.save(saveDialog.selectedFile)
    }
}
