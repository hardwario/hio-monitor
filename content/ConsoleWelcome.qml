import QtQuick 2.12
import QtQuick.Controls.Material 2.15

Item {
    property string name: AppSettings.consoleWelcomeName
    Image {
        id: img
        source: AppSettings.cliIcon
        width: 250
        height: 200
        anchors {
            top: parent.top
            topMargin: 75
            horizontalCenter: parent.horizontalCenter
        }
    }
    Text {
        id: welcomeMessage
        text: qsTr("Welcome to the Console page!\n\nTo start using it, click the Attach button to connect to your device.")
        color: AppSettings.grayColor
        font.pixelSize: 22
        wrapMode: Text.WordWrap
        width: parent.width / 2.5
        horizontalAlignment: Text.AlignHCenter
        anchors {
            top: img.bottom
            topMargin: 35
            horizontalCenter: parent.horizontalCenter
        }
    }

    Text {
        text: qsTr("Ensure that the device is connected via USB!")
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
