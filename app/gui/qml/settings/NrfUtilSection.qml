// Settings → nRF Util (0006; ADR-0016, ADR-0042): Automatic (PATH and the install directories) or a
// custom path (checked before it is saved); the detected version or "not found — needed only for
// LTE modem firmware". The link "Run the setup wizard" (0008) is hidden until it is registered.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: section

    readonly property SettingsViewModel vm: AppViewModel.globalPageViewModel("settings") as SettingsViewModel
    spacing: Theme.spacingM

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Path")

        RowLayout {
            spacing: Theme.spacingM

            RadioButton {
                objectName: "nrfUtilAuto"
                text: qsTr("Automatic")
                checked: section.vm.nrfUtilMode === "auto"
                onClicked: section.vm.nrfUtilMode = "auto"
            }

            RadioButton {
                objectName: "nrfUtilCustom"
                text: qsTr("Custom path")
                checked: section.vm.nrfUtilMode === "custom"
                onClicked: section.vm.nrfUtilMode = "custom"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: section.vm.nrfUtilMode === "custom"
            spacing: Theme.spacingS

            TextField {
                objectName: "nrfUtilPathField"
                Layout.fillWidth: true
                readOnly: true
                font: Theme.fontMonoSmall
                text: section.vm.nrfUtilPath
                placeholderText: qsTr("Choose the nrfutil program")
                Accessible.name: qsTr("nRF Util path")
            }

            HButton {
                objectName: "nrfUtilBrowse"
                text: qsTr("Browse…")
                enabled: !section.vm.nrfUtilPathChecking
                onClicked: nrfUtilDialog.open()
            }
        }

        Label {
            objectName: "nrfUtilPathError"
            Layout.fillWidth: true
            visible: text !== ""
            text: section.vm.nrfUtilPathChecking ? qsTr("Checking nRF Util…") : section.vm.nrfUtilPathError
            font: Theme.fontUiSmall
            color: section.vm.nrfUtilPathChecking ? Theme.textMuted : Theme.error
            wrapMode: Text.Wrap
        }

        Label {
            objectName: "nrfUtilDetected"
            Layout.fillWidth: true
            text: section.vm.nrfUtilStatusText
            visible: text !== ""
            font: Theme.fontUiSmall
            color: section.vm.nrfUtilStatusProblem ? Theme.warning : Theme.textMuted
            wrapMode: Text.WrapAnywhere
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingS

            HButton {
                objectName: "nrfUtilInstall"
                kind: "ghost"
                visible: section.vm.nrfUtilStatusProblem
                text: qsTr("How to install ↗")
                onClicked: Qt.openUrlExternally(section.vm.nrfUtilInstallUrl)
            }

            // Amended by 0008: hidden until the setup wizard is registered.
            HButton {
                objectName: "nrfUtilSetupWizard"
                kind: "ghost"
                visible: AppViewModel.launcher.canOpen(section.vm.setupWizardId)
                text: qsTr("Run the setup wizard")
                onClicked: AppViewModel.launcher.start(section.vm.setupWizardId)
            }

            HButton {
                objectName: "nrfUtilRefresh"
                kind: "ghost"
                text: qsTr("Check again")
                onClicked: section.vm.dependencies.refresh()
            }
        }
    }

    FileDialog {
        id: nrfUtilDialog

        title: qsTr("Choose the nrfutil program")
        fileMode: FileDialog.OpenFile
        onAccepted: section.vm.setNrfUtilPath(nrfUtilDialog.selectedFile)
    }
}
