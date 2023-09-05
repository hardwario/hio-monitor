import QtQuick 2.15
import QtQuick.Controls 2.15


Rectangle {
    id: _root
    color: Material.background
    property alias textInput: textInput
    property int hborderWidth: _root.width + 10
    property var device: undefined
    property string labelText: "Interactive shell"
    property bool enableHistory: true

    function sendCommand(cmd) {
        if (cmd === "") return
        device.sendCommand(cmd)
    }

    function addMessage(msg) {
        var lines = msg.split('\n')
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
        onSendCommand: {
            _root.sendCommand(textInput.text)
            textInput.text = ""
        }
    }

    TextLabel {
        id: placeholderText
        text: _root.labelText
        bindFocusTo: textInput.focus || textView.focused
    }

    TextView {
        id: textView
        anchors {
            left: parent.left
            right: parent.right
            top: placeholderText.bottom
            bottom: textInput.top
        }
        onFocusedChanged: {
            textInput.focus = true
        }
        Connections {
            target: device
            onDeviceMessageReceived: (msg) => {
                if (device.name === "bluetooth") {
                    _root.addMessage(msg)
                } else {
                    textView.append(msg)
                }
                textView.scrollToBottom()
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
        property bool isHistoryVisible: false
        height: 45
        anchors {
            topMargin: 10
            bottomMargin: 2
            bottom: parent.bottom
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
                x: textInput.x - 5
                width: _root.hborderWidth
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
        }

        Connections {
            target: device
            onSendCommandSucceeded: (command) => {
                textView.appendWithColor("> " + command, AppSettings.greenColor)
                textView.scrollToBottom()
            }
            onSendCommandFailed: (command) => {
                const cmd = "> " + command
                const res = textView.replaceWithColor(cmd, AppSettings.greenColor, AppSettings.redColor)
                if (!res) {
                    textView.appendWithColor(cmd, AppSettings.redColor)
                }
                textView.scrollToBottom()
            }
        }

        Keys.onPressed: (event) => {
            if ((event.key === Qt.Key_C) && (event.modifiers & Qt.ControlModifier)) {
                var txtIn = textInput.selectedText
                if (txtIn !== "") {
                    textInput.copy()
                } else {
                    textView.copy()
                }
                event.accepted = true
            }
            if (event.key === Qt.Key_R && (event.modifiers & Qt.ControlModifier)) {
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

        Keys.onReturnPressed: (event) => {
            if (cmdHistory.visible) {
                text = cmdHistory.getSelected()
                cmdHistory.visible = false
                cmdHistory.resetList()
                event.accepted = true
                return
            }
            _root.sendCommand(textInput.text)
            textInput.text = ""
            event.accepted = true
        }

        onTextChanged: {
            if (textInput.text === "")
                cmdHistory.setLast()
            cmdHistory.filter()
        }
    }
}
