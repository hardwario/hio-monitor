// Main window of HARDWARIO Monitor (ADR-0043, ADR-0061): device tab strip on top, per tab the
// rail, the device bar and the page area; global pages (Settings) replace the device bar and page
// area. Presentation only (ADR-0003): all data and actions come from AppViewModel.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import HardwarioMonitor

ApplicationWindow {
    id: window

    readonly property var globalPageInfo: AppViewModel.pages.find(page => page.id === AppViewModel.globalPage)

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

        function onCloseConfirmationNeeded(index: int, text: string) {
            closeDialog.tabIndex = index;
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

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            NavRail {
                objectName: "navRail"
                Layout.fillHeight: true
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

                        delegate: TabView {}
                    }
                }

                Loader {
                    id: globalPageLoader

                    objectName: "globalPage"
                    anchors.fill: parent
                    active: window.globalPageInfo !== undefined
                    source: window.globalPageInfo !== undefined ? window.globalPageInfo.qml : ""

                    Binding {
                        target: globalPageLoader.item
                        property: "vm"
                        value: AppViewModel.globalPageViewModel(AppViewModel.globalPage)
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

    Shortcuts {}

    ConfirmDialog {
        id: closeDialog

        property int tabIndex: -1

        objectName: "closeTabDialog"
        title: qsTr("Close the tab?")
        acceptText: qsTr("Cancel and close")
        rejectText: qsTr("Keep running")
        onAccepted: AppViewModel.closeTab(closeDialog.tabIndex, true)
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
