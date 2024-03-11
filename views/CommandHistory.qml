import QtQuick
import QtQuick.Controls

import hiomon 1.0

// CommandHistory is a command history list with navigation, item filtering and selection.
Item {
    visible: popup.visible
    required property TextField textInput
    // this is an interface that's defined in deviceinterface.h
    required property var device
    property var history: device.history
    property int index: -1
    property string lastCommand: ""

    onVisibleChanged: {
        resetList()
    }

    onHistoryChanged: {
        resetList()
    }

    function toggle() {
        popup.visible = !popup.visible
    }

    function getHistory() {
        return history
    }

    function up() {
        dec()
        textInput.text = getSelected()
        lastCommand = textInput.text
    }

    function down() {
        if (inc()) {
            textInput.text = getSelected()
            lastCommand = textInput.text
            return
        }

        // if user presses down arrow on the last command, clear the text input
        if (textInput.text === lastCommand) {
            textInput.text = ""
        }
    }

    function inc() {
        const inc = index < listView.model.length - 1

        if (inc)
            index++

        return inc
    }

    function dec() {
        const dec = index > 0

        if (dec)
            index--

        return dec
    }

    function setLast() {
        if (!listView.model)
            return

        // if cmdHistory visible index should point to length of the model for accessing the last command with up arrow
        index = listView.model.length
        listView.currentIndex = index - 1
    }

    function getSelected() {
        if (listView.model.length === 0) {
            return ""
        }

        let ind = index

        if (ind === listView.model.length) {
            ind = ind - 1
        }

        return listView.model[ind]
    }

    function resetList() {
        if (!history)
            return

        listView.model = history
        setLast()
        listView.forceLayout()
    }

    function filter() {
        if (!popup.visible)
            return

        listView.model = history.filter(function (value) {
            return value.toLowerCase().indexOf(
                        textInput.text.toLowerCase()) !== -1
        })

        setLast()
    }

    Popup {
        id: popup
        x: textInput.x
        y: textInput.y - 110
        width: textInput.width - 1
        height: 100
        visible: parent.visible
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: AppSettings.borderColor
        }

        onClosed: {
            resetList()
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
                    color: isCurrent ? Qt.lighter(
                                           popup.background.color) : popup.background.color

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
