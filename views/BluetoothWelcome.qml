import QtQuick
import QtQuick.Controls.Material

// BluetoothWelcome is the welcome page for the Bluetooth page.
// It has a discovery button to start scanning for the nearby Chester devices.
// It shows all the discovered devices in a list view and then the user can connect to the device.
// It has a guidaince message for the user.
Item {
    id: _root
    property string name: AppSettings.bluetoothWelcomeName

    function isBtOn() {
        const isOn = bluetooth.isOn

        if (!isOn)
            notify.showError("Bluetooth is trurned off")

        return isOn
    }

    Connections {
        target: toolPanel

        function onScanClicked() {
            if (!_root.isBtOn())
                return

            notify.showInfo("Scanning...")
            progress.visible = true
            bluetooth.startScan()
            deviceView.visible = true
            devicesFocusScope.forceActiveFocus()
        }

        function onConnectClicked() {
            if (!_root.isBtOn())
                return

            if (deviceView.model.rowCount() === 0) {
                notify.showError("No devices were found")
                return
            }

            notify.showInfo("Connecting...")
            loadingIndicator.open()
            bluetooth.connectToByIndex(deviceView.currentIndex)
        }
    }

    // Welcome message and icon
    Item {
        id: welcome
        anchors {
            top: parent.top
            left: parent.left
            right: devices.left
        }

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

        Text {
            id: welcomeMessage
            text: qsTr("Welcome to the Bluetooth page.")
            anchors {
                top: img.bottom
                topMargin: 40
            }
            color: AppSettings.whiteColor
            font.pixelSize: 24
            width: parent.width
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            id: guideMessage
            text: qsTr("To start using it, click the SCAN button to discover the nearby devices then choose your device and click CONNECT button.")
            anchors {
                top: welcomeMessage.bottom
                topMargin: 24
            }
            color: AppSettings.whiteColor
            font.pixelSize: 20
            width: parent.width
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            id: pairingWarningMessage
            text: qsTr("If the pairing window does not appear when you connect, try pairing the CHESTER device using the bluetooth system tool.")
            anchors {
                top: guideMessage.bottom
                topMargin: 48
            }
            color: AppSettings.wrnColor
            font.pixelSize: 16
            width: parent.width
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Connections {
        target: bluetooth

        function onBluetoothChanged() {
            if (!bluetooth.isOn)
                notify.showError("Bluetooth is turned off")
        }

        function onErrorOnConnect(msg) {
            notify.showError(msg)
            loadingIndicator.close()
        }

        function onDeviceConnected() {
            loadingIndicator.close()
            notify.showWrn("Connected to: " + deviceView.currentItem.model.name)
        }

        function onDeviceDisconnected() {
            loadingIndicator.close()
            notify.showWrn(
                        "Disconnected from: " + deviceView.currentItem.model.name)
        }

        function onDeviceIsUnpaired() {
            notify.showWrn("Device " + deviceView.currentItem.model.name
                           + " is unpaired, please pair the device")
            loadingIndicator.close()
        }

        function onDeviceDiscovered(device) {
            deviceModel.addDevice(device)
        }

        function onDeviceScanFinished() {
            progress.visible = false
            notify.showInfo("Device scanning finished")
        }

        function onDeviceScanCanceled() {
            progress.visible = false
            notify.showWrn("Device scanning canceled")
        }
    }

    Rectangle {
        id: devices
        anchors.right: parent.right
        color: Material.background
        height: parent.height
        width: appWindow.width / 3

        Component.onCompleted: {
            deviceView.forceActiveFocus()
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
            bindFocusTo: devicesFocusScope.activeFocus || parent.focus
            text: "DEVICES"
        }

        FocusScope {
            id: devicesFocusScope
            width: parent.width
            height: parent.height

            anchors {
                right: parent.right
                left: parent.left
                leftMargin: leftBorder.width
                top: placeholderText.bottom
            }

            Keys.onPressed: function (event) {
                switch (event) {
                case Qt.Key_Up:
                    deviceView.decrementCurrentIndex()
                    event.accepted = true
                    break
                case Qt.Key_Down:
                    deviceView.incrementCurrentIndex()
                    event.accepted = true
                    break
                }
            }

            ListView {
                id: deviceView
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                height: parent.height
                model: sortDeviceModel
                visible: true
                width: parent.width

                onCountChanged: {
                    currentIndex = 0
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
