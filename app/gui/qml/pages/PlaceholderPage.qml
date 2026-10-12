// Placeholder of the pages that later plans implement (Console 0005, Flash and Settings 0006).
// The Settings placeholder offers the language and the theme and shows the J-Link library path
// read-only (plan/features/0004-m3-gui-shell.md, Non-goals).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Item {
    id: page

    property DeviceTabViewModel tab
    property PlaceholderViewModel vm
    readonly property string pageId: page.vm ? page.vm.pageId : ""
    readonly property bool settings: page.pageId === "settings"

    ScrollView {
        id: scroll

        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            x: Math.max(Theme.spacingXl, (scroll.availableWidth - width) / 2)
            width: Math.min(760, scroll.availableWidth - 2 * Theme.spacingXl)
            spacing: Theme.spacingL

            Item {
                implicitHeight: Theme.spacingS
            }

            Label {
                objectName: "placeholderTitle"
                // revision: re-read after a language switch.
                text: AppViewModel.pages.revision >= 0 ? AppViewModel.pages.title(page.pageId) : ""
                font: Theme.fontTitle
                color: Theme.text
            }

            Label {
                Layout.fillWidth: true
                text: page.settings ? qsTr("The complete settings page comes with a later update. Until then:")
                                    : qsTr("This page comes with a later update of HARDWARIO Monitor.")
                color: Theme.textMuted
                wrapMode: Text.Wrap
            }

            GridLayout {
                Layout.fillWidth: true
                visible: page.settings
                columns: 2
                columnSpacing: Theme.spacingL
                rowSpacing: Theme.spacingM

                Label {
                    text: qsTr("Language")
                    color: Theme.text
                }

                ComboBox {
                    id: languageBox

                    objectName: "languageSelector"
                    Layout.preferredWidth: 220
                    model: AppViewModel.availableLanguages.map(
                               language => language.nativeName !== "" ? language.nativeName : qsTr("System"))
                    currentIndex: AppViewModel.availableLanguages.findIndex(language => language.code === AppViewModel.language)
                    Accessible.name: qsTr("Language")
                    onActivated: index => AppViewModel.setLanguage(AppViewModel.availableLanguages[index].code)
                }

                Label {
                    text: qsTr("Theme")
                    color: Theme.text
                }

                ComboBox {
                    id: themeBox

                    objectName: "themeSelector"
                    Layout.preferredWidth: 220
                    textRole: "text"
                    valueRole: "value"
                    model: [{ value: "system", text: qsTr("System") },
                            { value: "light", text: qsTr("Light") },
                            { value: "dark", text: qsTr("Dark") }]
                    currentIndex: ["system", "light", "dark"].indexOf(AppViewModel.theme)
                    Accessible.name: qsTr("Theme")
                    onActivated: AppViewModel.theme = themeBox.currentValue
                }

                Label {
                    Layout.alignment: Qt.AlignTop
                    text: qsTr("J-Link library")
                    color: Theme.text
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs

                    TextEdit {
                        objectName: "libraryPath"
                        Layout.fillWidth: true
                        text: AppViewModel.jlinkLibraryPath !== "" ? AppViewModel.jlinkLibraryPath : qsTr("Not found")
                        font: Theme.fontMonoSmall
                        color: Theme.text
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        Accessible.name: qsTr("J-Link library")
                    }

                    Label {
                        visible: AppViewModel.jlinkLibraryVersion !== ""
                        text: qsTr("Version %1").arg(AppViewModel.jlinkLibraryVersion)
                        font: Theme.fontUiSmall
                        color: Theme.textMuted
                    }
                }

                Label {
                    text: qsTr("Version")
                    color: Theme.text
                }

                Label {
                    objectName: "buildInfo"
                    Layout.fillWidth: true
                    text: AppViewModel.buildInfo
                    font: Theme.fontMonoSmall
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
