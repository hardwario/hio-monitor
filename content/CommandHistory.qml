import QtQuick 2.15
import QtQuick.Controls 2.15
import hiomon 1.0

Item {
    visible: false
    required property TextField textInput
    property var history: []
    property var device: undefined
    property bool lastShowed: false

    Component.onCompleted: {
        updateHistory()
    }

    function updateHistory() {
        history = device.getCommandHistory()
    }

    function getHistory() {
        return history
    }

    function up() {
        if (listView.model.length - 1 === listView.currentIndex && !visible && !lastShowed) {
            textInput.text = getSelected()
            lastShowed = true
            return
        }
        listView.decrementCurrentIndex()
        if (!visible) {
            textInput.text = getSelected()
            lastShowed = false
        }
    }

    function down() {
        listView.incrementCurrentIndex()
        if (!visible) {
            textInput.text = lastShowed ? "" : getSelected()
            lastShowed = listView.model.length - 1 === listView.currentIndex
        }
    }

    function setLast() {
        listView.currentIndex = listView.model.length - 1
    }

    function getLast() {
        setLast()
        return listView.model[listView.currentIndex]
    }

    function getSelected() {
        return listView.model[listView.currentIndex]
    }

    function resetList() {
        updateHistory()
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
            snapMode: ListView.SnapToItem
            clip: true
            Component.onCompleted: {
                if (!history) return
                listView.model = textInput.history
                listView.currentIndex = listView.model.length - 1
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
