import QtQuick
import QtQuick.Controls

// AppNotification is a notification manager for the app.
// It meant to be instatiated in the main window to be accessible globally.
Item {
    function showInfo(msg) {
        popup.message = msg
        popup.msgColor = AppSettings.greenColor
        popup.open()
    }

    function showError(msg) {
        popup.message = msg
        popup.msgColor = AppSettings.redColor
        popup.open()
    }

    function showWrn(msg) {
        popup.message = msg
        popup.msgColor = AppSettings.wrnColor
        popup.open()
    }

    x: parent.width
    y: parent.height
    z: 100

    Popup {
        id: popup
        width: (appWindow.width / 4) + message.length
        height: 45
        modal: false
        focus: false

        x: parent.width - width
        y: parent.height - height
        z: parent.z

        property string message: ""
        property string msgColor: AppSettings.whiteColor

        background: Rectangle {
            width: parent.width
            color: AppSettings.darkColor
            border.color: AppSettings.borderColor
            z: parent.z

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: popup.message
                color: popup.msgColor
                width: parent.width
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Timer {
            id: timer
            interval: 3500
            running: false
            repeat: false
            onTriggered: popup.close()
        }

        onVisibleChanged: {
            timer.running = visible
        }
    }
}
