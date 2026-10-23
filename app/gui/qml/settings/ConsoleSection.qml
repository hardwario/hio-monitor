// The Console section of the Settings page (plan/features/0005-m3-console.md, C5b), registered with
// PageRegistry::addSettingsSection and rendered by the Settings page (plan 0006): font size,
// timestamps, line wrapping, console log files, the log folder and the retention of the files;
// amended by 0021 (ADR-0170): command suggestions while typing and reading the command list on
// connect.
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
        objectName: "consoleRetention"
        Layout.fillWidth: true
        text: qsTr("Keeps the last %1 files, max. %2 MB.").arg(ConsoleSettings.retentionFiles).arg(ConsoleSettings.retentionMegabytes)
        font: Theme.fontUiSmall
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }
}
