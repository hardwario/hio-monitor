// Settings → About (0006): version, build, Qt, J-Link and nRF Util versions [Copy]; licences;
// links to the documentation, the firmware catalog and the source code.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: section

    readonly property SettingsViewModel vm: AppViewModel.globalPageViewModel("settings") as SettingsViewModel

    spacing: Theme.spacingM

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Version")

        TextEdit {
            objectName: "aboutText"
            Layout.fillWidth: true
            text: section.vm.aboutText
            font: Theme.fontMonoSmall
            color: Theme.text
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.WrapAnywhere
            Accessible.role: Accessible.StaticText
            Accessible.name: section.vm.aboutText
        }

        HButton {
            objectName: "copyAbout"
            text: qsTr("Copy")
            onClicked: section.vm.copyAbout()
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Licences")
        description: qsTr("SEGGER J-Link Software and Nordic nRF Util are not included; they are used from your installation.")

        Repeater {
            model: section.vm.licences

            delegate: Label {
                required property var modelData

                Layout.fillWidth: true
                text: qsTr("%1 (%2) — %3").arg(modelData.name).arg(modelData.licence).arg(modelData.use)
                font: Theme.fontUiSmall
                color: Theme.text
                wrapMode: Text.Wrap
            }
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Links")

        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingS

            HButton {
                kind: "ghost"
                text: qsTr("Documentation ↗")
                onClicked: Qt.openUrlExternally(section.vm.documentationUrl)
            }

            HButton {
                kind: "ghost"
                text: qsTr("Firmware catalog ↗")
                onClicked: Qt.openUrlExternally(section.vm.catalogUrl)
            }

            HButton {
                kind: "ghost"
                text: qsTr("Source code ↗")
                onClicked: Qt.openUrlExternally(section.vm.sourceUrl)
            }
        }
    }
}
