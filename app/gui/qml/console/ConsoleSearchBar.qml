// The search bar of a console view (ADR-0056, design system § 5): under the log, always visible,
// with a Search | Filter switch, the field with Aa / .* toggles, "3 / 128" ↑ ↓ × (Search) or
// "Showing X of Y lines" + Clear filter (Filter), and the level chips D I W E. An active filter
// stays visible as a chip in Search mode. The shell variant (`shellVariant`) has Search only.
// Both modes keep their own query in the model (findText / filterText).
// Keys in the field: Enter / Shift+Enter next / previous match; Esc clears the search and gives the
// focus back to the view (`escaped`), where a second Esc clears the filter.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: bar

    required property ConsoleLineModel model
    property bool shellVariant: false
    property string mode: "search" // "search" | "filter"
    readonly property bool searchMode: bar.shellVariant || bar.mode === "search"
    readonly property string error: bar.searchMode ? bar.model.findError : bar.model.filterError

    signal escaped

    // Shows the bar's field focused in `newMode`.
    function focusField(newMode: string) {
        bar.mode = bar.shellVariant ? "search" : newMode;
        field.forceActiveFocus();
        field.selectAll();
    }

    implicitHeight: layout.implicitHeight + 2 * Theme.spacingXs
    color: Theme.surface
    Accessible.role: Accessible.ToolBar
    Accessible.name: bar.shellVariant ? qsTr("Search in shell") : qsTr("Search and filter the log")

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.border
    }

    component Segment: AbstractButton {
        id: segment

        checkable: true
        autoExclusive: true
        focusPolicy: Qt.StrongFocus
        implicitHeight: 26
        implicitWidth: segmentLabel.implicitWidth + 2 * Theme.spacingS
        Accessible.role: Accessible.RadioButton
        Accessible.name: segment.text
        Accessible.checked: segment.checked

        contentItem: Label {
            id: segmentLabel

            text: segment.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: Theme.fontUiSmall
            color: segment.checked ? Theme.onAccent : Theme.text
        }

        background: Rectangle {
            radius: Theme.radiusS
            color: segment.checked ? Theme.accent : "transparent"

            FocusRing {
                control: segment
                radiusOffset: Theme.radiusS
            }
        }
    }

    component Toggle: ToolButton {
        id: toggle

        property string description: ""

        checkable: true
        focusPolicy: Qt.StrongFocus
        implicitHeight: 24
        implicitWidth: Math.max(26, toggleLabel.implicitWidth + Theme.spacingS)
        padding: 0
        ToolTip.visible: toggle.hovered
        ToolTip.text: toggle.description
        ToolTip.delay: 600
        Accessible.name: toggle.description

        contentItem: Label {
            id: toggleLabel

            text: toggle.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: Theme.fontMonoSmall
            color: toggle.checked ? Theme.onAccent : Theme.textMuted
        }

        background: Rectangle {
            radius: Theme.radiusS
            color: toggle.checked ? Theme.accent : "transparent"

            FocusRing {
                control: toggle
                radiusOffset: Theme.radiusS
            }
        }
    }

    component LevelChip: AbstractButton {
        id: chip

        required property int bit
        required property color tint
        property string description: ""

        checkable: true
        checked: (bar.model.levelMask & chip.bit) !== 0
        focusPolicy: Qt.StrongFocus
        implicitHeight: 24
        implicitWidth: 26
        ToolTip.visible: chip.hovered
        ToolTip.text: chip.description
        ToolTip.delay: 600
        Accessible.role: Accessible.CheckBox
        Accessible.name: chip.description
        Accessible.checked: chip.checked
        onToggled: bar.model.levelMask = bar.model.levelMask ^ chip.bit

        contentItem: Label {
            text: chip.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: Theme.fontUiStrong
            color: chip.checked ? chip.tint : Theme.textMuted
            opacity: chip.checked ? 1 : 0.6
        }

        background: Rectangle {
            radius: Theme.radiusS
            color: chip.checked ? Theme.bg : "transparent"
            border.width: 1
            border.color: chip.checked ? chip.tint : Theme.border

            FocusRing {
                control: chip
                radiusOffset: Theme.radiusS
            }
        }
    }

    Flow {
        id: layout

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacingS
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingS

        Rectangle {
            visible: !bar.shellVariant
            implicitWidth: segments.implicitWidth + 4
            implicitHeight: 30
            radius: Theme.radiusS
            color: Theme.bg
            border.color: Theme.border

            Row {
                id: segments

                anchors.centerIn: parent
                spacing: 2

                Segment {
                    objectName: "searchModeButton"
                    text: qsTr("Search")
                    checked: bar.mode === "search"
                    onClicked: bar.focusField("search")
                }

                Segment {
                    objectName: "filterModeButton"
                    text: qsTr("Filter")
                    checked: bar.mode === "filter"
                    onClicked: bar.focusField("filter")
                }
            }
        }

        Rectangle {
            id: fieldBox

            implicitWidth: Math.max(160, Math.min(300, bar.width - (bar.shellVariant ? 200 : 400)))
            implicitHeight: 30
            radius: Theme.radiusS
            color: Theme.bg
            border.width: field.activeFocus || bar.error !== "" ? 2 : 1
            border.color: bar.error !== "" ? Theme.error : field.activeFocus ? Theme.accent : Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingS
                anchors.rightMargin: 3
                spacing: 2

                TextField {
                    id: field

                    objectName: "searchField"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    background: null
                    leftPadding: 0
                    rightPadding: 0
                    topPadding: 0
                    bottomPadding: 0
                    verticalAlignment: TextInput.AlignVCenter
                    font: Theme.fontUi
                    color: Theme.text
                    selectByMouse: true
                    text: bar.searchMode ? bar.model.findText : bar.model.filterText
                    Accessible.name: placeholder.text
                    Accessible.description: bar.error
                    // Own placeholder: Material's floats above the field.
                    Label {
                        id: placeholder

                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        visible: field.text === "" && !field.preeditText
                        text: bar.shellVariant ? qsTr("Search in shell")
                              : bar.searchMode ? qsTr("Search in log (Ctrl+F)")
                              : qsTr("Filter log lines (Ctrl+L)")
                        font: Theme.fontUi
                        color: Theme.textMuted
                        elide: Text.ElideRight
                    }

                    onTextEdited: {
                        if (bar.searchMode)
                            bar.model.findText = text;
                        else
                            bar.model.filterText = text;
                    }
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            if (event.modifiers & Qt.ShiftModifier)
                                bar.model.findPrevious();
                            else
                                bar.model.findNext();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Escape) {
                            if (bar.searchMode)
                                bar.model.findText = "";
                            bar.escaped();
                            event.accepted = true;
                        }
                    }
                }

                Toggle {
                    objectName: "caseToggle"
                    text: "Aa"
                    description: qsTr("Match case")
                    checked: bar.searchMode ? bar.model.findCase : bar.model.filterCase
                    onToggled: {
                        if (bar.searchMode)
                            bar.model.findCase = checked;
                        else
                            bar.model.filterCase = checked;
                    }
                }

                Toggle {
                    objectName: "regexToggle"
                    text: ".*"
                    description: qsTr("Regular expression")
                    checked: bar.searchMode ? bar.model.findRegex : bar.model.filterRegex
                    onToggled: {
                        if (bar.searchMode)
                            bar.model.findRegex = checked;
                        else
                            bar.model.filterRegex = checked;
                    }
                }
            }
        }

        // Search: "3 / 128", ↑ ↓, ×
        Row {
            visible: bar.searchMode
            spacing: 2
            height: 30

            Label {
                objectName: "matchCount"
                anchors.verticalCenter: parent.verticalCenter
                rightPadding: Theme.spacingXs
                visible: bar.model.findText !== "" && bar.error === ""
                text: bar.model.currentMatch > 0
                      ? qsTr("%1 / %2").arg(Qt.locale().toString(bar.model.currentMatch)).arg(Qt.locale().toString(bar.model.matchCount))
                      : qsTr("%Ln matches", "", bar.model.matchCount)
                font: Theme.fontUiSmall
                color: Theme.textMuted
            }

            HToolButton {
                objectName: "previousMatch"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "up"
                text: qsTr("Previous match")
                shortcutId: "console.findPrevious"
                display: AbstractButton.IconOnly
                enabled: bar.model.matchCount > 0
                onClicked: bar.model.findPrevious()
            }

            HToolButton {
                objectName: "nextMatch"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "down"
                text: qsTr("Next match")
                shortcutId: "console.findNext"
                display: AbstractButton.IconOnly
                enabled: bar.model.matchCount > 0
                onClicked: bar.model.findNext()
            }

            HToolButton {
                objectName: "clearSearch"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "close"
                text: qsTr("Clear the search")
                display: AbstractButton.IconOnly
                visible: bar.model.findText !== ""
                onClicked: bar.model.findText = ""
            }
        }

        // The active filter as a chip in Search mode.
        HButton {
            objectName: "filterChip"
            visible: !bar.shellVariant && bar.searchMode && bar.model.filterActive
            implicitHeight: 26
            kind: "normal"
            iconName: "close"
            text: bar.model.filterText !== "" ? qsTr("Filter: %1").arg(bar.model.filterText) : qsTr("Filter: levels")
            toolTipText: qsTr("Clear filter")
            onClicked: bar.model.clearFilter()
        }

        // Filter: "Showing X of Y lines", Clear filter
        Row {
            visible: !bar.searchMode
            spacing: Theme.spacingS
            height: 30

            Label {
                objectName: "filterCount"
                anchors.verticalCenter: parent.verticalCenter
                // 0021 (user decision 2026-10-23): only for a filter changed from the default
                // (hexdump lines hidden by the default chips do not count).
                visible: bar.model.filterActive
                text: qsTr("Showing %1 of %2 lines").arg(Qt.locale().toString(bar.model.visibleLines)).arg(Qt.locale().toString(bar.model.totalLines))
                font: Theme.fontUiSmall
                color: Theme.textMuted
            }

            HButton {
                objectName: "clearFilter"
                anchors.verticalCenter: parent.verticalCenter
                implicitHeight: 26
                visible: bar.model.filterActive
                text: qsTr("Clear filter")
                onClicked: bar.model.clearFilter()
            }
        }

        Label {
            objectName: "searchError"
            height: 30
            verticalAlignment: Text.AlignVCenter
            visible: bar.error !== ""
            text: qsTr("Invalid regular expression: %1").arg(bar.error)
            font: Theme.fontUiSmall
            color: Theme.error
        }

        Row {
            visible: !bar.shellVariant
            spacing: Theme.spacingXs
            height: 30
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Log levels")

            LevelChip {
                objectName: "levelDebug"
                anchors.verticalCenter: parent.verticalCenter
                text: "D"
                bit: 1
                tint: Theme.logDebug
                description: qsTr("Debug")
            }

            LevelChip {
                objectName: "levelInfo"
                anchors.verticalCenter: parent.verticalCenter
                text: "I"
                bit: 2
                tint: Theme.text
                description: qsTr("Info")
            }

            LevelChip {
                objectName: "levelWarning"
                anchors.verticalCenter: parent.verticalCenter
                text: "W"
                bit: 4
                tint: Theme.warning
                description: qsTr("Warning")
            }

            LevelChip {
                objectName: "levelError"
                anchors.verticalCenter: parent.verticalCenter
                text: "E"
                bit: 8
                tint: Theme.error
                description: qsTr("Error")
            }

            // 0021 (ADR-0171): hexdump continuation lines, off by default.
            LevelChip {
                objectName: "levelHexdump"
                anchors.verticalCenter: parent.verticalCenter
                text: "H"
                bit: 16
                tint: Theme.textMuted
                description: qsTr("Show hexdump lines")
            }
        }
    }
}
