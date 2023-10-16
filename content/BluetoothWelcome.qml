import QtQuick 2.12
import QtQuick.Controls.Material 2.15

Item {
    id: _root
    property string name: AppSettings.bluetoothWelcomeName

    // Welcome message and icon
    Item {
        id: welcome
        anchors {
            top: parent.top
            left: parent.left
            right: devicesView.left
        }

        // Icon to be displayed at top
        Image {
            id: img
            source: AppSettings.btIcon
            width: 200
            height: 200
            anchors {
                top: parent.top
                topMargin: 75
                horizontalCenter: parent.horizontalCenter
            }
        }

        // Welcome message text
        Text {
            id: welcomeMessage
            text: qsTr("Welcome to the Bluetooth page!\n\nTo start using it, click the Scan button to discover the nearby devices then choose your device and click Connect button.")
            anchors {
                top: img.bottom
                topMargin: 35
            }
            color: AppSettings.grayColor
            font.pixelSize: 22
            width: parent.width
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            id: pairingWarningMessage
            text: qsTr("If the pairing window does not appear when you connect, try pairing the CHESTER device using the bluetooth system tool!")
            anchors {
                top: welcomeMessage.bottom
                topMargin: 55
            }
            color: AppSettings.wrnColor
            font.pixelSize: 16
            width: parent.width
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    function checkBt() {
        const isOn = bluetooth.isOn
        if (!isOn) notify.showError("Bluetooth is trurned off")
        return isOn
    }

    Connections {
        target: bluetooth
        onBluetoothChanged: {
            if (!bluetooth.isOn)
                notify.showError("Bluetooth is turned off")
        }
        onErrorOnConnect: (msg) => {
            notify.showError(msg)
            loadingIndicator.close()
        }
        onDeviceConnected: {
            loadingIndicator.close()
            notify.showWrn("Connected to: " + devices.currentItem.model.name)
        }
        onDeviceDisconnected: {
            loadingIndicator.close()
            notify.showWrn("Disconnected from: " + devices.currentItem.model.name)
        }
        onDeviceIsUnpaired: {
            notify.showWrn("Device " + devices.currentItem.model.name + " is unpaired, please pair the device")
            loadingIndicator.close()
        }
        onDeviceDiscovered: (device) => {
            deviceModel.addDevice(device)
        }
        onDeviceScanFinished: {
            progress.visible = false
            notify.showInfo("Device scanning finished")
        }
        onDeviceScanCanceled: {
            progress.visible = false
            notify.showWrn("Device scanning canceled")
        }
    }

    Rectangle {
        id: devicesView
        anchors.right: parent.right
        color: Material.background
        height: parent.height
        width: appWindow.width / 4

        Component.onCompleted: {
            devices.forceActiveFocus()
        }

        Rectangle {
            id: leftBorder
            anchors.left: parent.left
            color: AppSettings.borderColor
            height: parent.height
            width: 1
        }

        TextLabel {
            id: placeholderText
            bindFocusTo: devicesFocusScope.activeFocus || devicesView.focus
            text: "Devices"
        }

        FocusScope {
            id: devicesFocusScope
            width: devicesView.width
            height: devicesView.height

            anchors {
                right: parent.right
                left: parent.left
                leftMargin: leftBorder.width
                top: placeholderText.bottom
            }

            Keys.onPressed: (event) => {
                switch(event) {
                    case Qt.Key_Up:
                        devices.decrementCurrentIndex()
                        event.accepted = true
                        break
                    case Qt.Key_Down:
                        devices.incrementCurrentIndex()
                        event.accepted = true
                        break
                }
            }

            ListView {
                id: devices
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                height: parent.height
                model: sortDeviceModel
                visible: true
                width: parent.width

                onCountChanged: {
                    currentIndex = 0
                }

                Connections {
                    target: toolPanel
                    onScanClicked: {
                        if (!checkBt()) return
                        notify.showInfo("Scanning...")
                        progress.visible = true
                        bluetooth.startScan()
                        devices.visible = true
                        devicesFocusScope.forceActiveFocus()
                    }
                    onConnectClicked: {
                        if (!checkBt()) return
                        if (devices.model.rowCount() === 0) {
                            notify.showError("No devices were found")
                            return
                        }
                        notify.showInfo("Connecting...")
                        loadingIndicator.open()
                        bluetooth.connectToByIndex(devices.currentIndex)
                    }
                }

                delegate: BtDeviceInfo {}
            }
        }
    }

    ProgressBar {
        id: progress
        from: 0.0
        visible: false
        indeterminate: true
        width: _root.width
    }
}
