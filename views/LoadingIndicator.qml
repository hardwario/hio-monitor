import QtQuick
import QtQuick.Controls.Material
import QtQuick.Controls.Material.impl

// LoadingIndicator is just a busy spinning indecator.
Popup {
    id: busyPopup
    modal: false
    closePolicy: Popup.NoAutoClose
    property bool showed: false
    visible: false
    width: parent.width
    height: parent.height

    onClosed: {
        showed = false
    }

    onOpened: {
        showed = true
    }

    background: Rectangle {
        color: Material.background
        opacity: 0.8
    }

    Item {
        property int radius: busyPopup.visible ? 25 : 0
        property color color: "#FAFAFA"
        property bool useCircle: false

        property alias running: timer.running

        property int _innerRadius: radius * 0.7
        property int _currentIndex: 0

        anchors.centerIn: parent
        width: radius * 2
        height: radius * 2

        Repeater {
            id: repeater
            model: 8
            delegate: Component {
                Rectangle {
                    property int _rotation: (360 / repeater.model) * index
                    property int _maxIndex: parent._currentIndex + 1
                    property int _minIndex: parent._currentIndex - 1

                    width: parent.useCircle ? (parent.radius - parent._innerRadius)
                                              * 2 : parent.width - (parent._innerRadius * 2)
                    height: parent.useCircle ? width : width * 0.5
                    x: parent._getPosOnCircle(_rotation).x
                    y: parent._getPosOnCircle(_rotation).y
                    radius: parent.useCircle ? width : 0
                    color: parent.color
                    opacity: (index >= _minIndex && index <= _maxIndex)
                             || (index === 0
                                 && parent._currentIndex + 1 > 7) ? 1 : 0.3
                    transform: Rotation {
                        angle: 360 - _rotation
                        origin {
                            x: 0
                            y: height / 2
                        }
                    }
                    transformOrigin: index >= repeater.model / 2 ? Item.Center : Item.Center

                    Behavior on opacity {
                        NumberAnimation {
                            duration: 200
                        }
                    }
                }
            }
        }

        Timer {
            id: timer
            interval: 80
            repeat: true
            running: true
            onTriggered: {
                if (parent._currentIndex === 7) {
                    parent._currentIndex = 0
                } else {
                    parent._currentIndex++
                }
            }
        }
        function _toRadian(degree) {
            return (degree * 3.14159265) / 180.0
        }

        function _getPosOnCircle(angleInDegree) {
            let centerX = parent.width / 2, centerY = parent.height / 2
            let posX = 0, posY = 0

            posX = centerX + parent._innerRadius * Math.cos(_toRadian(
                                                                angleInDegree))
            posY = centerY - parent._innerRadius * Math.sin(_toRadian(
                                                                angleInDegree))
            return Qt.point(posX, posY)
        }
    }
}
