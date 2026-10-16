// The console banners (design system § 5, WarningBanner): dropped lines and the paused view.
// Session warnings (J-Link firmware outdated with [Details], console file not written) are shown
// by the shell's banner above every page (PageHost, plan 0004 as amended by 0005 on 2026-10-16),
// not repeated here.
import QtQuick
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: banners

    required property ConsoleViewModel viewModel

    spacing: 0

    WarningBanner {
        objectName: "droppedBanner"
        Layout.fillWidth: true
        visible: banners.viewModel.droppedLines > 0
        text: qsTr("%Ln lines were dropped because the device sent data faster than it could be displayed.", "",
                   Math.min(banners.viewModel.droppedLines, 2147483647))
        onDismissed: banners.viewModel.dismissDropped()
    }

    WarningBanner {
        objectName: "pausedBanner"
        Layout.fillWidth: true
        visible: banners.viewModel.paused
        text: qsTr("The view is paused. %Ln new lines are not shown yet.", "",
                   banners.viewModel.shell.pendingLines + banners.viewModel.log.pendingLines)
        actionText: qsTr("Resume")
        onActionTriggered: banners.viewModel.paused = false
        onDismissed: banners.viewModel.paused = false
    }
}
