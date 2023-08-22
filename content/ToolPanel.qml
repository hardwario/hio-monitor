import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15

Rectangle {
    id: _root
    color: Material.background
    property var consoleButtons: [ detach, clearCli, pause, undo ]
    property var bluetoothButtons: [ disconnect, clearBt ]
    property var consoleWelcomeButtons: [ attach ]
    property var bluetoothWelcomeButtons: [ scan, connect ]
    property var flashButtons: [ browse, run, catalog, clearFlash ]

    property var pageNameButtonMap: ({ });

    Component.onCompleted: {
        pageNameButtonMap[AppSettings.consoleWelcomeName] = consoleWelcomeButtons
        pageNameButtonMap[AppSettings.bluetoothWelcomeName] = bluetoothWelcomeButtons
        pageNameButtonMap[AppSettings.consoleName] = consoleButtons
        pageNameButtonMap[AppSettings.bluetoothName] = bluetoothButtons
        pageNameButtonMap[AppSettings.flashName] = flashButtons
    }

    signal clearClicked()
    signal pauseClicked()
    signal connectClicked()
    signal scanClicked()
    signal disconnectClicked()
    signal downClicked()
    signal undoClicked()
    signal autoscrollClicked()
    signal browseFilesClicked()
    signal runClicked()
    signal sendCommand()

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
        arr.forEach((button) => {
            button.visible = false
        })
    }

    function showAll(arr) {
        arr.forEach((button) => {
            button.visible = button.visibleOnInit
        })
    }

    function highlightOnlyThis(button, arr) {
        arr.forEach((btn) => {
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

    Column {
        anchors.fill: parent

        ToolButton {
            id: scan
            textContent: "Scan"
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
            textContent: "Connect"
            iconSource: AppSettings.selectIcon
            visibleOnInit: true
            onButtonClicked: {
                connectClicked()
                console.log("Connect clicked")
            }
            Connections {
                target: bluetooth
                onDeviceConnected: {
                    console.log("Device connected")
                    connect.borderHighlight = false
                }
                onDeviceDiscovered: {
                    highlightOnlyThis(connect, bluetoothWelcomeButtons)
                }
            }
        }

        ToolButton {
            id: disconnect
            textContent: "Disconnect"
            iconSource: AppSettings.btDisconnectIcon
            visible: false
            onButtonClicked: {
                console.log("Disconnect clicked")
                disconnectClicked()
                highlightOnlyThis(scan, bluetoothWelcomeButtons)
            }
        }

        ToolButton {
            id: attach
            textContent: "Attach"
            iconSource: AppSettings.attachIcon
            borderHighlight: true
            onButtonClicked: {
                chester.attachRequested()
            }
        }

        ToolButton {
            id: detach
            textContent: "Detach"
            iconSource: AppSettings.detachIcon
            onButtonClicked: {
                chester.detachRequested()
            }
        }

        ToolButton {
            id: pause
            iconHeight: 20
            iconWidth: 20
            textContent: "Pause"
            iconSource: AppSettings.pauseIcon
            onButtonClicked: {
                pauseClicked()
                if (pause.iconSource === AppSettings.pauseIcon) {
                    pause.iconSource = AppSettings.resumeIcon
                    pause.textContent = "Resume"
                } else {
                    pause.iconSource = AppSettings.pauseIcon
                    pause.textContent = "Pause"
                }
            }
        }

        ToolButton {
            id: undo
            iconSource: AppSettings.undoIcon
            textContent: "Undo"
            visibleOnInit: false
            onButtonClicked: {
                undoClicked()
            }
        }

        ToolButton {
            id: browse
            iconSource: AppSettings.folderIcon
            textContent: "Browse"
            borderHighlight: true
            onButtonClicked: {
                browseFilesClicked()
            }
        }

        ToolButton {
            id: run
            iconHeight: 20
            iconWidth: 20
            iconSource: AppSettings.resumeIcon
            textContent: "Run"
            borderHighlight: flash.ready
            onButtonClicked: {
                runClicked()
                sendCommand()
            }
        }

        ToolButton {
            id: catalog
            iconSource: AppSettings.catalogIcon
            textContent: "Catalog"
            onButtonClicked: {
                Qt.openUrlExternally(AppSettings.hardwarioCatalogAppWebUrl)
            }
        }

        ToolButton {
            id: clearCli
            textContent: "Clear"
            iconSource: AppSettings.clearIcon
            onButtonClicked: {
                clearClicked()
            }
        }

        ToolButton {
            id: clearBt
            textContent: "Clear"
            iconSource: AppSettings.clearIcon
            onButtonClicked: {
                clearClicked()
            }
        }

        ToolButton {
            id: clearFlash
            textContent: "Clear"
            iconSource: AppSettings.clearIcon
            onButtonClicked: {
                clearClicked()
            }
        }

        Connections {
            target: flash
            onReadyChanged: {
                if (flash.ready) {
                    highlightOnlyThis(run, flashButtons)
                }
            }
            onFinished: {
                highlightOnlyThis(browse, flashButtons)
            }
        }

        Connections {
            target: stackView
            onCurrentItemChanged: {
                var currentPageName = stackView.currentItem.name
                for (var pageName in _root.pageNameButtonMap) {
                    var buttons = pageNameButtonMap[pageName]
                    currentPageName === pageName ? _root.showAll(buttons) : _root.hideAll(buttons)
                }
            }
        }
    }
}
