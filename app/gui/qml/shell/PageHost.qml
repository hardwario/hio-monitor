// The page area of a tab (page-extension contract, ADR-0059): the connection ErrorPanel and the
// warning banner on top, then the Choose device page or the current page. Each page root is
// loaded once on first use and gets the properties `tab` (DeviceTabViewModel) and `vm` (its view
// model).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import HardwarioMonitor

Item {
    id: host

    required property DeviceTabViewModel tab
    readonly property bool choosing: host.tab.state === DeviceTabViewModel.ChoosingDevice

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ErrorPanel {
            objectName: "connectionError"
            Layout.fillWidth: true
            Layout.margins: Theme.spacingM
            visible: !host.choosing && host.tab.connection.error.valid
            error: host.tab.connection.error
            compact: Window.window !== null && Window.window.height < 700
            secondaryActionText: host.tab.connection.canChooseAnotherProbe ? qsTr("Choose another probe") : ""
            onSecondaryActionTriggered: host.tab.connection.chooseAnotherProbe()
            onActionTriggered: action => action === ErrorAction.OpenSettings ? AppViewModel.openSettings(host.tab.connection.error.settingsSection)
                                                                               : host.tab.connection.triggerErrorAction(action)
        }

        WarningBanner {
            objectName: "connectionWarning"
            Layout.fillWidth: true
            visible: !host.choosing && host.tab.connection.warning.valid
            text: host.tab.connection.warning.message
            details: host.tab.connection.warning
            onDismissed: host.tab.connection.dismissWarning()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ChooseDevicePage {
                anchors.fill: parent
                visible: host.choosing
                picker: host.tab.picker
                active: host.choosing && host.tab === AppViewModel.currentTabViewModel
                        && AppViewModel.globalPage === ""
            }

            Repeater {
                model: AppViewModel.pages

                delegate: Loader {
                    id: pageLoader

                    required property string pageId
                    required property bool isGlobal
                    required property url qmlUrl
                    readonly property bool current: !host.choosing && !pageLoader.isGlobal
                                                    && host.tab.currentPage === pageLoader.pageId

                    objectName: "page_" + pageLoader.pageId
                    anchors.fill: parent
                    // Loaded on first use and kept (the page keeps its state while hidden).
                    active: pageLoader.current || pageLoader.status === Loader.Ready
                    visible: pageLoader.current
                    source: pageLoader.isGlobal ? "" : pageLoader.qmlUrl

                    Binding {
                        target: pageLoader.item
                        property: "tab"
                        value: host.tab
                        when: pageLoader.status === Loader.Ready
                    }

                    Binding {
                        target: pageLoader.item
                        property: "vm"
                        value: host.tab.pageViewModel(pageLoader.pageId)
                        when: pageLoader.status === Loader.Ready
                    }
                }
            }
        }
    }
}
