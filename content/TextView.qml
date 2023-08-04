import QtQuick 2.15
import QtQuick.Controls 2.15
import hiomon 1.0

Item {
    id: _root
    property alias focused: view.focus
    property alias listView: view
    property bool deselectOnPress: true
    property bool paused: false
    property var msgsBuffer: []
    signal newItemArrived
    signal scrollDetected

    onPausedChanged: (paused) => {
        if (paused)
            return
        if (_root.msgsBuffer.length > 0) {
            for (var i in _root.msgsBuffer) {
                _root.append(_root.msgsBuffer[i])
            }
            scrollToBottom()
        }
        _root.msgsBuffer = []
    }

    function scrollToBottom() {
        view.positionViewAtEnd()
    }

    function togglePause() {
        _root.paused = !_root.paused
    }

    function clear() {
        messagesModel.clear()
        filteredModel.clear()
    }

    function copy() {
        copyPlaceholder.text = view.getSelectedText()
        copyPlaceholder.selectAll()
        copyPlaceholder.cut()
        copyPlaceholder.clear()
    }

    function append(msg) {
        if (_root.paused) {
            _root.msgsBuffer.push(msg)
            return
        }
        messagesModel.addMessage(msg)
    }

    function appendWithColor(msg, color) {
        messagesModel.addWithColor(msg, color)
    }

    function undoFilter() {
        view.model = null
        view.model = messagesModel
        filteredModel.clear()
        _root.paused = false
        view.forceLayout()
    }

    function filterFor(term) {
        _root.paused = true // some kind of mutex, otherwise new comming data will be in filter result due to race condition
        var filteredMessagesArray = messagesModel.getWithFilter(term)
        if (filteredMessagesArray.length <= 0) {
            notify.showError("No match for: " + term)
            return
        }
        filteredModel.clear()
        console.log("Filtered messages for the term: ", term)
        for (var i = 0; i < filteredMessagesArray.length; ++i) {
            console.log(filteredMessagesArray[i])
            filteredModel.addMessage(filteredMessagesArray[i])
        }
        view.model = null
        view.model = filteredModel
        view.forceLayout()
    }

    // Shortcuts for manual scrolling
    Keys.onPressed: (event) => {
        switch(event) {
            case Qt.Key_Up:
                view.contentY = Math.max(0, view.contentY - 20)
                event.accepted = true
                break
            case Qt.Key_Down:
                view.contentY = Math.min(view.contentHeight - view.height, view.contentY + 20)
                event.accepted = true
                break
        }
    }

    TextEdit {
        id: copyPlaceholder
        visible: false
    }

    MessageModel {
        id: messagesModel
    }

    MessageModel {
        id: filteredModel
    }

    ListView {
        id: view
        anchors.fill: parent
        model: messagesModel
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar {
            id: scrollBar
            policy: ScrollBar.AsNeeded
            minimumSize: 0.1
            width: 10
        }

        property bool autoScroll: true

        function indexOf(term) {
            return model.indexOf(term)
        }

        function indexAtRelative(x, y) {
            return indexAt(x + contentX, y + contentY)
        }

        onCountChanged: (count) => {
            if (count >= AppSettings.maxViewLines) {
                view.model.clear()
                return
            }
            view.autoscroll()
            newItemArrived()
        }

        onContentYChanged: {
            scrollDetected()
        }

        function autoscroll() {
            var scrolledToBottom = view.contentY >= view.contentHeight - view.height - AppSettings.scrollThreshold
            if (scrolledToBottom && view.autoScroll && view.count > 0) {
                _root.scrollToBottom()
            }
        }

        function getSelectedText() {
            var selectedText = ""
            for (var i = 0; i < view.count; ++i) {
                var item = view.itemAtIndex(i)
                if (item && (item.textEditObj.selectionStart !== item.textEditObj.selectionEnd))
                    selectedText += item.textEditObj.selectedText + "\n"
            }
            if (selectedText.length > 0)
                selectedText = selectedText.slice(0, -1)
            return selectedText
        }

        delegate: Row {
            property alias textEditObj: textEdit

            function positionAt(x, y) {
                return textEdit.positionAt(x, y)
            }

            // search only within row
            SearchComponent {
                id: internalSearch
            }

            Component.onCompleted: {
                internalSearch.onCompleted()
            }

            function searchFor(term) {
                internalSearch.searchFor(term)
                return internalSearch.getMatchedInds()
            }

            function reset() {
                internalSearch.reset()
                textEdit.deselect()
            }

            Text {
                id: rowNumbers
                text: index + 1
                // before 10000 the width 30 is okay but then it should be bigger
                width: text.length > 4 ? text.length * 10 + 1 : 30
                topPadding: textEdit.topPadding
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                color: AppSettings.grayColor
                font.family: textFont.name
                font.pixelSize: textEdit.font.pixelSize - 3
            }

            TextEdit {
                id: textEdit
                width: view.width
                wrapMode: Text.Wrap
                textFormat: TextEdit.RichText
                selectByMouse: false
                selectionColor: Material.accent
                leftPadding: 10
                topPadding: 1
                font.family: textFont.name
                font.pixelSize: 16
                text: model.display

                Connections {
                    target: selectionArea
                    function onSelectionChanged() {
                        textEdit.updateSelection()
                    }
                }

                // text selection handle
                function updateSelection() {
                    if (selectionArea.realStartIndex === -1 || selectionArea.realEndIndex === -1)
                        return
                    if (index < selectionArea.realStartIndex || index > selectionArea.realEndIndex)
                        textEdit.select(0, 0)
                    else if (index > selectionArea.realStartIndex && index < selectionArea.realEndIndex)
                        textEdit.selectAll()
                    else if (index === selectionArea.realStartIndex && index === selectionArea.realEndIndex)
                        textEdit.select(selectionArea.realStartPos, selectionArea.realEndPos)
                    else if (index === selectionArea.realStartIndex)
                        textEdit.select(selectionArea.realStartPos, textEdit.length)
                    else if (index === selectionArea.realEndIndex)
                        textEdit.select(0, selectionArea.realEndPos)
                }
            }
        }
    }

    MouseArea {
        id: selectionArea
        propagateComposedEvents: true
        property int selStartIndex
        property int selEndIndex
        property int selStartPos
        property int selEndPos
        property int realStartIndex: Math.min(selectionArea.selStartIndex, selectionArea.selEndIndex)
        property int realEndIndex: Math.max(selectionArea.selStartIndex, selectionArea.selEndIndex)
        property int realStartPos: (selectionArea.selStartIndex < selectionArea.selEndIndex) ?
                                    selectionArea.selStartPos : selectionArea.selEndPos
        property int realEndPos: (selectionArea.selStartIndex < selectionArea.selEndIndex) ?
                                    selectionArea.selEndPos : selectionArea.selStartPos
        property bool mouseDrag: false

        signal selectionChanged

        anchors {
            fill: view
            leftMargin: 30
        }

        enabled: !scrollBar.hovered
        cursorShape: enabled ? Qt.IBeamCursor : Qt.ArrowCursor

        function indexAndPos(x, y) {
            const index = view.indexAtRelative(x, y)
            if (index === -1)
                return [-1, -1]
            const item = view.itemAtIndex(index)
            const relItemY = item.y - view.contentY
            const pos = item.positionAt(x, y - relItemY)
            return [index, pos]
        }

        function deselectCurrentArea() {
            for (var i = realStartIndex; i <= realEndIndex; ++i) {
                var item = view.itemAtIndex(i);
                if (item)
                    item.reset();
            }
        }

        onPositionChanged: {
            if (!mouseDrag)
                return
            var [index, pos] = indexAndPos(mouseX, mouseY)
            if (index !== -1 && pos !== -1) {
                [selEndIndex, selEndPos] = [index, pos]
                selectionChanged()
            }
        }

        onPressed: {
            if (deselectOnPress) {
                deselectCurrentArea()
            }
            [selStartIndex, selStartPos] = indexAndPos(mouseX, mouseY)
            mouseDrag = true
            view.forceActiveFocus()
        }

        onReleased: {
            mouseDrag = false
        }
    }
}
