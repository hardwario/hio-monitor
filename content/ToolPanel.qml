import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15

Rectangle {
    id: _root
    color: Material.background
    property var consoleButtons: [ detach, clear, pause, undo ]
    property var bluetoothButtons: [ disconnect, clear ]
    property var consoleWelcomeButtons: [ attach ]
    property var bluetoothWelcomeButtons: [ scan, connect ]
    property var flashButtons: [ browse, flash ]

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
    signal flashClicked()
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
                    connect.borderHighlight = true
                    connect.visibleOnInit = true
                    connect.visible = true
                }
            }
        }

        ToolButton {
            id: connect
            textContent: "Connect"
            iconSource: AppSettings.selectIcon
            visibleOnInit: false
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
            }

            Connections {
                target: bluetooth
                onDeviceDisconnected: {
                    console.log("Device disconnected")
                    scan.borderHighlight = true
                }
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
            id: clear
            textContent: "Clear"
            iconSource: AppSettings.clearIcon
            onButtonClicked: {
                clearClicked()
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
                borderHighlight = false
                flash.borderHighlight = true
                browseFilesClicked()
            }
        }

        ToolButton {
            id: flash
            iconSource: AppSettings.resumeIcon
            textContent: "Run"
            onButtonClicked: {
                borderHighlight = false
                flashClicked()
                sendCommand()
            }
        }

        Connections {
            target: stackView
            onCurrentItemChanged: {
                var currentPageName = stackView.currentItem.name

                for (var pageName in _root.pageNameButtonMap) {
                    var buttons = pageNameButtonMap[pageName]
                    _root.hideAll(buttons)
                    if (currentPageName === pageName) {
                        _root.showAll(buttons)
                    }
                }
            }
        }
    }
}
