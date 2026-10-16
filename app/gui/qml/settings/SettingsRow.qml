// One row of a Settings section: a label (with an optional muted description below it) in a fixed
// column on the left, the control(s) on the right. Labels wrap (Czech texts are longer).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

RowLayout {
    id: row

    property string label: ""
    property string description: ""
    default property alias controls: controlArea.data

    spacing: Theme.spacingL

    ColumnLayout {
        Layout.preferredWidth: 200
        Layout.minimumWidth: 200
        Layout.maximumWidth: 200
        Layout.alignment: Qt.AlignTop
        spacing: 2

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: row.label
            color: Theme.text
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            visible: row.description !== ""
            text: row.description
            font: Theme.fontUiSmall
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }

    ColumnLayout {
        id: controlArea

        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        spacing: Theme.spacingXs
    }
}
