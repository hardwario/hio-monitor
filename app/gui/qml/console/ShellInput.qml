// The command input under the shell view (design system § 5): "> [_____] [Send]". Enter sends,
// Up / Down walk the history, Ctrl+R opens the history popup. Disabled with the reason
// ("Not connected", "Flashing — the console resumes automatically") as placeholder.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: input

    required property ConsoleViewModel viewModel
    readonly property alias field: field
    readonly property bool historyOpen: history.visible

    function openHistory() {
        if (input.viewModel.inputEnabled)
            history.open();
    }

    objectName: "shellInput"
    implicitHeight: row.implicitHeight + 2 * Theme.spacingS
    color: Theme.surface

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.border
    }

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.leftMargin: Theme.spacingS
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingS

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            radius: Theme.radiusS
            color: Theme.bg
            border.width: field.activeFocus ? 2 : 1
            border.color: field.activeFocus ? Theme.accent : Theme.border
            opacity: input.viewModel.inputEnabled ? 1 : 0.7

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingS
                anchors.rightMargin: Theme.spacingS
                spacing: Theme.spacingXs

                Label {
                    text: ">"
                    font: Theme.fontMono
                    color: Theme.shellEcho
                    Accessible.ignored: true
                }

                TextField {
                    id: field

                    objectName: "commandInput"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    background: null
                    leftPadding: 0
                    rightPadding: 0
                    topPadding: 0
                    bottomPadding: 0
                    verticalAlignment: TextInput.AlignVCenter
                    enabled: input.viewModel.inputEnabled
                    font: Theme.fontMono
                    color: Theme.text
                    selectByMouse: true
                    Accessible.name: qsTr("Command")
                    Accessible.description: input.viewModel.inputEnabled ? "" : input.viewModel.inputDisabledReason
                    // Own placeholder: Material's floats above the field.
                    Label {
                        objectName: "inputPlaceholder"
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        visible: field.text === "" && !field.preeditText
                        text: input.viewModel.inputEnabled
                              ? qsTr("Type a command, Enter to send · ↑↓ history · Ctrl+R search")
                              : input.viewModel.inputDisabledReason
                        font: Theme.fontMono
                        color: Theme.textMuted
                        elide: Text.ElideRight
                    }

                    Keys.onReturnPressed: sendButton.clicked()
                    Keys.onEnterPressed: sendButton.clicked()
                    Keys.onUpPressed: field.text = input.viewModel.historyPrevious(field.text)
                    Keys.onDownPressed: field.text = input.viewModel.historyNext(field.text)
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_R && (event.modifiers & Qt.ControlModifier)) {
                            input.openHistory();
                            event.accepted = true;
                        }
                    }
                }
            }
        }

        HButton {
            id: sendButton

            objectName: "sendButton"
            kind: "primary"
            text: qsTr("Send")
            enabled: input.viewModel.inputEnabled
            // The input keeps a command that was not sent (review K9).
            onClicked: if (input.viewModel.send(field.text)) field.clear()
        }
    }

    HistoryPopup {
        id: history

        viewModel: input.viewModel
        x: Theme.spacingS
        y: -implicitHeight - Theme.spacingXs
        width: Math.min(520, input.width - 2 * Theme.spacingS)
        onPicked: command => {
            field.text = command;
            field.forceActiveFocus();
        }
    }
}
