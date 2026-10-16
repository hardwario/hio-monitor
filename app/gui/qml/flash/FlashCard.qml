// A card of the Flash page: an optional step number, a title and the content (design system § 5:
// flat with a 1 px border, radius 6).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Rectangle {
    id: card

    property string title: ""
    property int number: 0
    default property alias content: body.data

    implicitHeight: column.implicitHeight + 2 * Theme.spacingL
    color: Theme.bg
    radius: Theme.radiusM
    border.color: Theme.border

    ColumnLayout {
        id: column

        anchors.fill: parent
        anchors.margins: Theme.spacingL
        spacing: Theme.spacingM

        RowLayout {
            visible: card.title !== ""
            spacing: Theme.spacingS

            Rectangle {
                visible: card.number > 0
                implicitWidth: 22
                implicitHeight: 22
                radius: 11
                color: Theme.accentSoft

                Label {
                    anchors.centerIn: parent
                    text: card.number
                    font: Theme.fontUiStrong
                    color: Theme.accent
                }
            }

            Label {
                Layout.fillWidth: true
                text: card.title
                font: Theme.fontUiStrong
                color: Theme.text
                wrapMode: Text.Wrap
                Accessible.role: Accessible.Heading
                Accessible.name: card.title
            }
        }

        ColumnLayout {
            id: body

            Layout.fillWidth: true
            spacing: Theme.spacingM
        }
    }
}
