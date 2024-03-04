import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

import hiomon 1.0

// PagePanel is a navigator/router controller of the app.
// It holds all possible pages and utilizes the StackView to switch them.
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

        buttons.forEach(function (button) {
            button.checked = false
        })

        button.checked = true
    }

    function setCurrentPage(page) {
        if (!stackView)
            return

        const cur = stackView.currentItem
        if (cur === page)
            return

        stackView.replace(cur, page, StackView.Immediate)
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

    component PageButton: SideButton {
        width: _root.width - 1
        height: _root.width
        checked: false
        property bool showWelcomePage: false
    }

    Column {
        id: buttonColumn
        anchors.fill: parent

        PageButton {
            id: consoleButton
            textContent: "CONSOLE"
            iconSource: AppSettings.cliIcon
            checked: true
            customColor: checked ? "#121219" : ""

            onButtonClicked: {
                _root.setCheckedButton(consoleButton)
                loadingIndicator.close()

                const page = showWelcomePage ? consoleWelcomePage : consolePage
                _root.setCurrentPage(page)
            }

            Connections {
                target: chester

                function onAttachSucceeded() {
                    consoleButton.showWelcomePage = false
                    _root.setCurrentPage(consolePage)
                }

                function onDetachSucceeded() {
                    consoleButton.showWelcomePage = true
                    _root.setCurrentPage(consoleWelcomePage)
                }
            }
        }

        PageButton {
            id: bluetoothButton
            textContent: "BLUETOOTH"
            iconSource: AppSettings.btIcon
            customColor: checked ? "#121219" : ""

            onButtonClicked: {
                _root.setCheckedButton(bluetoothButton)

                const page = showWelcomePage ? bluetoothWelcomePage : bluetoothPage

                _root.setCurrentPage(page)

                if (loadingIndicator.opened)
                    loadingIndicator.open()
            }

            Connections {
                target: bluetooth

                function onDeviceConnected() {
                    bluetoothButton.showWelcomePage = false
                    _root.setCheckedButton(bluetoothButton)
                    _root.setCurrentPage(bluetoothPage)
                }

                function onDeviceDisconnected() {
                    bluetoothButton.showWelcomePage = true
                    _root.setCheckedButton(bluetoothButton)
                    _root.setCurrentPage(bluetoothWelcomePage)
                }
            }
        }

        PageButton {
            id: flashButton
            textContent: "FLASH"
            iconSource: AppSettings.flashIcon
            customColor: checked ? "#121219" : ""

            onButtonClicked: {
                _root.setCheckedButton(flashButton)
                _root.setCurrentPage(flashPage)
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
