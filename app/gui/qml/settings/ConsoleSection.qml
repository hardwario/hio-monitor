// The Console section of the Settings page (plan/features/0005-m3-console.md, C5b), registered with
// PageRegistry::addSettingsSection and rendered by the Settings page (plan 0006): font size,
// timestamps, line wrapping, console log files, the log folder and the retention of the files;
// amended by 0021 (ADR-0170): command suggestions while typing and reading the command list on
// connect; by 0025 (ADR-0186): "Remember commands" and "Clear history".
// Binds to ConsoleSettings (the console/* keys of the SettingsStore).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: section

    objectName: "consoleSection"
    spacing: Theme.spacingS

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingM

        Label {
            Layout.fillWidth: true
            text: qsTr("Font size")
            color: Theme.text
            wrapMode: Text.Wrap
        }

        SpinBox {
            id: fontBox

            objectName: "consoleFontSize"
            from: ConsoleSettings.minFontSize
            to: ConsoleSettings.maxFontSize
            value: ConsoleSettings.fontSize
            editable: true
            Accessible.name: qsTr("Font size")
            onValueModified: ConsoleSettings.fontSize = fontBox.value
        }
    }

    Switch {
        objectName: "consoleTimestamps"
        Layout.fillWidth: true
        text: qsTr("Show timestamps")
        checked: ConsoleSettings.showTimestamps
        onToggled: ConsoleSettings.showTimestamps = checked
    }

    Switch {
        objectName: "consoleWrap"
        Layout.fillWidth: true
        text: qsTr("Wrap long lines")
        checked: ConsoleSettings.wrapLines
        onToggled: ConsoleSettings.wrapLines = checked
    }

    Switch {
        objectName: "consoleSuggestions"
        Layout.fillWidth: true
        text: qsTr("Suggest commands while typing")
        checked: ConsoleSettings.suggestions
        onToggled: ConsoleSettings.suggestions = checked
    }

    Switch {
        objectName: "consoleReadCommands"
        Layout.fillWidth: true
        text: qsTr("Read the command list from the device on connect (sends “help”)")
        checked: ConsoleSettings.readCommandsOnConnect
        onToggled: ConsoleSettings.readCommandsOnConnect = checked
    }

    // 0025 (ADR-0186): recording the command history; "Clear history" deletes it for all devices.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingM

        Switch {
            objectName: "consoleRememberCommands"
            Layout.fillWidth: true
            text: qsTr("Remember commands")
            checked: ConsoleSettings.rememberCommands
            onToggled: ConsoleSettings.rememberCommands = checked
        }

        HButton {
            objectName: "consoleClearHistory"
            text: qsTr("Clear history")
            onClicked: clearHistoryDialog.open()
        }
    }

    // Review R8: why "Clear history" failed (e.g. a file is locked by another program).
    ErrorPanel {
        objectName: "consoleClearHistoryError"
        Layout.fillWidth: true
        visible: ConsoleSettings.historyError.valid
        error: ConsoleSettings.historyError
        primaryAction: ErrorAction.Dismiss
        onActionTriggered: ConsoleSettings.dismissHistoryError()
    }

    ConfirmDialog {
        id: clearHistoryDialog

        objectName: "consoleClearHistoryConfirm"
        title: qsTr("Clear history")
        body: qsTr("Delete the saved command history of all devices?")
        acceptText: qsTr("Clear history")
        onAccepted: ConsoleSettings.clearHistory()
    }

    Switch {
        objectName: "consoleLogToFile"
        Layout.fillWidth: true
        text: qsTr("Save console to log files")
        checked: ConsoleSettings.logToFile
        onToggled: ConsoleSettings.logToFile = checked
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingM

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                text: qsTr("Log folder")
                color: Theme.text
            }

            Label {
                textFormat: Text.PlainText
                objectName: "consoleLogFolder"
                Layout.fillWidth: true
                text: ConsoleSettings.logFolder
                font: Theme.fontMonoSmall
                color: Theme.textMuted
                elide: Text.ElideMiddle
            }
        }

        HButton {
            objectName: "consoleOpenFolder"
            text: qsTr("Open")
            onClicked: ConsoleSettings.openLogFolder()
        }
    }

    Label {
        textFormat: Text.PlainText
        objectName: "consoleRetention"
        Layout.fillWidth: true
        text: qsTr("Keeps the last %1 files, max. %2 MB.").arg(ConsoleSettings.retentionFiles).arg(ConsoleSettings.retentionMegabytes)
        font: Theme.fontUiSmall
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }
}
