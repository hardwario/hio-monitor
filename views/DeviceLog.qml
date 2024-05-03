import QtCore
import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls
import Qt.labs.folderlistmodel

import hiomon 1.0

// DeviceLog component is complex device log view with search and filter functionality.
// Search functionality includes vim-like navigation with searched term highlighting.
Rectangle {
    id: _root
    color: Material.background
    property int hborderWidth: _root.width + 10

    Connections {
        target: chester

        function onDeviceLogReceived(msg) {
            textView.append(msg)
        }

        function onAttachSucceeded() {
            // there might be some messages already in the buffer
            textView.scrollToBottom()
        }
    }

    function clear() {
        _root.reset()
        textView.clear()
    }

    function reset() {
        textView.deselectOnPress = true
        textView.reset()
        toolPanel.setUndoVisible(false)
        toolPanel.setNavigationVisible(false)
        textInput.visible = true
        textInput.focus = true
    }

    function filterOrSearch() {
        const pattern = textInput.text
        if (pattern === "")
            return

        if (textView.mode === "Search") {
            textView.searchFor(pattern)
            textView.deselectOnPress = false
            textView.nextMatch()
        } else {
            textView.filterFor(pattern)
        }

        textInput.text = ""
    }

    Connections {
        target: textView

        function onNoMatchesFound() {
            _root.reset()
            notify.showError("No matches found")
        }

        function onMatchesFound() {
            textInput.visible = false
            toolPanel.setUndoVisible(true)
            toolPanel.setNavigationVisible(textView.mode === "Search")
            textView.pause()
            textView.focus = true
            textInput.focus = false
            textInput.visible = false
        }
    }

    // file dialog to open a log file
    FileDialog {
        id: fileDialog
        nameFilters: ["All files (*)"]
        currentFolder: StandardPaths.standardLocations(
                           StandardPaths.AppDataLocation)[0]
        onAccepted: {
            Qt.openUrlExternally(selectedFile)
        }
    }

    Connections {
        target: toolPanel

        function onPauseClicked() {
            textView.togglePause()
        }

        function onUndoClicked() {
            _root.reset()
        }

        function onOpenLogFileClicked() {
            fileDialog.open()
        }

        function onUpClicked() {
            textView.prevMatch()
        }

        function onDownClicked() {
            textView.nextMatch()
        }
    }

    TextLabel {
        id: placeholderText
        textValue: "DEVICE LOG"
        bindFocusTo: textInput.focus || textView.focused || textView.focus
        hborderWidth: _root.hborderWidth
    }

    TextView {
        id: textView
        property string mode: "Search"

        anchors {
            left: parent.left
            right: parent.right
            top: placeholderText.bottom
            topMargin: 3
            bottom: textInput.top
            bottomMargin: 5
        }

        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_N && event.modifiers === Qt.NoModifier) {
                textView.nextMatch()
                event.accepted = true
            } else if (event.key === Qt.Key_N
                       && event.modifiers === Qt.ShiftModifier) {
                textView.prevMatch()
                event.accepted = true
            } else if (event.key === Qt.Key_F5) {
                _root.reset()
                event.accepted = true
            } else if ((event.key === Qt.Key_C)
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

    Rectangle {
        id: guideMessage

        visible: !textInput.visible

        height: textInput.height
        width: textInput.width

        anchors {
            bottom: parent.bottom
            bottomMargin: 2
            left: parent.left
        }

        color: Material.background

        TextLabel {
            textValue: textView.mode + "ing for: " + textView.searchTerm
            bindFocusTo: true
        }
    }

    // Input field that is used to start searching for a pattern
    TextField {
        id: textInput
        visible: true
        height: 45
        font.family: textFont.name
        font.pixelSize: 14

        anchors {
            bottom: parent.bottom
            bottomMargin: 2
            right: parent.right
            left: parent.left
        }
        leftPadding: 40 // so the text not overlaps with the search icon

        placeholderText: textView.mode === "Search" ? "Type to search..." : "Type to filter..."

        background: Rectangle {
            implicitHeight: Material.textFieldHeight
            color: Material.background
            border.width: 0

            // top border line
            Rectangle {
                anchors.top: parent.top
                width: _root.hborderWidth
                height: 1
                color: AppSettings.borderColor
            }

            Rectangle {
                width: parent.height - 7
                height: parent.height - 1
                anchors {
                    left: parent.left
                    verticalCenter: parent.verticalCenter
                }
                color: mouseAreaMode.containsMouse ? AppSettings.hoverColor : Material.background

                Image {
                    id: modeImage
                    source: AppSettings.searchIcon
                    anchors.centerIn: parent
                    height: 16
                    width: 16
                    smooth: true
                }
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
                customMouseArea: mouseAreaSend
                iconSource: AppSettings.sendIcon
                iconWidth: 25
                iconHeight: 20
            }
        }

        MouseArea {
            id: mouseAreaSend
            anchors.right: parent.right
            height: parent.height
            width: 40
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                _root.filterOrSearch()
            }
        }

        MouseArea {
            id: mouseAreaMode
            anchors.left: parent.left
            height: parent.height
            width: parent.leftPadding
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor

            onClicked: {
                if (textView.mode === "Search") {
                    textView.mode = "Filter"
                    modeImage.source = AppSettings.filterIcon
                } else {
                    textView.mode = "Search"
                    modeImage.source = AppSettings.searchIcon
                }
            }
        }

        Keys.onEnterPressed: {
            _root.filterOrSearch()
        }

        Keys.onReturnPressed: {
            _root.filterOrSearch()
        }

        // F5 to _root.reset search and Enter to start searching
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
            } else if (event.key === Qt.Key_F5) {
                _root.reset()
                event.accepted = true
            }
        }
    }
}
