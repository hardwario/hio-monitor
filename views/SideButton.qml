import QtQuick 2.15
import QtQuick.Controls.Material 2.15

Rectangle {
    id: _root
    property string iconSource
    property string textContent
    property bool checked: false
    property bool borderHighlight: false
    property bool visibleOnInit: true
    property int iconWidth: 24
    property int iconHeight: 24
    signal buttonClicked()

    border.color: _root.borderHighlight ? Material.accent : "transparent"
    border.width: 2

    onCheckedChanged: {
        leftBorder.height = checked ? _root.height : 0
    }

    Component.onCompleted: {
        leftBorder.height = checked ? _root.height : 0
    }

    color: color()

    function color() {
        let res = Material.background
        if (mouseArea.containsMouse)
            res = AppSettings.hoverColor
        if (mouseArea.pressed)
            res = AppSettings.grayColor
        return res
    }

    Column {
        anchors.centerIn: parent
        spacing: 5
        Image {
            id: icon
            source: iconSource
            smooth: true
            fillMode: Image.PreserveAspectFit
            width: _root.iconWidth
            height: _root.iconHeight
            verticalAlignment: Image.AlignVCenter
        }

        Text {
            id: textItem
            visible: textContent !== ""
            text: textContent
            font.family: labelFont.name
            font.pixelSize: 12
            color: AppSettings.grayColor
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Rectangle {
        id: leftBorder
        visible: _root.checked
        width: 3
        height: 0
        anchors.bottom: _root.bottom
        color: Material.accent
        Behavior on height {
            PropertyAnimation { duration: 350; easing.type: Easing.InOutQuad }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            leftBorder.height = _root.height
            _root.buttonClicked()
        }
    }

    Behavior on color {
        ColorAnimation { duration: 300; easing.type: Easing.InOutQuad }
    }
}
