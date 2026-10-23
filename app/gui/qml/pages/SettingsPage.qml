// The global Settings page (Ctrl+,; plan/features/0006-m3-flash-settings.md, ADR-0059; shown by the
// Settings tab, 0021 ADR-0172): the
// sections registered in the PageRegistry (General, Console by 0005, J-Link, nRF Util, About), each
// under its title. Changes apply immediately; invalid input shows an inline error and is not saved.
// A settings file of a newer version is read-only: a banner and all controls disabled.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Item {
    id: page

    property DeviceTabViewModel tab
    property SettingsViewModel vm

    // 0021 (ADR-0172): scrolls to a section ("Open Settings" of an error panel, a wizard link).
    function showSection(sectionId: string) {
        if (sectionId === "")
            return;
        for (let i = 0; i < sections.count; ++i) {
            const item = sections.itemAt(i);
            if (item !== null && item.objectName === "section_" + sectionId) {
                const flickable = scroll.contentItem as Flickable;
                flickable.contentY = Math.max(0, Math.min(item.y, flickable.contentHeight - flickable.height));
                AppViewModel.clearSettingsSection(); // used: never re-applied later (review F8)
                return;
            }
        }
    }

    objectName: "settingsPage"
    onVisibleChanged: if (page.visible && page.vm) page.vm.pageShown()
    onVmChanged: {
        if (page.visible && page.vm)
            page.vm.pageShown();
        Qt.callLater(page.showSection, AppViewModel.settingsSection);
    }

    Connections {
        target: AppViewModel

        function onSettingsSectionRequested(sectionId: string) {
            Qt.callLater(page.showSection, sectionId);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: page.vm !== null

        WarningBanner {
            objectName: "readOnlyBanner"
            Layout.fillWidth: true
            visible: page.vm !== null && page.vm.readOnly
            text: qsTr("Settings were created by a newer version and are read-only.")
        }

        // A failed write (io.write_failed, settings.newer_schema): the value was not saved.
        Rectangle {
            objectName: "writeError"
            Layout.fillWidth: true
            visible: page.vm !== null && page.vm.writeError !== ""
            implicitHeight: writeErrorText.implicitHeight + 2 * Theme.spacingS
            color: Theme.errorPanelBg
            Accessible.role: Accessible.AlertMessage
            Accessible.name: writeErrorText.text

            Label {
                id: writeErrorText

                anchors.fill: parent
                anchors.margins: Theme.spacingS
                anchors.leftMargin: Theme.spacingM
                text: page.vm ? qsTr("The setting was not saved: %1").arg(page.vm.writeError) : ""
                color: Theme.text
                wrapMode: Text.Wrap
            }
        }

        ScrollView {
            id: scroll

            objectName: "settingsScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth

            ColumnLayout {
                x: Math.max(Theme.spacingXl, (scroll.availableWidth - width) / 2)
                width: Math.min(760, scroll.availableWidth - 2 * Theme.spacingXl)
                spacing: Theme.spacingL
                enabled: page.vm !== null && !page.vm.readOnly

                Item {
                    implicitHeight: Theme.spacingS
                }

                Label {
                    objectName: "settingsTitle"
                    text: AppViewModel.pages.revision >= 0 ? AppViewModel.pages.title("settings") : ""
                    font: Theme.fontTitle
                    color: Theme.text
                    Accessible.role: Accessible.Heading
                }

                Repeater {
                    id: sections

                    model: page.vm ? page.vm.sections : []

                    delegate: ColumnLayout {
                        id: section

                        required property var modelData

                        objectName: "section_" + section.modelData.sectionId
                        Layout.fillWidth: true
                        spacing: Theme.spacingS

                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingS
                            text: section.modelData.title
                            font: Theme.fontUiStrong
                            color: Theme.accent
                            Accessible.role: Accessible.Heading
                            Accessible.name: section.modelData.title
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.border
                        }

                        Loader {
                            objectName: "sectionLoader"
                            Layout.fillWidth: true
                            source: section.modelData.qml
                        }
                    }
                }

                Item {
                    implicitHeight: Theme.spacingXl
                }
            }
        }
    }
}
