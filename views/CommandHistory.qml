import QtQuick 2.15
import QtQuick.Controls 2.15
import hiomon 1.0

Item {
    visible: false
    required property TextField textInput
    required property var device
    property var history: device.history
    property int index: 0

    onVisibleChanged: {
        resetList()
    }

    onHistoryChanged: {
        resetList()
    }

    function getHistory() {
        return history
    }

    function up() {
        dec()
        textInput.text = getSelected()
    }

    function down() {
        inc()
        textInput.text = getSelected()
    }

    function inc() {
        if (index < listView.model.length)
            index++
    }

    function dec() {
        if (index > 0)
            index--
    }

    function setLast() {
        if (!listView.model) return
        index = listView.model.length - 1
        listView.currentIndex = listView.model.length - 1
    }

    function getSelected() {
        if (index === listView.model.length)
            return ""
        return listView.model[index]
    }

    function resetList() {
        if (!history) return
        listView.model = history
        setLast()
        listView.forceLayout()
    }

    function filter() {
        if (!popup.visible) return
        listView.model = history.filter(function(value) {
            return value.toLowerCase().indexOf(textInput.text.toLowerCase()) !== -1;
        })
        setLast()
    }

    Popup {
        id: popup
        x: textInput.x
        y: textInput.y - 101
        width: textInput.width - 1
        height: 100
        visible: parent.visible
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: AppSettings.borderColor
        }

        ListView {
            id: listView
            implicitHeight: parent.height
            implicitWidth: parent.width
            model: history
            snapMode: ListView.SnapToItem
            clip: true

            Component.onCompleted: {
                resetList()
            }

            delegate: Item {
                width: listView.width
                height: listView.height / 4
                property bool isCurrent: ListView.isCurrentItem
                Rectangle {
                    id: delegateRectangle
                    width: parent.width
                    height: parent.height
                    anchors.centerIn: parent
                    color: isCurrent ? Qt.lighter(popup.background.color) : popup.background.color

                    Text {
                        text: modelData
                        color: AppSettings.whiteColor
                        anchors {
                            left: parent.left
                            leftMargin: 5
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            textInput.text = modelData
                        }
                    }
                }
            }
        }
    }
}
