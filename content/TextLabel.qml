import QtQuick 2.15

Text {
    property string textValue: ""
    property bool bindFocusTo: false
    text: textValue
    font.bold: true
    font.family: labelFont.name
    color: bindFocusTo ? AppSettings.focusColor : AppSettings.whiteColor
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
        width: _root.hborderWidth
        height: 1
        color: AppSettings.borderColor
    }
}
