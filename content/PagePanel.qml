import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import hiomon 1.0

Rectangle {
    id: _root
    color: Material.background
    property var buttons: []

    Component.onCompleted: {
        buttons = [consoleButton, bluetoothButton, flashButton]
        stackView.push(flashPage, StackView.Immediate)
        stackView.push(bluetoothWelcomePage, StackView.Immediate)
        stackView.push(consoleWelcomePage, StackView.Immediate)
    }

    function setCheckedButton(button) {
        if (button.checked)
            return
        buttons.forEach((button) => {
            button.checked = false
        })
        button.checked = true
    }
    // right line
    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: AppSettings.borderColor
    }

    // Pages
    ConsolePage {
        id: consolePage
        visible: false
    }

    BluetoothPage {
        id: bluetoothPage
        visible: false
    }

    FlashPage {
        id: flashPage
        visible: false
    }

    ConsoleWelcome {
        id: consoleWelcomePage
        visible: true
    }

    BluetoothWelcome {
        id: bluetoothWelcomePage
        visible: true
    }

    function setCurrentPage(page) {
        if (!stackView)
            return
        var cur = stackView.currentItem
        if (cur === page)
            return
        stackView.replace(cur, page, StackView.Immediate)
    }

    component PageButton: SideButton {
        width: _root.width - 1
        height: _root.width
        checked: false
        property bool showWelcomePage: true
    }

    Column {
        id: buttonColumn
        anchors.fill: parent

        PageButton {
            id: consoleButton
            textContent: "Console"
            iconSource: AppSettings.cliIcon
            checked: true
            showWelcomePage: true
            onButtonClicked: {
                setCheckedButton(consoleButton)
                loadingIndicator.close()
                showWelcomePage ? setCurrentPage(consoleWelcomePage) : setCurrentPage(consolePage)
            }
            Connections {
                target: chester
                onAttachSucceeded: {
                    consoleButton.showWelcomePage = false
                    setCurrentPage(consolePage)
                }
                onDetachSucceeded: {
                    consoleButton.showWelcomePage = true
                    setCurrentPage(consoleWelcomePage)
                }
            }
        }

        PageButton {
            id: bluetoothButton
            textContent: "Bluetooth"
            iconSource: AppSettings.btIcon
            onButtonClicked: {
                setCheckedButton(bluetoothButton)
                showWelcomePage ? setCurrentPage(bluetoothWelcomePage) : setCurrentPage(bluetoothPage)
                if (loadingIndicator.opened)
                    loadingIndicator.open()
            }
            Connections {
                target: bluetooth
                onDeviceConnected: {
                    bluetoothButton.showWelcomePage = false
                    setCheckedButton(bluetoothButton)
                    setCurrentPage(bluetoothPage)
                }
                onDeviceDisconnected: {
                    bluetoothButton.showWelcomePage = true
                    setCheckedButton(bluetoothButton)
                    setCurrentPage(bluetoothWelcomePage)
                }
            }
        }

        PageButton {
            id: flashButton
            textContent: "Flash"
            iconSource: AppSettings.flashIcon
            onButtonClicked: {
                setCheckedButton(flashButton)
                setCurrentPage(flashPage)
            }
        }
    }

    SideButton {
        id: hardwarioRedirect
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
        }
        width: _root.width - 1
        height: _root.width
        iconHeight: height - 10
        iconWidth: width - 10
        iconSource: AppSettings.hwTitleIcon
        onButtonClicked: {
            Qt.openUrlExternally(AppSettings.hardwarioWebUrl)
        }
    }
}
