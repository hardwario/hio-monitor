// Settings → J-Link (0006): the JLinkARM library (Automatic / Custom path, checked before it is
// saved; ADR-0006, ADR-0029) and the defaults of the ⋯ Advanced popover for probes without their
// own values (ADR-0052): speed with "Automatic speed" (ADR-0090, ADR-0111; the ADR-0110 warning
// below 125 kHz), RTT latency, auto-reconnect, reset on connect. The link "Run the setup wizard"
// opens the setup wizard (0008) and is hidden until it is registered.
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
        label: qsTr("Library")

        RowLayout {
            spacing: Theme.spacingM

            RadioButton {
                objectName: "jlinkAuto"
                text: qsTr("Automatic")
                checked: section.vm.jlinkLibraryMode === "auto"
                onClicked: section.vm.jlinkLibraryMode = "auto"
            }

            RadioButton {
                objectName: "jlinkCustom"
                text: qsTr("Custom path")
                checked: section.vm.jlinkLibraryMode === "custom"
                onClicked: section.vm.jlinkLibraryMode = "custom"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: section.vm.jlinkLibraryMode === "custom"
            spacing: Theme.spacingS

            TextField {
                objectName: "jlinkPathField"
                Layout.fillWidth: true
                readOnly: true
                font: Theme.fontMonoSmall
                text: section.vm.jlinkLibraryPath
                placeholderText: qsTr("Choose the JLinkARM library file")
                Accessible.name: qsTr("J-Link library path")
            }

            HButton {
                objectName: "jlinkBrowse"
                text: qsTr("Browse…")
                enabled: !section.vm.jlinkPathChecking
                onClicked: jlinkDialog.open()
            }
        }

        Label {
            objectName: "jlinkPathError"
            Layout.fillWidth: true
            visible: text !== ""
            text: section.vm.jlinkPathChecking ? qsTr("Checking the library…") : section.vm.jlinkPathError
            font: Theme.fontUiSmall
            color: section.vm.jlinkPathChecking ? Theme.textMuted : Theme.error
            wrapMode: Text.Wrap
        }

        Label {
            objectName: "jlinkDetected"
            Layout.fillWidth: true
            text: section.vm.jlinkStatusText
            visible: text !== ""
            font: Theme.fontUiSmall
            color: section.vm.jlinkStatusProblem ? Theme.error : Theme.textMuted
            wrapMode: Text.WrapAnywhere
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Default speed (kHz)")

        Switch {
            objectName: "defaultAutoSpeed"
            text: qsTr("Automatic speed")
            checked: section.vm.autoSpeed
            onToggled: section.vm.autoSpeed = checked
        }

        StepSpinBox {
            id: speedBox

            objectName: "defaultSpeedBox"
            // While automatic, the box keeps the last fixed value (ADR-0111).
            enabled: !section.vm.autoSpeed
            from: section.vm.minSpeedKhz
            to: section.vm.maxSpeedKhz
            step: 100
            value: section.vm.defaultSpeedKhz
            Accessible.name: qsTr("Default speed (kHz)")
            onValueModified: section.vm.defaultSpeedKhz = speedBox.value
            onStepped: newValue => section.vm.defaultSpeedKhz = newValue
        }

        Label {
            objectName: "defaultSpeedWarning"
            Layout.fillWidth: true
            visible: text !== ""
            text: section.vm.speedWarning
            font: Theme.fontUiSmall
            color: Theme.warning
            wrapMode: Text.Wrap
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("RTT latency (ms)")

        StepSpinBox {
            id: latencyBox

            objectName: "defaultLatencyBox"
            from: section.vm.minLatencyMs
            to: section.vm.maxLatencyMs
            step: 10
            value: section.vm.defaultLatencyMs
            Accessible.name: qsTr("RTT latency (ms)")
            onValueModified: section.vm.defaultLatencyMs = latencyBox.value
            onStepped: newValue => section.vm.defaultLatencyMs = newValue
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Auto-reconnect")

        Switch {
            objectName: "defaultAutoReconnect"
            checked: section.vm.autoReconnect
            Accessible.name: qsTr("Auto-reconnect")
            onToggled: section.vm.autoReconnect = checked
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Reset on connect")

        Switch {
            objectName: "defaultResetOnConnect"
            checked: section.vm.resetOnConnect
            Accessible.name: qsTr("Reset on connect")
            onToggled: section.vm.resetOnConnect = checked
        }
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("These are the defaults for probes without their own options (⋯ Advanced in the device bar). Changes apply on the next connect.")
        font: Theme.fontUiSmall
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }

    // Amended by 0008: hidden until the setup wizard is registered.
    HButton {
        objectName: "jlinkSetupWizard"
        kind: "ghost"
        visible: AppViewModel.launcher.canOpen(section.vm.setupWizardId)
        text: qsTr("Run the setup wizard")
        onClicked: AppViewModel.launcher.start(section.vm.setupWizardId)
    }

    FileDialog {
        id: jlinkDialog

        title: qsTr("Choose the JLinkARM library")
        fileMode: FileDialog.OpenFile
        onAccepted: section.vm.setJlinkLibraryPath(jlinkDialog.selectedFile)
    }
}
