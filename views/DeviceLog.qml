import QtCore
import QtQuick
import QtQuick.Dialogs
import Qt.labs.folderlistmodel
import QtQuick.Controls

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
        if (search.mode === "search") {
            toolPanel.setUndoVisible(false)
            search.resetHighlights()
            textView.deselectOnPress = true
        } else {
            toolPanel.setUndoVisible(false)
            textView.undoFilter()
            textInput.visible = true
        }

        textInput.forceActiveFocus()
    }

    function filterOrSearch() {
        const pattern = textInput.text
        if (pattern === "")
            return

        if (search.mode === "search") {
            search.searchFor(pattern)
            textView.deselectOnPress = false
            search.findNext()
        } else {
            textView.filterFor(pattern)
            textInput.visible = false
        }

        toolPanel.setUndoVisible(true)
        textInput.text = ""
        textView.listView.forceActiveFocus()
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
    }

    TextLabel {
        id: placeholderText
        textValue: "Device Log"
        bindFocusTo: textInput.focus || textView.focused
    }

    SearchHelper {
        id: search
        property string mode: "search"
    }

    TextView {
        id: textView
        height: parent.height - placeholderText.height - textInput.height
        anchors {
            left: parent.left
            right: parent.right
            top: placeholderText.bottom
            topMargin: 5
            bottom: textInput.top
            bottomMargin: 5
        }

        onFocusedChanged: {
            if (!search.isSearching)
                textInput.forceActiveFocus()
        }

        // to keep searching while new data is arriving
        // it won't do anything if the pattern is empty or search.isSearching is false
        Component.onCompleted: {
            search.view = listView

            textView.newItemArrived.connect(function () {
                search.searchFor(search.pattern)
            })

            textView.scrollDetected.connect(function () {
                search.searchFor(search.pattern)
            })
        }

        Keys.onPressed: function (event) {
            if (event.key === Qt.Key_N && event.modifiers === Qt.NoModifier) {
                search.findNext()
                event.accepted = true
            } else if (event.key === Qt.Key_N
                       && event.modifiers === Qt.ShiftModifier) {
                search.findPrevious()
                event.accepted = true
            } else if (event.key === Qt.Key_F5) {
                _root.reset()
                event.accepted = true
            }
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
            topMargin: 5
            bottomMargin: 2
            bottom: parent.bottom
            right: parent.right
            left: parent.left
        }
        leftPadding: 40 // so the text not overlaps with the search icon

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
                iconWidth: 20
                iconHeight: 20
            }
        }

        MouseArea {
            id: mouseAreaSend
            anchors.right: parent.right
            height: parent.height
            width: 40
            hoverEnabled: true
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

            onClicked: {
                if (search.mode === "search") {
                    search.mode = "filter"
                    modeImage.source = AppSettings.filterIcon
                } else {
                    search.mode = "search"
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
            } else if (event.key === Qt.Key_Up) {
                textView.scrollUp()
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                textView.scrollDown()
                event.accepted = true
            }
        }
    }
}
