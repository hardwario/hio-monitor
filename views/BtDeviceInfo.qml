import QtQuick
import QtQuick.Controls.Material

// BtDeviceInfo is a bluetooth device list item object.
// It's meant to be used as the ListView delegate, that's why below it accesses model object.
Item {
    id: deviceItem

    height: deviceView.height * 0.15
    width: deviceView.width

    required property int index
    required property var model

    function colorByState() {
        let res = Material.background

        if (mouseArea.containsMouse)
            res = AppSettings.hoverColor

        if (deviceView.currentIndex === index)
            res = Qt.lighter(AppSettings.borderColor)

        return res
    }

    function chooseColor(rssi) {
        let res = Qt.darker(AppSettings.whiteColor)

        if (rssi <= -90)
            res = "#F97583"
        else if (rssi <= -67)
            res = "#FFAB70"
        else if (rssi <= -55)
            res = "#85E89D"
        else if (rssi <= -30)
            res = "#85E89D"

        return res
    }

    function chooseIcon(rssi) {
        if (rssi <= -90)
            return AppSettings.weakSignalIcon
        else if (rssi <= -67)
            return AppSettings.okaySignalIcon
        else if (rssi <= -55)
            return AppSettings.goodSignalIcon
        else if (rssi <= -30)
            return AppSettings.bestSignalIcon

        return AppSettings.signalIcon
    }

    MouseArea {
        id: mouseArea
        anchors.fill: deviceItem
        hoverEnabled: true
        onClicked: {
            deviceView.currentIndex = index
        }
    }

    Rectangle {
        id: box
        width: parent.width
        height: parent.height
        color: colorByState()

        Image {
            id: hwIcon
            source: AppSettings.hwNoTitleIcon
            smooth: true
            height: 2 * device.height
            width: height - 6
            anchors {
                right: infoColumn.left
                rightMargin: 30
                verticalCenter: parent.verticalCenter
            }
        }

        Column {
            id: infoColumn
            anchors.centerIn: parent
            width: parent.width / 3

            Text {
                id: device
                text: deviceItem.model.name
                color: AppSettings.whiteColor
                font.family: textFont.name
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                id: deviceAddress
                text: deviceItem.model.address
                visible: text !== ""
                color: Qt.darker(AppSettings.whiteColor, 1.3)
                font.family: textFont.name
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Text {
            id: deviceRssi
            text: deviceItem.model.rssi
            visible: deviceItem.model.rssi !== 0
            color: chooseColor(deviceItem.model.rssi)
            font.family: textFont.name
            anchors {
                left: infoColumn.right
                leftMargin: 30
                bottom: signalImage.bottom
            }
            verticalAlignment: Text.AlignBottom
        }

        Image {
            id: signalImage
            visible: deviceItem.model.rssi !== 0
            source: chooseIcon(deviceItem.model.rssi)
            width: 24
            height: 24
            anchors {
                left: deviceRssi.right
                leftMargin: 10
                verticalCenter: parent.verticalCenter
            }
        }

        Rectangle {
            id: lineBottom
            color: "#1E000000"
            height: 1
            width: box.width
            anchors {
                bottom: parent.bottom
                bottomMargin: 5
            }
        }
    }
}
