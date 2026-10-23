// One console row (ADR-0045): optional time column, level tag (log), text. Device text is always
// plain text (never rich text). A row with search matches is drawn by a read-only plain-text
// TextEdit, whose positionToRectangle() places the highlight rectangles of the model's
// matchRanges (character ranges of the same shown text; review K4/K5) behind the text, also when
// lines wrap. Sent commands ("> cmd") use the accent colour, marker rows are muted separators.
// Click selects, Shift+click and dragging extend the selection, right-click opens the menu.
pragma ComponentBehavior: Bound

import QtQuick
import HardwarioMonitor

Rectangle {
    id: row

    required property int index
    required property string text
    required property int level
    required property int direction
    required property string time
    required property bool selected
    required property bool currentMatch
    required property var matchRanges
    required property string levelTag

    property bool showTime: false
    property bool showLevel: false
    property bool wrap: false
    property font rowFont: Theme.fontMono
    property real rowWidth: 0
    property real levelWidth: 0 // the level column (one character, measured by the view)


    readonly property bool marker: row.direction === 2
    readonly property color textColour: row.marker ? Theme.textMuted
                                      : row.direction === 1 ? Theme.shellEcho
                                      : row.level === 1 ? Theme.logDebug
                                      : row.level === 3 ? Theme.warning
                                      : row.level === 4 ? Theme.error
                                      : row.level === 5 ? Theme.textMuted : Theme.logInfo // 5: hexdump (0021)

    objectName: "consoleRow"
    width: row.rowWidth
    implicitWidth: row.contentX + (row.hasMatches ? highlighted.implicitWidth : content.implicitWidth) + Theme.spacingS
    implicitHeight: Math.max(row.hasMatches ? highlighted.implicitHeight : content.implicitHeight,
                             timeText.implicitHeight) + 2
    height: implicitHeight
    color: row.selected ? Theme.selection : row.currentMatch ? Theme.findMatch : "transparent"
    Accessible.role: Accessible.ListItem
    Accessible.name: row.text
    Accessible.selected: row.selected

    // Plain positioned texts (no layout, no per-row metrics): rows are created and rebound often.
    Text {
        id: timeText

        x: Theme.spacingS
        y: 1
        visible: row.showTime
        width: row.showTime ? implicitWidth : 0
        text: row.showTime ? row.time : ""
        font: row.rowFont
        color: Theme.textMuted
        textFormat: Text.PlainText
    }

    Text {
        id: levelText

        x: timeText.x + timeText.width + (row.showTime ? Theme.spacingS : 0)
        y: 1
        visible: row.showLevel
        width: row.showLevel ? row.levelWidth : 0
        text: row.levelTag
        font.family: row.rowFont.family
        font.pointSize: row.rowFont.pointSize
        font.bold: true
        color: row.textColour
        textFormat: Text.PlainText
    }

    readonly property bool hasMatches: row.matchRanges !== undefined && row.matchRanges.length > 0
    readonly property real contentX: levelText.x + levelText.width + (row.showLevel ? Theme.spacingS : 0)

    Text {
        id: content

        x: row.contentX
        y: 1
        visible: !row.hasMatches
        width: row.wrap ? row.width - content.x - Theme.spacingS : implicitWidth
        text: row.hasMatches ? "" : row.text
        textFormat: Text.PlainText
        font.family: row.rowFont.family
        font.pointSize: row.rowFont.pointSize
        font.italic: row.marker
        color: row.textColour
        wrapMode: row.wrap ? Text.WrapAnywhere : Text.NoWrap
    }

    // Rows with matches only (a few per screen): the same text, plain, with highlight rectangles.
    Loader {
        id: highlighted

        objectName: "highlightedText"
        x: row.contentX
        y: 1
        active: row.hasMatches
        visible: active

        sourceComponent: Item {
            id: matchText

            // Rectangles {x, y, width, height} for the ranges; a range that wraps gets one per line.
            function rectangles(ranges: var): var {
                const result = [];
                const lineEnd = edit.width > 0 ? edit.width : edit.contentWidth;
                for (const range of ranges) {
                    if (range.start + range.length > edit.length)
                        continue; // the text is not set yet (binding order)
                    const first = edit.positionToRectangle(range.start);
                    const last = edit.positionToRectangle(range.start + range.length);
                    if (Math.abs(last.y - first.y) < 1) {
                        result.push({ x: first.x, y: first.y, width: last.x - first.x, height: first.height });
                        continue;
                    }
                    result.push({ x: first.x, y: first.y, width: lineEnd - first.x, height: first.height });
                    for (let y = first.y + first.height; y < last.y - 1; y += first.height)
                        result.push({ x: 0, y: y, width: lineEnd, height: first.height });
                    result.push({ x: 0, y: last.y, width: last.x, height: last.height });
                }
                return result;
            }

            implicitWidth: edit.implicitWidth
            implicitHeight: edit.implicitHeight
            width: row.wrap ? row.width - row.contentX - Theme.spacingS : implicitWidth
            height: edit.height

            Repeater {
                // Re-evaluated when the text, the ranges or the layout change.
                model: edit.length > 0 && edit.contentWidth >= 0 && edit.contentHeight >= 0 && edit.width >= 0
                       ? matchText.rectangles(row.matchRanges) : []

                delegate: Rectangle {
                    required property var modelData

                    objectName: "matchHighlight"
                    x: modelData.x
                    y: modelData.y
                    width: Math.max(1, modelData.width)
                    height: modelData.height
                    radius: 2
                    color: row.currentMatch ? Theme.findCurrent : Theme.findMatch
                }
            }

            TextEdit {
                id: edit

                width: row.wrap ? parent.width : implicitWidth
                text: row.text
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: false
                selectByKeyboard: false
                activeFocusOnPress: false
                font.family: row.rowFont.family
                font.pointSize: row.rowFont.pointSize
                font.italic: row.marker
                color: row.textColour
                wrapMode: row.wrap ? TextEdit.WrapAnywhere : TextEdit.NoWrap
                Accessible.ignored: true // the row exposes its text
            }
        }
    }
}
