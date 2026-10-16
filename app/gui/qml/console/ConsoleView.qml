// A console view (design system § 5, ADR-0045): header (title, line count), a virtualised ListView
// over a ConsoleLineModel (only visible rows are created), the "▼ N new lines" pill while not
// following and the "Waiting for data from the device…" text while empty.
// Keys in the view: Ctrl+A select all, Ctrl+C copy, End follow, n / Shift+N and F3 / Shift+F3 next
// / previous match, Esc (second Esc after the search bar: clear the filter).
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

    signal copyRequested
    signal escapePressed
    signal contextMenuRequested(Item item, real x, real y)

    // Follow (ADR-0046): keeps the end in view, but never while the user moves the list or holds
    // the scroll bar (review K3).
    function followEnd() {
        if (view.model.following && !list.moving && !list.dragging && !verticalBar.pressed)
            list.positionViewAtEnd();
    }

    Accessible.role: Accessible.Pane
    Accessible.name: view.title

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
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.consoleBg

            ListView {
                id: list

                objectName: "consoleList"
                anchors.fill: parent
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

                ScrollBar.vertical: ScrollBar {
                    id: verticalBar

                    objectName: "consoleScrollBar"
                    policy: ScrollBar.AsNeeded
                    // Holding the scroll bar stops following at once; releasing it at the end
                    // follows again (review K3).
                    onPressedChanged: view.model.following = !pressed && list.atYEnd
                }

                ScrollBar.horizontal: ScrollBar {
                    policy: view.wrap ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded
                }

                delegate: ConsoleRow {
                    id: rowItem

                    list: list
                    rowWidth: list.contentWidth
                    showTime: view.viewModel.settings.showTimestamps
                    showLevel: view.showLevels
                    wrap: view.wrap
                    rowFont: view.rowFont
                    levelWidth: levelMetrics.advanceWidth
                    onImplicitWidthChanged: if (implicitWidth > view.widest) view.widest = implicitWidth
                    onPressedRow: (index, extend) => {
                        list.forceActiveFocus();
                        view.model.select(index, extend);
                    }
                    onDraggedTo: index => view.model.select(index, true)
                    onContextMenuRequested: (x, y) => view.contextMenuRequested(rowItem, x, y)
                }

                // Follow: new rows keep the end in view; a wheel, drag or flick turns it off when it
                // starts and on again when it ends at the end (ADR-0046, review K3).
                onCountChanged: Qt.callLater(view.followEnd)
                onHeightChanged: Qt.callLater(view.followEnd)
                onMovementStarted: view.model.following = false
                onMovementEnded: view.model.following = list.atYEnd

                Keys.onPressed: event => {
                    if (event.matches(StandardKey.SelectAll)) {
                        view.model.selectAll();
                    } else if (event.matches(StandardKey.Copy)) {
                        view.copyRequested();
                    } else if (event.key === Qt.Key_End && event.modifiers === Qt.NoModifier) {
                        view.model.following = true;
                    } else if (event.key === Qt.Key_Home && event.modifiers === Qt.NoModifier) {
                        view.model.following = false;
                        list.positionViewAtBeginning();
                    } else if (event.key === Qt.Key_N && event.modifiers === Qt.NoModifier) {
                        view.model.findNext();
                    } else if (event.key === Qt.Key_N && event.modifiers === Qt.ShiftModifier) {
                        view.model.findPrevious();
                    } else if (event.key === Qt.Key_Escape) {
                        view.escapePressed();
                    } else {
                        return;
                    }
                    event.accepted = true;
                }
                Keys.onUpPressed: {
                    verticalBar.decrease();
                    view.model.following = false;
                }
                Keys.onDownPressed: {
                    verticalBar.increase();
                    view.model.following = list.atYEnd;
                }

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
