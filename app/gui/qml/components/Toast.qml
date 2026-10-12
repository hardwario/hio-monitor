// Success / info toasts (never errors, UX 0003): bottom centre, at most 3, 4 s each
// (AppViewModel.toasts). New toasts are announced to screen readers.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import HardwarioMonitor

Column {
    id: toasts

    spacing: Theme.spacingS

    Repeater {
        model: AppViewModel.toasts

        delegate: Rectangle {
            id: toast

            required property string modelData

            objectName: "toast"
            anchors.horizontalCenter: parent.horizontalCenter
            implicitWidth: Math.min(label.implicitWidth + 2 * Theme.spacingL, 560)
            implicitHeight: label.implicitHeight + 2 * Theme.spacingS
            radius: Theme.radiusM
            color: Theme.text
            opacity: 0.95
            Accessible.role: Accessible.StaticText
            Accessible.name: toast.modelData
            Component.onCompleted: Accessible.announce(toast.modelData)

            Label {
                id: label

                anchors.centerIn: parent
                width: Math.min(implicitWidth, 560 - 2 * Theme.spacingL)
                text: toast.modelData
                color: Theme.bg
                wrapMode: Text.Wrap
            }
        }
    }
}
