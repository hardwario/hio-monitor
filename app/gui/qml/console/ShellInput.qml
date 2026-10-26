// The command input under the shell view (design system § 5): "> [_____] [Send]". Enter sends,
// Up / Down walk the history, Ctrl+R opens the history popup. Disabled with the reason
// ("Not connected", "Flashing — the console resumes automatically") as placeholder.
// Amended by 0021 (ADR-0170): while the input is non-empty the suggestion list opens above it
// (Settings › Console › Suggest commands while typing): ↑ ↓ move the highlight, Tab or → accept
// the highlighted row, Esc closes the list until the next keystroke; Enter sends the typed line
// unless ↑ ↓ moved the highlight (then it accepts the row). The rest of the highlighted row (or,
// without a list, of the newest matching history entry) is ghost text after the cursor: → or End
// accepts it. Tab completes (the highlighted row, else the common prefix of the device commands)
// and never leaves the field: Shift+Tab, or Esc then Tab, does; Ctrl+Tab still switches tabs.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: input

    required property ConsoleViewModel viewModel
    readonly property alias field: field
    readonly property bool historyOpen: history.visible
    readonly property alias suggestionList: suggestions
    // 0021: Esc closed the list (and the ghost text) until the next keystroke; Tab then leaves.
    property bool suppressed: false
    // 0021: ↑ ↓ moved the highlight since the last keystroke (Enter accepts the row then).
    property bool highlightMoved: false
    readonly property bool listEnabled: input.viewModel.settings.suggestions
    // The ghost text: the rest of the highlighted row, or of the newest matching history entry.
    readonly property string ghost: {
        if (input.suppressed || field.text === "" || !field.activeFocus
                || field.cursorPosition !== field.length)
            return "";
        const full = suggestions.visible && suggestions.currentRow !== null
                     ? suggestions.currentRow.text : input.historyHint;
        return full.startsWith(field.text) ? full.substring(field.text.length) : "";
    }
    property string historyHint: ""

    function openHistory() {
        if (input.viewModel.inputEnabled)
            history.open();
    }

    // Re-reads the suggestions for the current text (after every edit).
    function refreshSuggestions() {
        input.highlightMoved = false;
        input.historyHint = field.text === "" ? "" : input.viewModel.suggestion(field.text);
        if (!input.listEnabled || input.suppressed || field.text === "" || !input.viewModel.inputEnabled) {
            suggestions.close();
            return;
        }
        suggestions.rows = input.viewModel.suggestions(field.text);
        suggestions.currentIndex = 0;
        suggestions.reading = input.viewModel.readingCommands;
        if (suggestions.rows.length > 0 || suggestions.reading)
            suggestions.open();
        else
            suggestions.close();
    }

    function accept(text: string) {
        field.text = text;
        field.cursorPosition = field.length;
        input.suppressed = false;
        input.refreshSuggestions();
    }

    // Tab: the highlighted row, else the common prefix of the device commands.
    function complete() {
        if (suggestions.visible && suggestions.currentRow !== null) {
            input.accept(suggestions.currentRow.text);
            return;
        }
        const completion = input.viewModel.complete(field.text);
        input.suppressed = false;
        if (completion.replacement !== field.text)
            input.accept(completion.replacement);
        else
            input.refreshSuggestions();
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

                    // 0021 (ADR-0170): ghost text after the cursor.
                    Text {
                        objectName: "ghostText"
                        // The cursor is at the end whenever the ghost text shows; cursorRectangle
                        // notifies after the layout (positionToRectangle() in a binding does not
                        // re-evaluate when the text changes).
                        x: field.cursorRectangle.x
                        // Never past the field (over the Send button) when the text scrolls.
                        width: Math.max(0, field.width - x)
                        clip: true
                        anchors.verticalCenter: parent.verticalCenter
                        visible: input.ghost !== ""
                        text: input.ghost
                        textFormat: Text.PlainText
                        font: Theme.fontMono
                        color: Theme.textMuted
                        Accessible.ignored: true
                    }

                    onTextEdited: {
                        input.suppressed = false;
                        input.refreshSuggestions();
                    }
                    onActiveFocusChanged: if (!activeFocus) suggestions.close()

                    Keys.onReturnPressed: input.enter()
                    Keys.onEnterPressed: input.enter()
                    Keys.onUpPressed: {
                        if (suggestions.visible && suggestions.rows.length > 0) {
                            if (suggestions.move(-1))
                                input.highlightMoved = true;
                        } else {
                            field.text = input.viewModel.historyPrevious(field.text);
                        }
                    }
                    Keys.onDownPressed: {
                        if (suggestions.visible && suggestions.rows.length > 0) {
                            if (suggestions.move(1))
                                input.highlightMoved = true;
                        } else {
                            field.text = input.viewModel.historyNext(field.text);
                        }
                    }
                    Keys.onTabPressed: event => {
                        // Ctrl+Tab switches tabs; Tab after Esc leaves the field.
                        if ((event.modifiers & Qt.ControlModifier) || input.suppressed) {
                            event.accepted = false;
                            return;
                        }
                        input.complete();
                        event.accepted = true;
                    }
                    Keys.onEscapePressed: event => {
                        if (suggestions.visible || input.ghost !== "") {
                            suggestions.close();
                            input.suppressed = true;
                            event.accepted = true;
                        } else {
                            // Nothing to close: not consumed (review F6), but Tab now leaves.
                            input.suppressed = true;
                            event.accepted = false;
                        }
                    }
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_R && (event.modifiers & Qt.ControlModifier)) {
                            input.openHistory();
                            event.accepted = true;
                        } else if ((event.key === Qt.Key_Right || event.key === Qt.Key_End)
                                   && (event.modifiers & ~Qt.KeypadModifier) === Qt.NoModifier
                                   && field.cursorPosition === field.length) {
                            // → accepts the highlighted row; → or End accept the ghost text.
                            if (event.key === Qt.Key_Right && suggestions.visible && suggestions.currentRow !== null) {
                                input.accept(suggestions.currentRow.text);
                                event.accepted = true;
                            } else if (input.ghost !== "") {
                                input.accept(field.text + input.ghost);
                                event.accepted = true;
                            }
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
            onClicked: input.send()
        }
    }

    function send() {
        if (input.viewModel.send(field.text)) {
            field.clear();
            suggestions.close();
            input.historyHint = "";
        }
    }

    // Enter: sends the typed line, unless ↑ ↓ moved the highlight (then it accepts the row).
    function enter() {
        if (suggestions.visible && input.highlightMoved && suggestions.currentRow !== null) {
            input.accept(suggestions.currentRow.text);
            return;
        }
        input.send();
    }

    SuggestionList {
        id: suggestions

        x: Theme.spacingS
        y: -implicitHeight - Theme.spacingXs
        width: Math.min(640, input.width - 2 * Theme.spacingS)
        onPicked: text => {
            input.accept(text);
            field.forceActiveFocus();
        }
    }

    // 0021: the device command tree was learned (or is being read): update the open list.
    Connections {
        target: input.viewModel

        function onCommandTreeChanged() {
            if (field.activeFocus && field.text !== "" && !input.suppressed)
                input.refreshSuggestions();
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
