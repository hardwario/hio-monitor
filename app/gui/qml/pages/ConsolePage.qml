// The console page (plan/features/0005-m3-console.md; ADR-0045, ADR-0046, ADR-0056, ADR-0061):
// toolbar, banners, the console's own ErrorPanel, and the Shell | Log split (resizable, stacked
// below 1000 px or by choice, panes swappable). The shell pane has the command input and a
// Search-only bar (Ctrl+F in the shell); the log pane has the Search | Filter bar. The page gets
// `tab` and `vm` from PageHost (page-extension contract, ADR-0059). All behaviour lives in the
// ConsoleViewModel; this file only binds and forwards.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import HardwarioMonitor

Item {
    id: page

    property DeviceTabViewModel tab
    property ConsoleViewModel vm
    // 0 = shell, 1 = log: the view that Ctrl+F, Ctrl+K and Ctrl+S act on.
    property int focusedChannel: 1
    property bool shellSearchVisible: false
    readonly property bool active: page.visible && page.vm !== null && page.tab === AppViewModel.currentTabViewModel
                                   && AppViewModel.globalPage === ""
    readonly property bool compact: AppViewModel.railCompact

    function announce(text: string) {
        Accessible.announce(text);
    }

    objectName: "consolePage"
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Console")

    Binding {
        target: page.vm
        property: "pageWidth"
        value: Math.round(page.width)
        when: page.vm !== null && page.width > 0
    }

    Connections {
        target: page.vm

        function onAnnounce(text) {
            if (page.active)
                page.announce(text);
        }
    }

    // The content needs the view model (PageHost sets `vm` after loading the page).
    Loader {
        objectName: "consoleContent"
        anchors.fill: parent
        active: page.vm !== null && page.tab !== null

        sourceComponent: Item {
            id: content

            function openSearch() {
                if (page.focusedChannel === 0) {
                    page.shellSearchVisible = true;
                    shellBar.focusField("search");
                } else {
                    logBar.focusField("search");
                }
            }

            function focusedModel(): ConsoleLineModel {
                return page.focusedChannel === 0 ? page.vm.shell : page.vm.log;
            }

            // ---- shortcuts (scope "console", ADR-0051; F4 is the shell's) ----
            Shortcut {
                enabled: page.active
                sequence: AppViewModel.shortcuts.portableText("console.find")
                onActivated: content.openSearch()
            }

            Shortcut {
                enabled: page.active
                sequence: AppViewModel.shortcuts.portableText("console.findNext")
                onActivated: content.focusedModel().findNext()
            }

            Shortcut {
                enabled: page.active
                sequence: AppViewModel.shortcuts.portableText("console.findPrevious")
                onActivated: content.focusedModel().findPrevious()
            }

            Shortcut {
                enabled: page.active
                sequence: AppViewModel.shortcuts.portableText("console.filter")
                onActivated: logBar.focusField("filter")
            }

            Shortcut {
                enabled: page.active
                sequence: AppViewModel.shortcuts.portableText("console.clear")
                onActivated: page.vm.clear(page.focusedChannel === 0 ? 1 : 2)
            }

            Shortcut {
                enabled: page.active
                sequence: AppViewModel.shortcuts.portableText("console.save")
                onActivated: saveDialog.openFor(page.focusedChannel)
            }

            Shortcut {
                enabled: page.active
                sequence: AppViewModel.shortcuts.portableText("console.pause")
                onActivated: page.vm.paused = !page.vm.paused
            }

            Shortcut {
                enabled: page.active && page.vm.canRunBatch
                sequence: AppViewModel.shortcuts.portableText("console.batch")
                onActivated: batchDialog.open()
            }

            Shortcut {
                enabled: page.active
                sequence: AppViewModel.shortcuts.portableText("console.focusInput")
                onActivated: shellInput.field.forceActiveFocus()
            }

            Shortcut {
                enabled: page.active
                sequence: AppViewModel.shortcuts.portableText("console.fontLarger")
                onActivated: page.vm.settings.increaseFontSize()
            }

            Shortcut {
                enabled: page.active
                sequence: AppViewModel.shortcuts.portableText("console.fontSmaller")
                onActivated: page.vm.settings.decreaseFontSize()
            }

            Shortcut {
                enabled: page.active
                sequence: AppViewModel.shortcuts.portableText("console.fontReset")
                onActivated: page.vm.settings.resetFontSize()
            }

            // ---- dialogs and menus ----
            FileDialog {
                id: saveDialog

                property int channel: 1

                function openFor(newChannel: int) {
                    saveDialog.channel = newChannel;
                    saveDialog.open();
                }

                title: qsTr("Save view")
                fileMode: FileDialog.SaveFile
                defaultSuffix: "txt"
                nameFilters: [qsTr("Text files (*.txt *.log)"), qsTr("All files (*)")]
                onAccepted: page.vm.saveView(saveDialog.channel, saveDialog.selectedFile)
            }

            FileDialog {
                id: batchDialog

                title: qsTr("Run a batch file")
                fileMode: FileDialog.OpenFile
                nameFilters: [qsTr("Text files (*.txt)"), qsTr("All files (*)")]
                onAccepted: page.vm.runBatch(batchDialog.selectedFile)
            }

            Menu {
                id: rowMenu

                property int channel: 1

                MenuItem {
                    text: qsTr("Copy")
                    onTriggered: page.vm.copySelection(rowMenu.channel)
                }

                MenuItem {
                    text: qsTr("Select all")
                    onTriggered: (rowMenu.channel === 0 ? page.vm.shell : page.vm.log).selectAll()
                }

                MenuSeparator {}

                MenuItem {
                    text: rowMenu.channel === 0 ? qsTr("Clear the shell") : qsTr("Clear the log")
                    onTriggered: page.vm.clear(rowMenu.channel === 0 ? 1 : 2)
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // ---- toolbar (design system § 5: page actions left; Follow, Pause, ⋯ right) ----
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: toolbar.implicitHeight + 2 * Theme.spacingXs
                    color: Theme.surface

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.border
                    }

                    RowLayout {
                        id: toolbar

                        objectName: "consoleToolbar"
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingS
                        anchors.rightMargin: Theme.spacingS
                        spacing: Theme.spacingXs
                        Accessible.role: Accessible.ToolBar
                        Accessible.name: qsTr("Console actions")

                        HToolButton {
                            id: resetButton

                            objectName: "resetButton"
                            iconName: "reset"
                            text: qsTr("Reset")
                            compact: page.compact
                            enabled: page.vm.canReset
                            Accessible.description: qsTr("Opens the reset menu")
                            onClicked: resetMenu.popup(resetButton, 0, resetButton.height)

                            Menu {
                                id: resetMenu

                                MenuItem {
                                    objectName: "resetGo"
                                    text: qsTr("Reset")
                                    onTriggered: page.vm.reset(false)
                                }

                                MenuItem {
                                    objectName: "resetHalt"
                                    text: qsTr("Reset and halt")
                                    onTriggered: page.vm.reset(true)
                                }
                            }
                        }

                        HToolButton {
                            objectName: "reconnectButton"
                            iconName: "reconnect"
                            text: qsTr("Reconnect")
                            shortcutId: "device.reconnect"
                            compact: page.compact
                            enabled: page.vm.canReconnect
                            onClicked: page.vm.reconnect()
                        }

                        HToolButton {
                            objectName: "batchButton"
                            iconName: "batch"
                            text: qsTr("Batch…")
                            shortcutId: "console.batch"
                            compact: page.compact
                            enabled: page.vm.canRunBatch
                            onClicked: batchDialog.open()
                        }

                        HToolButton {
                            id: clearButton

                            objectName: "clearButton"
                            iconName: "clear"
                            text: qsTr("Clear")
                            shortcutId: "console.clear"
                            compact: page.compact
                            Accessible.description: qsTr("Opens the clear menu")
                            onClicked: clearMenu.popup(clearButton, 0, clearButton.height)

                            Menu {
                                id: clearMenu

                                MenuItem {
                                    text: qsTr("Shell")
                                    onTriggered: page.vm.clear(1)
                                }

                                MenuItem {
                                    text: qsTr("Log")
                                    onTriggered: page.vm.clear(2)
                                }

                                MenuItem {
                                    text: qsTr("Both")
                                    onTriggered: page.vm.clear(3)
                                }
                            }
                        }

                        HToolButton {
                            objectName: "saveButton"
                            iconName: "save"
                            text: qsTr("Save view…")
                            shortcutId: "console.save"
                            compact: page.compact
                            onClicked: saveDialog.openFor(page.focusedChannel)
                        }

                        // "Batch 12 / 40 [Stop]"
                        Rectangle {
                            objectName: "batchChip"
                            visible: page.vm.batchRunning
                            implicitWidth: batchRow.implicitWidth + 2 * Theme.spacingS
                            implicitHeight: Theme.toolButtonSize
                            radius: Theme.radiusS
                            color: Theme.accentSoft

                            RowLayout {
                                id: batchRow

                                anchors.centerIn: parent
                                spacing: Theme.spacingXs

                                Label {
                                    text: qsTr("Batch %1 / %2").arg(page.vm.batchDone).arg(page.vm.batchTotal)
                                    font: Theme.fontUiSmall
                                    color: Theme.text
                                }

                                HToolButton {
                                    objectName: "batchStop"
                                    iconName: "stop"
                                    text: qsTr("Stop the batch")
                                    display: AbstractButton.IconOnly
                                    onClicked: page.vm.stopBatch()
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        HToolButton {
                            objectName: "followButton"
                            iconName: "follow"
                            text: qsTr("Follow")
                            shortcutId: "console.follow"
                            compact: page.compact
                            checked: page.vm.following
                            Accessible.checkable: true
                            Accessible.checked: page.vm.following
                            onClicked: page.vm.following = !page.vm.following
                        }

                        HToolButton {
                            objectName: "pauseButton"
                            iconName: page.vm.paused ? "resume" : "pause"
                            text: qsTr("Pause")
                            shortcutId: "console.pause"
                            compact: page.compact
                            checked: page.vm.paused
                            Accessible.checkable: true
                            Accessible.checked: page.vm.paused
                            onClicked: page.vm.paused = !page.vm.paused
                        }

                        HToolButton {
                            id: moreButton

                            objectName: "consoleMore"
                            iconName: "more"
                            text: qsTr("More")
                            display: AbstractButton.IconOnly
                            onClicked: moreMenu.popup(moreButton, moreButton.width - moreMenu.width, moreButton.height)

                            Menu {
                                id: moreMenu

                                MenuItem {
                                    text: qsTr("Open log file")
                                    onTriggered: page.vm.openLogFile()
                                }

                                MenuItem {
                                    text: qsTr("Open log folder")
                                    onTriggered: page.vm.openLogFolder()
                                }

                                MenuSeparator {}

                                MenuItem {
                                    text: qsTr("Show timestamps")
                                    checkable: true
                                    checked: page.vm.settings.showTimestamps
                                    onTriggered: page.vm.settings.showTimestamps = !page.vm.settings.showTimestamps
                                }

                                MenuItem {
                                    text: qsTr("Wrap lines")
                                    checkable: true
                                    checked: page.vm.settings.wrapLines
                                    onTriggered: page.vm.settings.wrapLines = !page.vm.settings.wrapLines
                                }

                                MenuItem {
                                    text: qsTr("Swap panes")
                                    checkable: true
                                    checked: page.vm.swapPanes
                                    onTriggered: page.vm.swapPanes = !page.vm.swapPanes
                                }

                                MenuItem {
                                    text: qsTr("Stacked layout")
                                    checkable: true
                                    checked: page.vm.settings.stacked
                                    onTriggered: page.vm.settings.stacked = !page.vm.settings.stacked
                                }
                            }
                        }
                    }
                }

                Banners {
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    viewModel: page.vm
                }

                ErrorPanel {
                    objectName: "consoleError"
                    Layout.fillWidth: true
                    Layout.margins: Theme.spacingM
                    visible: page.vm.error.valid
                    error: page.vm.error
                    compact: Window.window !== null && Window.window.height < 700
                    // Reconnect and Open Settings act; for the console's other errors the action is Dismiss.
                    primaryAction: page.vm.error.primaryAction === ErrorAction.Reconnect
                                   || page.vm.error.primaryAction === ErrorAction.OpenSettings
                                   ? page.vm.error.primaryAction : ErrorAction.Dismiss
                    onActionTriggered: action => {
                        page.vm.dismissError();
                        if (action === ErrorAction.Reconnect)
                            page.vm.reconnect();
                        else if (action === ErrorAction.OpenSettings)
                            AppViewModel.openGlobalPage("settings");
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    EmptyState {
                        objectName: "notConnectedState"
                        anchors.centerIn: parent
                        visible: page.vm.showConnectState
                        iconName: "console"
                        headline: qsTr("Not connected")
                        text: qsTr("Connect to see the device console.")
                        actionText: qsTr("Connect")
                        onActionTriggered: page.vm.connectDevice()
                    }

                    // ---- split (ADR-0061): ratio of the first pane, stacked below 1000 px ----
                    Item {
                        id: split

                        readonly property bool stacked: page.vm.stackedLayout
                        readonly property int handleSize: 6
                        property real dragRatio: -1
                        readonly property real ratio: (split.dragRatio >= 0 ? split.dragRatio : page.vm.settings.splitRatio) / 100
                        readonly property real extent: (split.stacked ? split.height : split.width) - split.handleSize
                        readonly property real firstExtent: Math.round(split.extent * split.ratio)

                        objectName: "consoleSplit"
                        anchors.fill: parent
                        visible: !page.vm.showConnectState

                        Item {
                            id: firstSlot

                            width: split.stacked ? split.width : split.firstExtent
                            height: split.stacked ? split.firstExtent : split.height
                        }

                        Item {
                            id: secondSlot

                            x: split.stacked ? 0 : split.firstExtent + split.handleSize
                            y: split.stacked ? split.firstExtent + split.handleSize : 0
                            width: split.stacked ? split.width : split.extent - split.firstExtent
                            height: split.stacked ? split.extent - split.firstExtent : split.height
                        }

                        Rectangle {
                            id: handle

                            objectName: "splitHandle"
                            x: split.stacked ? 0 : split.firstExtent
                            y: split.stacked ? split.firstExtent : 0
                            width: split.stacked ? split.width : split.handleSize
                            height: split.stacked ? split.handleSize : split.height
                            color: handleArea.containsMouse || handleArea.pressed || handle.activeFocus ? Theme.accentSoft : Theme.border
                            activeFocusOnTab: true
                            Accessible.role: Accessible.Separator
                            Accessible.name: qsTr("Resize the shell and the log")
                            Keys.onLeftPressed: page.vm.settings.splitRatio = page.vm.settings.splitRatio - 5
                            Keys.onRightPressed: page.vm.settings.splitRatio = page.vm.settings.splitRatio + 5
                            Keys.onUpPressed: page.vm.settings.splitRatio = page.vm.settings.splitRatio - 5
                            Keys.onDownPressed: page.vm.settings.splitRatio = page.vm.settings.splitRatio + 5

                            // 2 px accent focus ring (design system § 6; review K11).
                            Rectangle {
                                objectName: "splitHandleFocusRing"
                                anchors.fill: parent
                                anchors.margins: -Theme.focusRingWidth
                                color: "transparent"
                                border.width: Theme.focusRingWidth
                                border.color: Theme.accent
                                visible: handle.activeFocus
                                z: 10
                            }

                            MouseArea {
                                id: handleArea

                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: split.stacked ? Qt.SplitVCursor : Qt.SplitHCursor
                                onPositionChanged: mouse => {
                                    if (!pressed)
                                        return;
                                    const point = mapToItem(split, mouse.x, mouse.y);
                                    const position = split.stacked ? point.y : point.x;
                                    split.dragRatio = Math.max(20, Math.min(80, position / split.extent * 100));
                                }
                                onReleased: {
                                    if (split.dragRatio >= 0)
                                        page.vm.settings.splitRatio = Math.round(split.dragRatio);
                                    split.dragRatio = -1;
                                }
                            }
                        }

                        // Shell pane: view, Search-only bar (Ctrl+F in the shell), command input.
                        FocusScope {
                            id: shellPane

                            objectName: "shellPane"
                            parent: page.vm.swapPanes ? secondSlot : firstSlot
                            anchors.fill: parent
                            onActiveFocusChanged: if (activeFocus) page.focusedChannel = 0

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0

                                ConsoleView {
                                    id: shellView

                                    objectName: "shellView"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    model: page.vm.shell
                                    viewModel: page.vm
                                    title: qsTr("Shell")
                                    onCopyRequested: page.vm.copySelection(0)
                                    onEscapePressed: {
                                        page.shellSearchVisible = false;
                                        page.vm.shell.findText = "";
                                    }
                                    onContextMenuRequested: (item, x, y) => {
                                        rowMenu.channel = 0;
                                        rowMenu.popup(item, x, y);
                                    }
                                }

                                ConsoleSearchBar {
                                    id: shellBar

                                    objectName: "shellSearchBar"
                                    Layout.fillWidth: true
                                    visible: page.shellSearchVisible
                                    model: page.vm.shell
                                    shellVariant: true
                                    onEscaped: {
                                        page.shellSearchVisible = false;
                                        shellInput.field.forceActiveFocus();
                                    }
                                }

                                ShellInput {
                                    id: shellInput

                                    Layout.fillWidth: true
                                    viewModel: page.vm
                                }
                            }
                        }

                        // Log pane: view and the Search | Filter bar.
                        FocusScope {
                            id: logPane

                            objectName: "logPane"
                            parent: page.vm.swapPanes ? firstSlot : secondSlot
                            anchors.fill: parent
                            onActiveFocusChanged: if (activeFocus) page.focusedChannel = 1

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0

                                ConsoleView {
                                    id: logView

                                    objectName: "logView"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    model: page.vm.log
                                    viewModel: page.vm
                                    title: qsTr("Log")
                                    showLevels: true
                                    onCopyRequested: page.vm.copySelection(1)
                                    // The second Esc (after the search bar's) clears the filter.
                                    onEscapePressed: page.vm.log.clearFilter()
                                    onContextMenuRequested: (item, x, y) => {
                                        rowMenu.channel = 1;
                                        rowMenu.popup(item, x, y);
                                    }
                                }

                                ConsoleSearchBar {
                                    id: logBar

                                    objectName: "logSearchBar"
                                    Layout.fillWidth: true
                                    model: page.vm.log
                                    onEscaped: logView.list.forceActiveFocus()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
