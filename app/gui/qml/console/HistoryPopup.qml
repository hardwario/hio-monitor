// Ctrl+R command history (design system § 5): filters as you type (newest first, at most 50),
// Up / Down choose, Enter inserts the command into the input, Esc closes.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Popup {
    id: popup

    required property ConsoleViewModel viewModel
    property var results: []

    signal picked(string command)

    function refresh() {
        popup.results = popup.viewModel.historySearch(query.text);
        resultList.currentIndex = popup.results.length > 0 ? 0 : -1;
    }

    function pick() {
        if (resultList.currentIndex >= 0 && resultList.currentIndex < popup.results.length)
            popup.picked(popup.results[resultList.currentIndex]);
        popup.close();
    }

    objectName: "historyPopup"
    padding: Theme.spacingS
    focus: true
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: {
        query.text = "";
        popup.refresh();
        query.forceActiveFocus();
    }

    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusM
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS

        Label {
            text: qsTr("History — type to search")
            font: Theme.fontUiStrong
            color: Theme.text
        }

        TextField {
            id: query

            objectName: "historyQuery"
            Layout.fillWidth: true
            font: Theme.fontMono
            selectByMouse: true
            Accessible.name: qsTr("Search the command history")
            onTextEdited: popup.refresh()
            Keys.onUpPressed: resultList.decrementCurrentIndex()
            Keys.onDownPressed: resultList.incrementCurrentIndex()
            Keys.onReturnPressed: popup.pick()
            Keys.onEnterPressed: popup.pick()
        }

        ListView {
            id: resultList

            objectName: "historyList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 240)
            clip: true
            model: popup.results
            highlightMoveDuration: 0
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Commands")

            delegate: ItemDelegate {
                id: item

                required property int index
                required property string modelData

                width: ListView.view.width
                height: implicitContentHeight + 8
                highlighted: ListView.isCurrentItem
                Accessible.name: item.modelData
                onClicked: {
                    resultList.currentIndex = item.index;
                    popup.pick();
                }

                contentItem: Text {
                    text: item.modelData
                    textFormat: Text.PlainText
                    font: Theme.fontMono
                    color: Theme.text
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    color: item.highlighted ? Theme.selection : "transparent"
                }
            }

            Label {
                anchors.centerIn: parent
                visible: popup.results.length === 0
                text: qsTr("No matching commands")
                color: Theme.textMuted
            }
        }
    }
}
