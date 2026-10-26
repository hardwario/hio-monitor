// "HARDWARIO Monitor quit unexpectedly last time." (plan/features/0015 B3, ADR-0123): a non-modal
// notice in the shell banner area after a crash; [Create diagnostic report] opens the report
// dialog (the report includes the crash files), [Dismiss] hides it for good. Both acknowledge the
// crash. Data and actions: AppViewModel.crashedLastRun / reportCrash() / dismissCrashNotice().
import QtQuick
import HardwarioMonitor

WarningBanner {
    objectName: "crashNotice"
    visible: AppViewModel.crashedLastRun
    text: qsTr("HARDWARIO Monitor quit unexpectedly last time.")
    actionText: qsTr("Create diagnostic report")
    onActionTriggered: AppViewModel.reportCrash()
    onDismissed: AppViewModel.dismissCrashNotice()
}
