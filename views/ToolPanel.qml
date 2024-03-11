import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// ToolPanel holds all the buttons and emits the signal of approriated clicked button.
Rectangle {
    id: _root
    color: Material.background
    property var consoleButtons: [detach, clearCli, pause, undo, batchCli, logFile]
    property var bluetoothButtons: [disconnect, clearBt, batchBt]
    property var consoleWelcomeButtons: [attach]
    property var bluetoothWelcomeButtons: [scan, connect]
    property var flashButtons: [browse, run, catalog, clearFlash]

    property var pageNameButtonMap: ({})

    Component.onCompleted: {
        pageNameButtonMap[AppSettings.consoleWelcomeName] = consoleWelcomeButtons
        pageNameButtonMap[AppSettings.bluetoothWelcomeName] = bluetoothWelcomeButtons
        pageNameButtonMap[AppSettings.consoleName] = consoleButtons
        pageNameButtonMap[AppSettings.bluetoothName] = bluetoothButtons
        pageNameButtonMap[AppSettings.flashName] = flashButtons
    }

    signal clearCliClicked
    signal clearBtClicked
    signal clearFlashClicked
    signal pauseClicked
    signal connectClicked
    signal scanClicked
    signal disconnectClicked
    signal downClicked
    signal undoClicked
    signal autoscrollClicked
    signal browseClicked
    signal runClicked
    signal sendCommand
    signal batchCliClicked
    signal batchBtClicked
    signal openLogFileClicked

    Rectangle {
        anchors.left: parent.left
        height: parent.height
        width: 1
        color: AppSettings.borderColor
    }

    function setUndoVisible(value) {
        undo.visible = value
    }

    function hideAll(arr) {
        arr.forEach(function (button) {
            button.visible = false
        })
    }

    function showAll(arr) {
        arr.forEach(function (button) {
            button.visible = button.visibleOnInit
        })
    }

    function highlightOnlyThis(button, arr) {
        arr.forEach(function (btn) {
            btn.borderHighlight = btn.textContent === button.textContent
        })
    }

    component ToolButton: SideButton {
        visible: true
        borderHighlight: false
        anchors.right: parent.right
        property bool visibleOnInit: true
        width: _root.width - 1
        height: _root.width
    }

    // tmp fix
    function togglePause(value) {
        if (value) {
            pause.textContent = "PAUSE"
            pause.iconSource = AppSettings.pauseIcon
            return
        }

        pause.textContent = "RESUME"
        pause.iconSource = AppSettings.resumeIcon
    }

    Column {
        id: buttons
        anchors.fill: parent

        ToolButton {
            id: scan
            textContent: "SCAN"
            borderHighlight: true
            iconSource: AppSettings.btDiscoverIcon
            onButtonClicked: {
                scanClicked()
                if (bluetooth.isOn) {
                    scan.borderHighlight = false
                }
            }
        }

        ToolButton {
            id: connect
            textContent: "CONNECT"
            iconSource: AppSettings.selectIcon
            visibleOnInit: true
            onButtonClicked: {
                connectClicked()
            }

            Connections {
                target: bluetooth

                function onDeviceConnected() {
                    connect.borderHighlight = false
                }

                function onDeviceDiscovered() {
                    highlightOnlyThis(connect, bluetoothWelcomeButtons)
                }
            }
        }

        ToolButton {
            id: disconnect
            textContent: "DISCONNECT"
            iconSource: AppSettings.btDisconnectIcon
            visible: false
            onButtonClicked: {
                disconnectClicked()
                highlightOnlyThis(scan, bluetoothWelcomeButtons)
            }
        }

        ToolButton {
            id: attach
            textContent: "ATTACH"
            iconSource: AppSettings.attachIcon
            borderHighlight: true
            onButtonClicked: {
                chester.attachRequested()
            }
        }

        ToolButton {
            id: detach
            textContent: "DETACH"
            iconSource: AppSettings.detachIcon
            onButtonClicked: {
                chester.detachRequested()
            }
        }

        ToolButton {
            id: pause
            iconHeight: 20
            iconWidth: 20
            textContent: "PAUSE"
            iconSource: AppSettings.pauseIcon

            onButtonClicked: {
                pauseClicked()
            }
        }

        ToolButton {
            id: undo
            iconSource: AppSettings.undoIcon
            textContent: "STOP"
            borderHighlight: true
            visibleOnInit: false
            onButtonClicked: {
                undoClicked()
            }
        }

        ToolButton {
            id: browse
            iconSource: AppSettings.folderIcon
            textContent: "BROWSE"
            onButtonClicked: {
                browseClicked()
            }
        }

        ToolButton {
            id: run
            iconHeight: 20
            iconWidth: 20
            iconSource: AppSettings.resumeIcon
            textContent: "FLASH"
            borderHighlight: flash.ready
            onButtonClicked: {
                runClicked()
                sendCommand()
            }
        }

        ToolButton {
            id: catalog
            iconSource: AppSettings.catalogIcon
            textContent: "CATALOG"
            borderHighlight: true
            onButtonClicked: {
                Qt.openUrlExternally(AppSettings.hardwarioCatalogAppWebUrl)
            }
        }

        ToolButton {
            id: clearCli
            textContent: "CLEAR"
            iconSource: AppSettings.clearIcon
            onButtonClicked: {
                clearCliClicked()
            }
        }

        ToolButton {
            id: clearBt
            textContent: "CLEAR"
            iconSource: AppSettings.clearIcon
            onButtonClicked: {
                clearBtClicked()
            }
        }

        ToolButton {
            id: clearFlash
            textContent: "CLEAR"
            iconSource: AppSettings.clearIcon
            onButtonClicked: {
                clearFlashClicked()
            }
        }

        ToolButton {
            id: batchBt
            textContent: "BATCH"
            iconSource: AppSettings.batchIcon
            onButtonClicked: {
                batchBtClicked()
            }
        }

        ToolButton {
            id: batchCli
            textContent: "BATCH"
            iconSource: AppSettings.batchIcon
            onButtonClicked: {
                batchCliClicked()
            }
        }

        ToolButton {
            id: logFile
            textContent: "LOG FILE"
            iconSource: AppSettings.openIcon
            onButtonClicked: {
                openLogFileClicked()
            }
        }

        Connections {
            target: flash

            function onReadyChanged() {
                if (flash.ready) {
                    highlightOnlyThis(run, flashButtons)
                }
            }

            function onFinished() {
                highlightOnlyThis(browse, flashButtons)
            }
        }

        Connections {
            target: stackView
            function onCurrentItemChanged() {
                const currentPageName = stackView.currentItem.name

                for (const pageName in _root.pageNameButtonMap) {
                    const buttons = pageNameButtonMap[pageName]
                    currentPageName === pageName ? _root.showAll(
                                                       buttons) : _root.hideAll(
                                                       buttons)
                }
            }
        }
    }
}
