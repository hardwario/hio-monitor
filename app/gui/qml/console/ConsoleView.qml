// A console view (design system § 5, ADR-0045): header (title, line count), a virtualised ListView
// over a ConsoleLineModel (only visible rows are created), the "▼ N new lines" pill while not
// following and the "Waiting for data from the device…" text while empty.
// Keys in the view: Ctrl+A select all, Ctrl+C copy, End follow, n / Shift+N and F3 / Shift+F3 next
// / previous match, Esc (second Esc after the search bar: clear the filter).
// Amended by 0021:
//   - ADR-0169: whole-row scrolling - a WheelHandler consumes the wheel (a notch = the system's
//     lines per scroll, touchpad pixels accumulated to rows; no coasting), Up / Down one row,
//     PgUp / PgDn a page minus one row, Ctrl+Home / Ctrl+End (Ctrl+End follows); the top row is
//     aligned after a scroll bar drag. Touch drag keeps the Flickable behaviour.
//   - ADR-0173: the scroll bars sit beside the rows (vertical right, horizontal below; shown only
//     when needed), never over them; a pressed row suspends Follow until release; dragging a
//     selection past the top / bottom edge scrolls by whole rows and extends the selection.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

FocusScope {
    id: view

    required property ConsoleLineModel model
    required property ConsoleViewModel viewModel
    property string title: ""
    property bool showLevels: false
    readonly property alias list: list
    readonly property font rowFont: Qt.font({ family: Theme.fontMonoFamily, pointSize: view.viewModel.settings.fontSize })
    readonly property bool wrap: view.viewModel.settings.wrapLines
    property real widest: 0

    // ADR-0173: a row is pressed (click or drag selection): Follow is suspended until release.
    property bool rowPressed: false
    // ADR-0173: rows to scroll per tick while a drag selection is above (< 0) / below (> 0) the
    // rows (auto-scroll; faster the farther the pointer is), else 0.
    property int dragScroll: 0
    // ADR-0169: wheel deltas not yet applied (angle in 1/8 degrees, pixels).
    property real wheelAngle: 0
    property real wheelPixels: 0
    readonly property real rowHeight: Math.max(1, rowMetrics.height + 2)

    signal copyRequested
    signal escapePressed
    signal contextMenuRequested(Item item, real x, real y)

    // Follow (ADR-0046): keeps the end in view, but never while the user moves the list, holds the
    // scroll bar (review K3) or presses a row (ADR-0173).
    function followEnd() {
        if (view.model.following && !list.moving && !list.dragging && !verticalBar.pressed
                && !view.rowPressed)
            list.positionViewAtEnd();
    }

    // The index of the first (partly) visible row.
    function topRow(): int {
        const index = list.indexAt(1, list.contentY + 1);
        return index >= 0 ? index : 0;
    }

    // The number of whole rows in the view (at least 1).
    function pageRows(): int {
        const top = list.indexAt(1, list.contentY + 1);
        const bottom = list.indexAt(1, list.contentY + list.height - 1);
        if (top < 0 || bottom < 0)
            return Math.max(1, Math.floor(list.height / view.rowHeight));
        return Math.max(1, bottom - top);
    }

    // ADR-0169: moves the view by whole rows (negative = up) with the top row aligned; scrolling up
    // turns Follow off, reaching the end turns it on.
    function scrollRows(rows: int) {
        if (rows === 0 || list.count === 0)
            return;
        const target = Math.max(0, Math.min(list.count - 1, view.topRow() + rows));
        list.positionViewAtIndex(target, ListView.Beginning);
        if (rows < 0)
            view.model.following = false;
        else
            view.model.following = list.atYEnd;
    }

    // ADR-0169: after a scroll bar drag the top row is aligned (not at the end).
    function alignTop() {
        if (list.count > 0 && !list.atYEnd)
            list.positionViewAtIndex(view.topRow(), ListView.Beginning);
    }

    // ADR-0173: the row under a drag position (list coordinates), clamped to the visible rows.
    function dragTo(y: real) {
        const beyond = y < 0 ? y : y > list.height ? y - list.height : 0;
        view.dragScroll = beyond === 0 ? 0
                        : Math.sign(beyond) * Math.min(10, Math.ceil(Math.abs(beyond) / view.rowHeight));
        const clampedY = Math.max(1, Math.min(list.height - 1, y));
        const index = list.indexAt(1, list.contentY + clampedY);
        if (index >= 0)
            view.model.select(index, true);
    }

    Accessible.role: Accessible.Pane
    Accessible.name: view.title

    FontMetrics {
        id: rowMetrics

        font: view.rowFont
    }

    // ADR-0173: auto-scroll while a drag selection is past the top / bottom edge.
    Timer {
        interval: 60
        repeat: true
        running: view.rowPressed && view.dragScroll !== 0
        onTriggered: {
            view.scrollRows(view.dragScroll);
            const edge = view.dragScroll < 0 ? list.indexAt(1, list.contentY + 1)
                                             : list.indexAt(1, list.contentY + list.height - 1);
            if (edge >= 0)
                view.model.select(edge, true);
        }
    }

    TextMetrics {
        id: levelMetrics

        font.family: view.rowFont.family
        font.pointSize: view.rowFont.pointSize
        font.bold: true
        text: "W"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: header.implicitHeight + 2 * Theme.spacingXs
            color: Theme.surface

            RowLayout {
                id: header

                anchors.fill: parent
                anchors.leftMargin: Theme.spacingS
                anchors.rightMargin: Theme.spacingS
                spacing: Theme.spacingS

                Label {
                    text: view.title
                    font: Theme.fontUiStrong
                    color: Theme.text
                }

                Label {
                    objectName: "lineCount"
                    Layout.fillWidth: true
                    text: qsTr("%Ln lines", "", view.model.totalLines)
                    font: Theme.fontUiSmall
                    color: Theme.textMuted
                    elide: Text.ElideRight
                }

                BusyIndicator {
                    visible: view.model.searching
                    running: visible && !AppViewModel.reducedMotion
                    implicitWidth: 16
                    implicitHeight: 16
                    Accessible.name: qsTr("Searching")
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
        }

        Rectangle {
            id: body

            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.consoleBg

            // ADR-0173: the bars take layout space beside the list, only while they are needed.
            GridLayout {
                anchors.fill: parent
                columns: 2
                rowSpacing: 0
                columnSpacing: 0

                ListView {
                    id: list

                    objectName: "consoleList"
                    Layout.row: 0
                    Layout.column: 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    focus: true
                    activeFocusOnTab: true
                    model: view.model
                    reuseItems: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: view.wrap ? Flickable.VerticalFlick : Flickable.HorizontalAndVerticalFlick
                    contentWidth: view.wrap ? width : Math.max(width, view.widest)
                    Accessible.role: Accessible.List
                    Accessible.name: view.title

                    ScrollBar.vertical: verticalBar
                    ScrollBar.horizontal: horizontalBar

                    // ADR-0169: the wheel moves whole rows; no kinetic coasting (the event is consumed,
                    // so the Flickable never flicks on wheel input).
                    WheelHandler {
                        objectName: "consoleWheel"
                        target: null
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: event => {
                            // The dominant axis decides (a diagonal touchpad move keeps its
                            // vertical part); Shift+wheel scrolls horizontally (review F3).
                            const usePixels = event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0;
                            const dxRaw = usePixels ? event.pixelDelta.x : event.angleDelta.x;
                            const dyRaw = usePixels ? event.pixelDelta.y : event.angleDelta.y;
                            const shift = (event.modifiers & Qt.ShiftModifier) !== 0;
                            if (shift || Math.abs(dxRaw) > Math.abs(dyRaw)) {
                                if (!view.wrap) {
                                    const delta = shift && dxRaw === 0 ? dyRaw : dxRaw;
                                    const dx = usePixels ? delta : delta / 120 * 3 * levelMetrics.advanceWidth;
                                    list.contentX = Math.max(0, Math.min(list.contentWidth - list.width, list.contentX - dx));
                                }
                                return;
                            }
                            let rows = 0;
                            if (event.pixelDelta.y !== 0) {
                                view.wheelPixels += event.pixelDelta.y;
                                rows = Math.trunc(view.wheelPixels / view.rowHeight);
                                view.wheelPixels -= rows * view.rowHeight;
                            } else {
                                view.wheelAngle += event.angleDelta.y;
                                const lines = Math.max(1, Application.styleHints.wheelScrollLines);
                                const notches = Math.trunc(view.wheelAngle * lines / 120);
                                rows = notches;
                                view.wheelAngle -= notches * 120 / lines;
                            }
                            view.scrollRows(-rows);
                        }
                    }

                    delegate: ConsoleRow {
                        rowWidth: list.contentWidth
                        showTime: view.viewModel.settings.showTimestamps
                        showLevel: view.showLevels
                        wrap: view.wrap
                        rowFont: view.rowFont
                        levelWidth: levelMetrics.advanceWidth
                        onImplicitWidthChanged: if (implicitWidth > view.widest) view.widest = implicitWidth
                    }

                    // Row selection (click, Shift+click, drag) and the context menu for all rows
                    // (0021, ADR-0173): on the list itself, so a drag survives the pressed row being
                    // scrolled out (and its delegate reused). Dragging selects rows instead of
                    // flicking the list (desktop behaviour).
                    MouseArea {
                        id: rowMouse

                        objectName: "consoleRowMouse"
                        parent: list
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        preventStealing: true

                        function rowAt(y: real): int {
                            return list.indexAt(1, list.contentY + y);
                        }

                        onPressed: mouse => {
                            const index = rowMouse.rowAt(mouse.y);
                            const item = index >= 0 ? list.itemAtIndex(index) as ConsoleRow : null;
                            if (item === null)
                                return;
                            if (mouse.button === Qt.RightButton) {
                                if (!item.selected)
                                    view.model.select(index, false);
                                const point = rowMouse.mapToItem(item, mouse.x, mouse.y);
                                view.contextMenuRequested(item, point.x, point.y);
                                return;
                            }
                            view.rowPressed = true;
                            list.forceActiveFocus();
                            view.model.select(index, (mouse.modifiers & Qt.ShiftModifier) !== 0);
                        }
                        onPositionChanged: mouse => {
                            if (view.rowPressed && (mouse.buttons & Qt.LeftButton))
                                view.dragTo(mouse.y);
                        }
                        onReleased: rowMouse.endPress()
                        onCanceled: rowMouse.endPress()

                        function endPress() {
                            if (!view.rowPressed)
                                return;
                            view.rowPressed = false;
                            view.dragScroll = 0;
                            Qt.callLater(view.followEnd);
                        }
                    }

                    // Follow: new rows keep the end in view; a drag or flick turns it off when it
                    // starts and on again when it ends at the end (ADR-0046, review K3).
                    onCountChanged: Qt.callLater(view.followEnd)
                    onHeightChanged: Qt.callLater(view.followEnd)
                    onMovementStarted: view.model.following = false
                    onMovementEnded: view.model.following = list.atYEnd

                    Keys.onPressed: event => {
                        // Arrow and navigation keys carry KeypadModifier on macOS (review F4).
                        const modifiers = event.modifiers & ~Qt.KeypadModifier;
                        if (event.matches(StandardKey.SelectAll)) {
                            view.model.selectAll();
                        } else if (event.matches(StandardKey.Copy)) {
                            view.copyRequested();
                        } else if (event.key === Qt.Key_End && (modifiers === Qt.NoModifier
                                                                || modifiers === Qt.ControlModifier)) {
                            view.model.following = true;
                        } else if (event.key === Qt.Key_Home && (modifiers === Qt.NoModifier
                                                                 || modifiers === Qt.ControlModifier)) {
                            view.model.following = false;
                            list.positionViewAtBeginning();
                        } else if (event.key === Qt.Key_PageUp && modifiers === Qt.NoModifier) {
                            view.scrollRows(-Math.max(1, view.pageRows() - 1));
                        } else if (event.key === Qt.Key_PageDown && modifiers === Qt.NoModifier) {
                            view.scrollRows(Math.max(1, view.pageRows() - 1));
                        } else if (event.key === Qt.Key_N && modifiers === Qt.NoModifier) {
                            view.model.findNext();
                        } else if (event.key === Qt.Key_N && modifiers === Qt.ShiftModifier) {
                            view.model.findPrevious();
                        } else if (event.key === Qt.Key_Escape) {
                            view.escapePressed();
                        } else {
                            return;
                        }
                        event.accepted = true;
                    }
                    Keys.onUpPressed: view.scrollRows(-1)
                    Keys.onDownPressed: view.scrollRows(1)

                    Connections {
                        target: view.model

                        function onFollowingChanged() {
                            view.followEnd();
                        }

                        // Only Find next / previous moves the view (review K2): appends and evictions
                        // never re-centre it on the current match.
                        function onCurrentMatchMoved(row: int) {
                            list.positionViewAtIndex(row, ListView.Center);
                        }
                    }
                }

                ScrollBar {
                    id: verticalBar

                    objectName: "consoleScrollBar"
                    Layout.row: 0
                    Layout.column: 1
                    Layout.fillHeight: true
                    orientation: Qt.Vertical
                    policy: ScrollBar.AsNeeded
                    visible: list.contentHeight > list.height + 0.5
                    // Holding the scroll bar stops following at once; releasing it at the end
                    // follows again (review K3); elsewhere the top row is aligned (ADR-0169).
                    onPressedChanged: {
                        view.model.following = !pressed && list.atYEnd;
                        if (!pressed)
                            Qt.callLater(view.alignTop);
                    }
                }

                ScrollBar {
                    id: horizontalBar

                    objectName: "consoleHorizontalBar"
                    Layout.row: 1
                    Layout.column: 0
                    Layout.fillWidth: true
                    orientation: Qt.Horizontal
                    policy: view.wrap ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded
                    visible: !view.wrap && list.contentWidth > list.width + 0.5
                }
            }

            // Focus ring of the view (design system § 6).
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: Theme.focusRingWidth
                border.color: Theme.accent
                visible: list.activeFocus
            }

            Label {
                objectName: "waitingText"
                anchors.centerIn: parent
                visible: view.model.totalLines === 0 && view.viewModel.deviceActive
                text: qsTr("Waiting for data from the device…")
                color: Theme.textMuted
            }

            HButton {
                objectName: "newLinesPill"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.spacingM
                visible: !view.model.following && view.model.newLinesWhileNotFollowing > 0
                kind: "primary"
                text: qsTr("▼ %Ln new lines", "", view.model.newLinesWhileNotFollowing)
                toolTipText: AppViewModel.shortcuts.tooltip(qsTr("Follow new lines"), "console.follow")
                onClicked: view.model.following = true
            }
        }
    }
}
