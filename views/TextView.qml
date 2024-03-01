import QtQuick
import QtQuick.Controls

import hiomon 1.0

// TextView is a component that holds a texts and provides searching and filtering functions.
Item {
    id: _root

    property alias focused: view.focus
    property alias listView: view
    property bool deselectOnPress: true

    signal newItemArrived
    signal scrollDetected
    signal noMatchesFound
    signal matchesFound

    function scrollToBottom() {
        view.positionViewAtEnd()
    }

    function togglePause() {
        view.autoScroll = !view.autoScroll
    }

    function clear() {
        messagesModel.clear()
        filteredModel.clear()
    }

    function searchFor(term) {
        messagesModel.searchAndHighlight(term)
        view.searchTermLength = term.length
    }

    function resetSearch() {
        messagesModel.resetHighlights()
        scrollToBottom()
    }

    function nextMatch() {
        messagesModel.nextMatch()
    }

    function prevMatch() {
        messagesModel.prevMatch()
    }

    function copy() {
        copyPlaceholder.text = view.getSelectedText()
        copyPlaceholder.selectAll()
        copyPlaceholder.cut()
        copyPlaceholder.clear()
    }

    function append(msg) {
        messagesModel.addMessage(msg)
    }

    function appendWithColor(msg, color) {
        messagesModel.addWithColor(msg, color)
    }

    function replaceWithColor(msg, oldColor, newColor) {
        return messagesModel.replaceWithColor(msg, oldColor, newColor)
    }

    function undoFilter() {
        view.model = messagesModel
        view.forceLayout()
        filteredModel.clear()
    }

    function filterFor(term) {
        const filteredMessagesArray = messagesModel.getWithFilter(term)
        if (filteredMessagesArray.length <= 0) {
            noMatchesFound()
            return
        }

        matchesFound()

        filteredModel.clear()
        filteredModel.setModel(filteredMessagesArray)
        view.model = filteredModel
        view.forceLayout()
    }

    // Shortcuts for manual scrolling
    Keys.onPressed: function (event) {
        switch (event) {
        case Qt.Key_Up:
            view.contentY = Math.max(0, view.contentY - 20)
            event.accepted = true
            break
        case Qt.Key_Down:
            view.contentY = Math.min(view.contentHeight - view.height,
                                     view.contentY + 20)
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

    Connections {
        target: messagesModel

        function onCurrentMatchPositionChanged(row, index) {
            view.selectCurrentDeselectPrevious(row, index,
                                               view.searchTermLength)
        }

        function onSearchFoundMatch(found) {
            if (found) {
                matchesFound()
            } else {
                noMatchesFound()
                view.searchTermLength = 0
            }
        }
    }

    ListView {
        id: view
        anchors.fill: parent
        model: messagesModel
        clip: true

        reuseItems: true
        property bool autoScroll: true
        property int searchTermLength: 0
        property var previousItem: null

        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar {
            id: scrollBar
            policy: ScrollBar.AsNeeded
            minimumSize: 0.1
            width: 10
        }

        Component.onCompleted: {
            _root.scrollToBottom()
        }

        function indexOf(term) {
            return model.indexOf(term)
        }

        function indexAtRelative(x, y) {
            return indexAt(x + contentX, y + contentY)
        }

        onCountChanged: {
            const count = view.count

            if (count === 0) {
                return
            }

            if (count >= AppSettings.maxViewLines) {
                view.model.clear()
                return
            }

            if (view.autoScroll) {
                view.positionViewAtEnd()
            }

            newItemArrived()
        }

        onContentYChanged: {
            scrollDetected()
        }

        function selectCurrentDeselectPrevious(row, index, length) {
            if (row < 0 || row >= count)
                return

            if (previousItem) {
                previousItem.textEditObj.deselect()
            } else {
                // if there is no previous item, then the search has been just started, so scroll to the row
                view.positionViewAtIndex(row, ListView.Beginning)
            }

            const item = view.itemAtIndex(row)

            if (!item)
                return

            previousItem = item
            item.textEditObj.deselect()
            view.positionViewAtIndex(row, ListView.Center)
            item.textEditObj.select(index, index + length)
            view.forceLayout()
        }

        function getSelectedText() {
            let selectedText = ""

            for (var i = 0; i < view.count; ++i) {
                const item = view.itemAtIndex(i)

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

            function reset() {
                textEdit.deselect()
            }

            Text {
                id: rowNumbers
                text: index + 1
                width: text.length > 4 ? text.length * 10 + 1 : 40
                topPadding: textEdit.topPadding
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                color: AppSettings.grayColor
                font.family: textFont.name
                font.pixelSize: textEdit.font.pixelSize - 3
            }

            TextEdit {
                id: textEdit
                width: view.width - 35
                wrapMode: Text.Wrap
                readOnly: true
                textFormat: TextEdit.RichText
                selectByMouse: false
                selectionColor: Material.accent
                leftPadding: 10
                topPadding: 1
                font.family: textFont.name
                font.pixelSize: 16
                text: modelData

                Connections {
                    target: selectionArea

                    function onSelectionChanged() {
                        textEdit.updateSelection()
                    }
                }

                // text selection handle
                function updateSelection() {
                    if (selectionArea.realStartIndex === -1
                            || selectionArea.realEndIndex === -1)
                        return
                    if (index < selectionArea.realStartIndex
                            || index > selectionArea.realEndIndex)
                        textEdit.select(0, 0)
                    else if (index > selectionArea.realStartIndex
                             && index < selectionArea.realEndIndex)
                        textEdit.selectAll()
                    else if (index === selectionArea.realStartIndex
                             && index === selectionArea.realEndIndex)
                        textEdit.select(selectionArea.realStartPos,
                                        selectionArea.realEndPos)
                    else if (index === selectionArea.realStartIndex)
                        textEdit.select(selectionArea.realStartPos,
                                        textEdit.length)
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
        property int realStartIndex: Math.min(selectionArea.selStartIndex,
                                              selectionArea.selEndIndex)
        property int realEndIndex: Math.max(selectionArea.selStartIndex,
                                            selectionArea.selEndIndex)
        property int realStartPos: (selectionArea.selStartIndex < selectionArea.selEndIndex) ? selectionArea.selStartPos : selectionArea.selEndPos
        property int realEndPos: (selectionArea.selStartIndex < selectionArea.selEndIndex) ? selectionArea.selEndPos : selectionArea.selStartPos
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
                const item = view.itemAtIndex(i)

                if (item)
                    item.reset()
            }
        }

        onPositionChanged: {
            if (!mouseDrag) {
                return
            }

            let res = []
            res = indexAndPos(mouseX, mouseY)

            const index = res[0]
            const pos = res[1]

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
            view.focus = true
        }

        onReleased: {
            mouseDrag = false
        }
    }
}
