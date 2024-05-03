import QtQuick
import QtQuick.Controls

import hiomon 1.0

// TextView is a component that holds a texts and provides searching and filtering functions.
Item {
    id: _root

    property alias focused: view.focus
    property alias listView: view
    property bool deselectOnPress: true
    property bool searching: false
    property string searchTerm: ""

    signal newItemArrived
    signal noMatchesFound
    signal matchesFound

    function scrollToBottom() {
        Qt.callLater(view.positionViewAtEnd)
    }

    function togglePause() {
        view.autoScroll = !view.autoScroll
        toolPanel.togglePause(view.autoScroll)
    }

    function pause() {
        view.autoScroll = false
        toolPanel.togglePause(false)
    }

    function resume() {
        view.autoScroll = true
        toolPanel.togglePause(true)
        scrollToBottom()
    }

    function clear() {
        messagesModel.clear()
    }

    function searchFor(term) {
        searchTerm = term
        view.searchTermLength = term.length
        messagesModel.searchAndHighlight(term)
        searching = true
    }

    function reset() {
        searching = false
        messagesModel.reset()
        scrollToBottom()
        resume()
    }

    function nextMatch() {
        messagesModel.nextMatch()
    }

    function prevMatch() {
        messagesModel.prevMatch()
    }

    function copy() {
        if (messagesModel.copyToClipboard()) {
            notify.showInfo("Copied to clipboard")
            return
        }

        notify.showWarn("Nothing to copy")
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

    function filterFor(term) {
        messagesModel.filterFor(term)
        searchTerm = term
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

    MessageModel {
        id: messagesModel
    }

    Connections {
        target: messagesModel

        function onCurrentMatchPositionChanged(row, index) {
            view.selectCurrentDeselectPrevious(row, index,
                                               view.searchTermLength)
        }

        function onFoundMatch(found) {
            if (found) {
                matchesFound()
            } else {
                noMatchesFound()
                searching = false
                view.searchTermLength = 0
                searchTerm = ""
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
        property var previousItem: null
        property int searchTermLength: 0

        boundsMovement: Flickable.StopAtBounds
        boundsBehavior: Flickable.DragOverBounds
        flickableDirection: Flickable.VerticalFlick
        interactive: true

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
                _root.scrollToBottom()
            }

            newItemArrived()
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

        delegate: Row {
            required property int index
            required property var model
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
                selectionColor: _root.searching ? "#ffff00" : Material.accent
                selectedTextColor: _root.searching ? "black" : "white"
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

                    messagesModel.addSelectedText(index, textEdit.selectedText)
                }
            }
        }
    }

    MouseArea {
        id: selectionArea

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
        property bool held: false
        property int initialMouseY

        signal selectionChanged

        anchors {
            fill: view
            leftMargin: 30
            rightMargin: scrollBar.width
        }

        drag.target: view

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

                if (item) {
                    messagesModel.deselect(i)
                    item.reset()
                }
            }
        }

        onMouseYChanged: {
            if (!held) {
                return
            }

            const factor = 5
            const deltaY = mouseY - initialMouseY

            if (mouseY < view.y) {
                // Cursor is above the visible area
                view.contentY += (mouseY - view.y) / factor
            } else if (mouseY > (view.y + view.height)) {
                // Cursor is below the visible area
                view.contentY += (mouseY - (view.y + view.height)) / factor
            }

            initialMouseY = mouseY
        }

        onPositionChanged: {
            if (!held) {
                return
            }

            const res = indexAndPos(mouseX, mouseY)
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
                messagesModel.clearCopyBuff()
            }

            [selStartIndex, selStartPos] = indexAndPos(mouseX, mouseY)
            initialMouseY = mouseY
            view.focus = true
        }

        pressAndHoldInterval: 50

        onPressAndHold: {
            held = true
            view.focus = true
        }

        onReleased: held = false
    }
}
