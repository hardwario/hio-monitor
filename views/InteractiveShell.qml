import QtQuick
import QtQuick.Controls

// InteractiveShell is a component which behaves like terminal with command history.
// This component is used by FlashPage and BluetoothPage.
Rectangle {
    id: _root
    color: Material.background
    property alias textInput: textInput
    property int hborderWidth: _root.width + 10
    property var device: undefined
    property string labelText: "INTERACTIVE SHELL"
    property string inputHint: "Enter command"
    property bool enableHistory: true

    function sendCommand(cmd) {
        if (cmd === "")
            return

        device.sendCommand(cmd)
    }

    function addMessage(msg) {
        const lines = msg.split('\n')

        for (var i = 0; i < lines.length; ++i) {
            textView.append(lines[i])
        }

        textView.scrollToBottom()
    }

    function clear() {
        textView.clear()
    }

    Connections {
        target: toolPanel

        function onSendCommand() {
            // flash page command run
            _root.sendCommand(textInput.text)
            textInput.text = ""
        }
    }

    TextLabel {
        id: placeholderText
        text: _root.labelText
        bindFocusTo: textInput.focus || textView.focused
        hborderWidth: _root.hborderWidth
    }

    TextView {
        id: textView

        anchors {
            left: parent.left
            right: parent.right
            top: placeholderText.bottom
            topMargin: 5
            bottom: textInput.top
            bottomMargin: 10
        }

        Connections {
            target: device

            function onDeviceMessageReceived(msg) {
                if (device.name === "bluetooth") {
                    _root.addMessage(msg)
                } else {
                    textView.append(msg)
                }

                textView.scrollToBottom()
            }
        }

        Keys.onPressed: function (event) {
            if ((event.key === Qt.Key_C)
                    && (event.modifiers & Qt.ControlModifier)) {
                const txtIn = textInput.selectedText
                if (txtIn !== "") {
                    textInput.copy()
                } else {
                    textView.copy()
                }
            }
        }
    }

    CommandHistory {
        id: cmdHistory
        textInput: textInput
        device: _root.device
    }

    TextField {
        id: textInput
        font.family: textFont.name
        font.pixelSize: 14

        placeholderText: {
            const enterCmd = "Enter command"

            if (enableHistory) {
                return cmdHistory.visible ? "Type to search for a command" : enterCmd
            }

            return inputHint === "" ? enterCmd : inputHint
        }

        height: 45
        anchors {
            // topMargin: 10
            bottom: parent.bottom
            bottomMargin: 2
            right: parent.right
            left: parent.left
        }
        leftPadding: 24

        background: Rectangle {
            implicitHeight: Material.textFieldHeight
            color: Material.background
            border.width: 0

            // top line
            Rectangle {
                anchors.top: parent.top
                width: _root.hborderWidth
                x: -5
                height: 1
                color: AppSettings.borderColor
            }

            Label {
                id: label
                property var fontMetrics: TextMetrics {
                    id: metrics
                    font.pixelSize: 24
                    text: ">"
                }
                anchors {
                    left: parent.left
                    leftMargin: 5
                    verticalCenter: parent.verticalCenter
                    verticalCenterOffset: -fontMetrics.tightBoundingRect.height / 4
                }
                text: ">"
                font.pixelSize: 24
                color: AppSettings.grayColor
            }

            SideButton {
                id: sendButton
                visible: textInput.text !== ""

                anchors {
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                }
                width: parent.height - 7
                height: parent.height - 1

                // TextField can not be anchored to the non parent or sibling item.
                // That's why the custom mouse area is used to capture mouse, because TextField overlays default MouseArea of SideButton
                customMouseArea: mouseArea

                iconSource: AppSettings.sendIcon
                iconWidth: 20
                iconHeight: 20
            }
        }

        MouseArea {
            id: mouseArea
            anchors.right: parent.right
            height: parent.height
            width: 40
            hoverEnabled: true
            onClicked: {
                _root.sendCommand(textInput.text)
                textInput.text = ""
            }
        }

        Connections {
            target: device

            function onSendCommandSucceeded(command) {
                textView.appendWithColor("> " + command, AppSettings.greenColor)
                textView.scrollToBottom()
            }

            function onSendCommandFailed(command) {
                const cmd = "> " + command
                const res = textView.replaceWithColor(cmd,
                                                      AppSettings.greenColor,
                                                      AppSettings.redColor)
                if (!res) {
                    textView.appendWithColor(cmd, AppSettings.redColor)
                }
                textView.scrollToBottom()
            }
        }

        Keys.onPressed: function (event) {
            if ((event.key === Qt.Key_C)
                    && (event.modifiers & Qt.ControlModifier)) {
                const txtIn = textInput.selectedText
                if (txtIn !== "") {
                    textInput.copy()
                } else {
                    textView.copy()
                }
                event.accepted = true
            }
            if (event.key === Qt.Key_R
                    && (event.modifiers & Qt.ControlModifier)) {
                if (enableHistory) {
                    cmdHistory.visible = !cmdHistory.visible
                    cmdHistory.resetList()
                    cmdHistory.filter()
                }
                event.accepted = true
            }
            if (event.key === Qt.Key_Up) {
                cmdHistory.up()
                event.accepted = true
            }
            if (event.key === Qt.Key_Down) {
                cmdHistory.down()
                event.accepted = true
            }
        }

        onTextChanged: {
            if (textInput.text === "")
                cmdHistory.setLast()

            cmdHistory.filter()
        }
    }

    function processEnter(event) {
        if (cmdHistory.visible) {
            textInput.text = cmdHistory.getSelected()
            cmdHistory.visible = false
            cmdHistory.resetList()
            event.accepted = true
            return
        }

        _root.sendCommand(textInput.text)
        textInput.text = ""
        event.accepted = true
    }

    Keys.onEnterPressed: function (event) {
        processEnter(event)
    }

    Keys.onReturnPressed: function (event) {
        processEnter(event)
    }
}
