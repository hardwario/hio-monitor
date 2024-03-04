import QtQuick

// TextLabel is a custom text label which is mainly used in InteractiveShell and DeviceLog.
Text {
    property string textValue: ""
    property bool bindFocusTo: false
    property int hborderWidth: 0
    property string customColor: ""

    text: textValue
    font.bold: true
    font.family: labelFont.name
    color: {
        if (customColor !== "") {
            return customColor
        }

        return bindFocusTo ? AppSettings.focusColor : AppSettings.whiteColor
    }

    width: parent.width
    height: 25
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter

    anchors {
        horizontalCenter: parent.horizontalCenter
        top: parent.top
        topMargin: 3
        bottomMargin: 3
    }

    // bottom line
    Rectangle {
        anchors {
            top: parent.bottom
            horizontalCenter: parent.horizontalCenter
        }
        width: parent.hborderWidth
        height: 1
        color: AppSettings.borderColor
    }
}
