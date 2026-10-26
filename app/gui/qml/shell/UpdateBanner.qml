// "HARDWARIO Monitor 2.1.0 is available (you have 2.0.3)." [Download] [Skip this version] [✕]
// (plan/features/0016 C2, ADR-0116): a non-modal notice in the shell banner area after an automatic
// check found a newer version. [Download] opens the release page in the browser (the app keeps
// running), [Skip this version] never announces that version again, [✕] (Later) hides it until the
// next automatic check. Data and actions: AppViewModel.update.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: banner

    readonly property UpdateViewModel vm: AppViewModel.update

    objectName: "updateBanner"
    implicitHeight: row.implicitHeight + 2 * Theme.spacingS
    color: Theme.accentSoft
    Accessible.role: Accessible.AlertMessage
    Accessible.name: banner.vm.bannerText

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.topMargin: Theme.spacingS
        anchors.bottomMargin: Theme.spacingS
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingS

        Icon {
            symbol: "up"
            color: Theme.accent
        }

        Label {
            objectName: "updateBannerText"
            Layout.fillWidth: true
            text: banner.vm.bannerText
            color: Theme.text
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        HButton {
            objectName: "updateDownload"
            text: qsTr("Download")
            onClicked: banner.vm.download()
        }

        HButton {
            objectName: "updateSkip"
            kind: "ghost"
            text: qsTr("Skip this version")
            onClicked: banner.vm.skip()
        }

        HToolButton {
            objectName: "updateLater"
            iconName: "close"
            text: qsTr("Later")
            display: AbstractButton.IconOnly
            onClicked: banner.vm.dismiss()
        }
    }
}
