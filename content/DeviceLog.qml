import QtQuick 2.15
import QtQuick.Controls 2.15
import hiomon 1.0

Rectangle {
    id: _root
    color: Material.background
    property int hborderWidth: _root.width + 10
    property bool searchStarted: false
    property bool filterStarted: false

    Connections {
        target: chester
        onDeviceLogReceived: (msg) => {
            textView.append(msg)
        }
    }

    function reset() {
        if (searchStarted) {
            searchStarted = false
            toolPanel.setUndoVisible(false)
            search.resetHighlights()
            textView.deselectOnPress = true
            return
        }
        if (filterStarted) {
            filterStarted = false
            toolPanel.setUndoVisible(false)
            textView.undoFilter()
            textInput.visible = true
            return
        }
    }

    Connections {
        target: toolPanel
        onClearClicked: {
            _root.reset()
            textView.clear()
        }
        onDownClicked: {
            textView.scrollToBottom()
        }
        onPauseClicked: {
            textView.togglePause()
        }
        onUndoClicked: {
            _root.reset()
        }
    }

    TextLabel {
        id: placeholderText
        textValue: "Device Log"
        bindFocusTo: textInput.focus || textView.focused
    }

    Search {
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
        }
        onFocusedChanged: {
            if (!search.isSearching)
                textInput.focus = true
        }
        Component.onCompleted: {
            search.view = listView
            textView.newItemArrived.connect(function() {
                search.searchFor(search.searchTerm)
            })
            textView.scrollDetected.connect(function() {
                search.searchFor(search.searchTerm)
            })
        }

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_N && event.modifiers === Qt.NoModifier) {
                search.findNext()
                event.accepted = true
            } else if (event.key === Qt.Key_N && event.modifiers === Qt.ShiftModifier) {
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
                color: mouseArea.containsMouse ? AppSettings.hoverColor : Material.background
                Image {
                    id: modeImage
                    source: AppSettings.searchIcon
                    anchors.centerIn: parent
                    height: 16
                    width: 16
                    smooth: true
                }
            }
        }
        MouseArea {
            id: mouseArea
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

        Keys.onReturnPressed: {
            var pattern = textInput.text
            if (pattern === "")
                return
            if (search.mode === "search") {
                search.searchFor(pattern)
                searchStarted = true
                textView.deselectOnPress = false
                search.findNext()
            } else {
                textView.filterFor(pattern)
                textInput.visible = false
                filterStarted = true
            }
            toolPanel.setUndoVisible(true)
            textInput.text = ""
            textView.forceActiveFocus()
        }

        // F5 to _root.reset search and Enter to start searching
        Keys.onPressed: (event) => {
            if ((event.key === Qt.Key_C) && (event.modifiers & Qt.ControlModifier)) {
                var txtIn = textInput.selectedText
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
                event.accepted = true;
            } else if (event.key === Qt.Key_Down) {
                textView.scrollDown()
                event.accepted = true;
            }
        }
    }
}

