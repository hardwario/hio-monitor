import QtQuick
import QtQuick.Controls.Material

// SideButton is a custom button component with image and label.
Rectangle {
    id: _root
    property string iconSource
    property string textContent
    property bool checked: false
    property bool borderHighlight: false
    property bool visibleOnInit: true
    property int iconWidth: 24
    property int iconHeight: 24
    property var customMouseArea: null
    signal buttonClicked

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
        // Determine which mouse area is active, defaulting to mouseArea if customMouseArea is not defined
        var activeMouseArea = _root.customMouseArea || mouseArea

        // Check for press state first, as it should take precedence over hover state
        if (activeMouseArea.pressed) {
            return AppSettings.clickIndicatorColor
        } else if (activeMouseArea.containsMouse) {
            return AppSettings.hoverColor
        } else {
            return Material.background
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 5

        Image {
            id: icon
            source: _root.iconSource
            smooth: true
            fillMode: Image.PreserveAspectFit
            width: _root.iconWidth
            height: _root.iconHeight
            verticalAlignment: Image.AlignVCenter
        }

        Text {
            id: textItem
            visible: _root.textContent !== ""
            text: _root.textContent
            font.family: labelFont.name
            font.pixelSize: 12
            property color textColor: AppSettings.grayColor
            color: textColor
            width: parent.width
            horizontalAlignment: Text.AlignHCenter

            // text color transition animation on click detected in MouseArea
            Behavior on textColor {
                ColorAnimation {
                    duration: 200
                    easing.type: Easing.InOutQuad
                }
            }
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
            PropertyAnimation {
                duration: 350
                easing.type: Easing.InOutQuad
            }
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

            // button text color change on click
            textItem.textColor = AppSettings.clickIndicatorColor
            Qt.callLater(function () {
                textItem.textColor = AppSettings.grayColor
            })
        }
    }

    Behavior on color {
        ColorAnimation {
            duration: 200
            easing.type: Easing.InOutQuad
        }
    }
}
