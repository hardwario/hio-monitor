// Main window of HARDWARIO Monitor (ADR-0043, ADR-0061): device tab strip on top, per tab the
// rail, the device bar and the page area; the Settings tab (0021, ADR-0172) shows the Settings page
// over the full width (no rail, no device bar). Presentation only (ADR-0003): all data and actions come from AppViewModel.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import HardwarioMonitor

ApplicationWindow {
    id: window


    objectName: "mainWindow"
    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    title: qsTr("HARDWARIO Monitor")
    color: Theme.bg
    font: Theme.fontUi
    Material.theme: Theme.dark ? Material.Dark : Material.Light
    Material.accent: Theme.accent
    Material.primary: Theme.accent
    Material.background: Theme.bg
    Material.foreground: Theme.text
    onClosing: close => close.accepted = AppViewModel.canQuit()

    Binding {
        target: AppViewModel
        property: "windowWidth"
        value: window.width
    }

    Connections {
        target: AppViewModel

        function onQuitConfirmationNeeded(text: string) {
            quitDialog.body = text;
            quitDialog.open();
        }

        function onCloseConfirmationNeeded(tab: DeviceTabViewModel, text: string) {
            closeDialog.tab = tab;
            closeDialog.body = text;
            closeDialog.open();
        }

        function onQuitRequested() {
            Qt.quit();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        DeviceTabBar {
            objectName: "deviceTabBar"
            Layout.fillWidth: true
        }

        // The shell banner area (plan 0015 B3, plan 0016 C2): one banner at a time, the crash
        // notice before the update notice.
        CrashNoticeBanner {
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            NavRail {
                objectName: "navRail"
                Layout.fillHeight: true
                visible: AppViewModel.globalPage === ""
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                StackLayout {
                    objectName: "tabStack"
                    anchors.fill: parent
                    currentIndex: AppViewModel.currentTab
                    visible: AppViewModel.globalPage === ""

                    Repeater {
                        model: AppViewModel.tabs

                        // 0021 (ADR-0172): the Settings row has no device view (an empty slot
                        // keeps the indexes of the StackLayout).
                        delegate: Loader {
                            id: slot

                            required property DeviceTabViewModel tab
                            required property int index
                            required property string tabKind

                            active: slot.tabKind === "device"
                            sourceComponent: TabView {
                                tab: slot.tab
                                index: slot.index
                            }
                        }
                    }
                }

                Loader {
                    id: globalPageLoader

                    objectName: "globalPage"
                    anchors.fill: parent
                    // 0021 review F8: alive while the Settings tab is open (scroll position and
                    // half-typed fields survive switching tabs), shown while it is current.
                    active: AppViewModel.tabs.settingsOpen
                    visible: AppViewModel.globalPage !== ""
                    source: AppViewModel.pages.qml("settings")

                    Binding {
                        target: globalPageLoader.item
                        property: "vm"
                        value: AppViewModel.globalPageViewModel("settings")
                        when: globalPageLoader.status === Loader.Ready
                    }
                }
            }
        }
    }

    Toast {
        objectName: "toasts"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingL + 2
        z: 20
    }

    ShortcutOverlay {}

    // M4 (plan/features/0007): the one wizard dialog and the diagnostic report dialog.
    WizardDialog {}

    ReportDialog {}

    Shortcuts {}

    ConfirmDialog {
        id: closeDialog

        // The tab, not its index: tabs may move while the dialog is open (review S6). Null when
        // the tab is gone meanwhile.
        property DeviceTabViewModel tab: null

        objectName: "closeTabDialog"
        title: qsTr("Close the tab?")
        acceptText: qsTr("Cancel and close")
        rejectText: qsTr("Keep running")
        onAccepted: AppViewModel.closeTabObject(closeDialog.tab, true)
    }

    ConfirmDialog {
        id: quitDialog

        objectName: "quitDialog"
        title: qsTr("Quit HARDWARIO Monitor?")
        acceptText: qsTr("Cancel and quit")
        rejectText: qsTr("Keep running")
        onAccepted: AppViewModel.cancelOperationsAndQuit()
    }
}
