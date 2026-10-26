// Settings → About (0006, extended by 0016 C2): version with the package kind, build, Qt, J-Link
// and nRF Util versions [Copy]; updates (automatic check, pre-releases, last check, [Check now]);
// the command line of this installation [Copy]; the licence files of this installation (each
// opens the file); links to the documentation, the firmware catalog and the source code.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: section

    readonly property SettingsViewModel vm: AppViewModel.globalPageViewModel("settings") as SettingsViewModel
    readonly property UpdateViewModel update: AppViewModel.update

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
        objectName: "updatesRow"
        Layout.fillWidth: true
        label: qsTr("Updates")
        description: qsTr("The check asks GitHub for the list of releases. No data about you or your devices is sent.")

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            CheckBox {
                objectName: "updateAutoCheck"
                text: qsTr("Check for updates automatically")
                checked: section.update.autoCheck
                onToggled: section.update.autoCheck = checked
            }

            CheckBox {
                objectName: "updatePrerelease"
                text: qsTr("Include pre-releases (beta, release candidates)")
                checked: section.update.includePrerelease
                onToggled: section.update.includePrerelease = checked
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS

                BusyIndicator {
                    objectName: "updateChecking"
                    visible: section.update.state === UpdateViewModel.Checking
                    running: visible
                    Layout.preferredWidth: Theme.iconSize + Theme.spacingS
                    Layout.preferredHeight: Theme.iconSize + Theme.spacingS
                }

                Label {
                    objectName: "updateStatus"
                    Layout.fillWidth: true
                    text: section.update.lastChecked !== "" && section.update.state !== UpdateViewModel.Checking
                          && section.update.state !== UpdateViewModel.Failed
                          ? qsTr("Last checked: %1 — %2").arg(section.update.lastChecked).arg(section.update.statusText)
                          : section.update.statusText
                    color: section.update.state === UpdateViewModel.Failed ? Theme.error : Theme.text
                    wrapMode: Text.Wrap
                }

                HButton {
                    objectName: "updateDownloadAbout"
                    visible: section.update.state === UpdateViewModel.Available
                    text: qsTr("Download")
                    onClicked: section.update.download()
                }

                HButton {
                    objectName: "updateCheckNow"
                    text: qsTr("Check now")
                    enabled: section.update.state !== UpdateViewModel.Checking
                    onClicked: section.update.checkNow()
                }
            }
        }
    }

    SettingsRow {
        objectName: "cliRow"
        Layout.fillWidth: true
        label: qsTr("Command line")
        description: section.vm.cliOnPath ? qsTr("The hio command is on the PATH of new terminals.") : ""

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            RowLayout {
                Layout.fillWidth: true

                TextEdit {
                    objectName: "cliPath"
                    Layout.fillWidth: true
                    text: section.vm.cliCommand
                    font: Theme.fontMonoSmall
                    color: Theme.text
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    Accessible.role: Accessible.StaticText
                    Accessible.name: section.vm.cliCommand
                }

                HButton {
                    objectName: "copyCliPath"
                    text: qsTr("Copy")
                    onClicked: {
                        AppViewModel.copyToClipboard(section.vm.cliCommand);
                        AppViewModel.showToast(qsTr("Copied."));
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: section.vm.cliSymlinkCommand !== ""

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Create a symlink: %1").arg(section.vm.cliSymlinkCommand)
                    font: Theme.fontMonoSmall
                    color: Theme.textMuted
                    wrapMode: Text.WrapAnywhere
                }

                HButton {
                    objectName: "copyCliSymlink"
                    text: qsTr("Copy")
                    onClicked: {
                        AppViewModel.copyToClipboard(section.vm.cliSymlinkCommand);
                        AppViewModel.showToast(qsTr("Copied."));
                    }
                }
            }
        }
    }

    SettingsRow {
        Layout.fillWidth: true
        label: qsTr("Licences")
        description: qsTr("SEGGER J-Link Software and Nordic nRF Util are not included; they are used from your installation.")

        Flow {
            objectName: "licenceFiles"
            Layout.fillWidth: true
            spacing: Theme.spacingS

            Repeater {
                model: section.vm.licenceFiles

                delegate: HButton {
                    required property string name
                    required property url url

                    kind: "ghost"
                    text: name
                    enabled: url.toString() !== ""
                    onClicked: Qt.openUrlExternally(url)
                }
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
