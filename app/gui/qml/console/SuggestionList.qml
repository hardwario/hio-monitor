// The automatic suggestion list above the shell input (plan 0021, ADR-0170): the matching device
// commands of the current word (help text muted), then the matching history entries (clock icon);
// 8 rows visible, at most 50 (ConsoleViewModel.suggestions). It never takes the focus: the input
// keeps it and moves the highlight (↑ ↓), accepts (Tab, →, Enter after ↑ ↓) and closes it (Esc).
// While the command list is being learned it says "Reading commands from the device…".
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Popup {
    id: list

    property var rows: []
    property int currentIndex: 0
    property bool reading: false
    readonly property var currentRow: list.currentIndex >= 0 && list.currentIndex < list.rows.length
                                      ? list.rows[list.currentIndex] : null
    readonly property int visibleRows: 8

    signal picked(string text)

    // Moves the highlight; true when it moved (not at the first / last row already).
    function move(step: int): bool {
        if (list.rows.length === 0)
            return false;
        const next = Math.max(0, Math.min(list.rows.length - 1, list.currentIndex + step));
        if (next === list.currentIndex)
            return false;
        list.currentIndex = next;
        rowList.positionViewAtIndex(list.currentIndex, ListView.Contain);
        return true;
    }

    objectName: "suggestionList"
    padding: Theme.spacingXs
    focus: false
    modal: false
    closePolicy: Popup.NoAutoClose

    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusM
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: 0

        Label {
            objectName: "readingCommands"
            Layout.fillWidth: true
            Layout.margins: Theme.spacingXs
            visible: list.reading
            text: qsTr("Reading commands from the device…")
            font: Theme.fontUiSmall
            color: Theme.textMuted
        }

        ListView {
            id: rowList

            objectName: "suggestionRows"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(rowList.contentHeight, list.visibleRows * (rowMetrics.height + 8))
            clip: true
            model: list.rows
            currentIndex: list.currentIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Suggestions")

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: ItemDelegate {
                id: row

                required property int index
                required property var modelData

                objectName: "suggestionRow" + row.index
                width: ListView.view.width
                height: rowMetrics.height + 8
                highlighted: row.index === list.currentIndex
                focusPolicy: Qt.NoFocus
                Accessible.name: row.modelData.kind === "history"
                                 ? qsTr("History: %1").arg(row.modelData.label)
                                 : row.modelData.label + (row.modelData.help !== "" ? ", " + row.modelData.help : "")
                onClicked: list.picked(row.modelData.text)

                contentItem: RowLayout {
                    spacing: Theme.spacingS

                    Icon {
                        Layout.alignment: Qt.AlignVCenter
                        symbol: row.modelData.kind === "history" ? "history" : "console"
                        size: 16
                        color: Theme.textMuted
                    }

                    Text {
                        Layout.maximumWidth: row.width * 0.6
                        text: row.modelData.label
                        textFormat: Text.PlainText
                        font: Theme.fontMono
                        color: Theme.text
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.help
                        textFormat: Text.PlainText
                        font: Theme.fontUiSmall
                        color: Theme.textMuted
                        elide: Text.ElideRight
                    }
                }

                background: Rectangle {
                    color: row.highlighted ? Theme.selection : "transparent"
                    radius: Theme.radiusS
                }
            }
        }
    }

    FontMetrics {
        id: rowMetrics

        font: Theme.fontMono
    }
}
