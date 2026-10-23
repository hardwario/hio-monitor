// The Flash page of a device tab (plan/features/0006-m3-flash-settings.md; ADR-0049, ADR-0055):
// header with "Guide me" (flash wizard, shown only when it is registered, 0010), then 1 kind cards,
// 2 firmware file, 3 options, the maintenance section and the sticky Flash bar. While an operation
// runs the cards are replaced by the progress card; afterwards the result card is shown. Files can
// be dropped anywhere on the page. Gets `tab` and `vm` from the PageHost (page contract, 0004).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import HardwarioMonitor

Item {
    id: page

    property DeviceTabViewModel tab
    property FlashViewModel vm
    // The last view model: the PageHost resets `vm` to null while a closed tab's page is torn down
    // (its view model lives until after the page); the content keeps using it until then.
    property FlashViewModel model: null
    readonly property bool running: page.model !== null && (page.model.opState === FlashViewModel.Running
                                                            || page.model.opState === FlashViewModel.Cancelling)
    readonly property bool showResult: page.model !== null && (page.model.opState === FlashViewModel.Succeeded
                                                               || page.model.opState === FlashViewModel.Failed
                                                               || page.model.opState === FlashViewModel.Cancelled)

    Binding on model {
        value: page.vm
        when: page.vm !== null
        restoreMode: Binding.RestoreNone
    }

    objectName: "flashPage"

    function browse() {
        fileDialog.open();
    }

    Connections {
        target: page.model

        function onChooseFileRequested() {
            page.browse();
        }

        function onSettingsRequested(sectionId: string) {
            AppViewModel.openSettings(sectionId);
        }

        function onConfirmationRequested() {
            confirmDialog.open();
        }

        function onAnnouncement(text: string) {
            page.Accessible.announce(text);
        }
    }

    // The content exists only while the page has its view model (set by the PageHost after the
    // page was created).
    Loader {
        objectName: "flashContent"
        anchors.fill: parent
        active: page.model !== null
        sourceComponent: Component {
            ColumnLayout {
                spacing: 0

                ScrollView {
                    id: scroll

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth

                    ColumnLayout {
                        x: Math.max(Theme.spacingXl, (scroll.availableWidth - width) / 2)
                        width: Math.min(880, scroll.availableWidth - 2 * Theme.spacingXl)
                        spacing: Theme.spacingM

                        Item {
                            implicitHeight: Theme.spacingS
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingM

                            Label {
                                objectName: "flashTitle"
                                Layout.fillWidth: true
                                text: AppViewModel.pages.revision >= 0 ? AppViewModel.pages.title("flash") : ""
                                font: Theme.fontTitle
                                color: Theme.text
                                Accessible.role: Accessible.Heading
                            }

                            // Amended by 0010: the flash wizard; hidden until it is registered.
                            HButton {
                                objectName: "guideMe"
                                kind: "ghost"
                                visible: AppViewModel.launcher.canOpen("flash-firmware")
                                text: qsTr("Guide me")
                                onClicked: page.tab.openWizard("flash-firmware")
                            }
                        }

                        ProgressCard {
                            objectName: "progressCard"
                            Layout.fillWidth: true
                            visible: page.running
                            vm: page.model
                        }

                        ResultCard {
                            Layout.fillWidth: true
                            visible: page.showResult
                            vm: page.model
                        }

                        ColumnLayout {
                            objectName: "flashForm"
                            Layout.fillWidth: true
                            visible: !page.running && !page.showResult
                            spacing: Theme.spacingM

                            KindCards {
                                Layout.fillWidth: true
                                vm: page.model
                            }

                            SourceCard {
                                Layout.fillWidth: true
                                source: page.model.source
                                onBrowseRequested: page.browse()
                            }

                            OptionsCard {
                                Layout.fillWidth: true
                                vm: page.model
                            }

                            MaintenanceSection {
                                objectName: "maintenance"
                                Layout.fillWidth: true
                                vm: page.model
                                onConfirmRequested: recover => page.model.requestConfirmation(recover)
                            }
                        }

                        Item {
                            implicitHeight: Theme.spacingL
                        }
                    }
                }

                FlashBar {
                    objectName: "flashBar"
                    Layout.fillWidth: true
                    visible: !page.running && !page.showResult
                    vm: page.model
                }
            }
        }
    }

    // Only on the form: a drop on the progress or result card would replace the source behind it.
    DropArea {
        objectName: "flashDropArea"
        anchors.fill: parent
        enabled: !page.running && !page.showResult
        onDropped: drop => page.model.source.loadFile(drop.urls[0])
    }

    FileDialog {
        id: fileDialog

        title: qsTr("Choose a firmware file")
        fileMode: FileDialog.OpenFile
        nameFilters: page.model ? page.model.source.nameFilters : []
        onAccepted: page.model.source.loadFile(fileDialog.selectedFile)
    }

    ConfirmDialog {
        id: confirmDialog

        objectName: "maintenanceConfirm"
        title: page.model ? page.model.confirmation.title ?? "" : ""
        body: page.model ? page.model.confirmation.body ?? "" : ""
        items: page.model ? page.model.confirmation.items ?? [] : []
        consequence: page.model ? page.model.confirmation.consequence ?? "" : ""
        acceptText: page.model ? page.model.confirmation.acceptText ?? "" : ""
        onAccepted: page.model.confirm()
    }

    Shortcut {
        sequence: AppViewModel.shortcuts.portableText("flash.start")
        enabled: page.visible && page.model !== null && page.model.canFlash && !page.running && !page.showResult
        onActivated: page.model.flash()
    }

    Shortcut {
        sequence: AppViewModel.shortcuts.portableText("flash.browse")
        enabled: page.visible && page.model !== null && !page.running && !page.showResult
        onActivated: page.browse()
    }
}
