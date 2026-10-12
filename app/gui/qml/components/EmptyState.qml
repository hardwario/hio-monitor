// Empty state (UX principle 1, "show the next step"): icon, headline, one sentence, extra content
// (e.g. a check list) and the primary next-step button.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: empty

    property string iconName: ""
    property string headline: ""
    property string text: ""
    property string actionText: ""
    default property alias extraContent: extra.data

    signal actionTriggered

    spacing: Theme.spacingM

    Icon {
        Layout.alignment: Qt.AlignHCenter
        symbol: empty.iconName
        size: 40
        color: Theme.textMuted
    }

    Label {
        objectName: "emptyHeadline"
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 460
        horizontalAlignment: Text.AlignHCenter
        text: empty.headline
        font: Theme.fontTitle
        color: Theme.text
        wrapMode: Text.Wrap
    }

    Label {
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 460
        horizontalAlignment: Text.AlignHCenter
        visible: empty.text !== ""
        text: empty.text
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }

    ColumnLayout {
        id: extra

        Layout.alignment: Qt.AlignHCenter
        spacing: Theme.spacingS
    }

    HButton {
        objectName: "emptyAction"
        Layout.alignment: Qt.AlignHCenter
        visible: empty.actionText !== ""
        kind: "primary"
        text: empty.actionText
        onClicked: empty.actionTriggered()
    }
}
