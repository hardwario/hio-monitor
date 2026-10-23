// Flash step 1 (ADR-0055; amended by 0021, ADR-0174): "What do you want to flash?" - two radio
// cards (application firmware, LTE firmware incl. the modem), stacked below 1000 px window width;
// prerequisite problems (nRF Util) and the connector warning (ADR-0057 Phase 0) appear inline below
// the cards.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

FlashCard {
    id: card

    required property FlashViewModel vm

    number: 1
    title: qsTr("What do you want to flash?")

    GridLayout {
        Layout.fillWidth: true
        columns: AppViewModel.windowWidth < 1000 ? 1 : 2
        columnSpacing: Theme.spacingS
        rowSpacing: Theme.spacingS
        Accessible.role: Accessible.Grouping
        Accessible.name: card.title

        Repeater {
            model: card.vm ? card.vm.kinds : []

            delegate: AbstractButton {
                id: kindCard

                required property var modelData

                objectName: "kindCard" + kindCard.modelData.kind
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                // Not checkable: `checked` follows the view model only (a click on the selected
                // card must not un-check it).
                checkable: false
                checked: card.vm ? card.vm.kind === kindCard.modelData.kind : false
                focusPolicy: Qt.StrongFocus
                padding: Theme.spacingM
                Accessible.role: Accessible.RadioButton
                Accessible.name: kindCard.modelData.title
                Accessible.description: kindCard.modelData.mcu + ", " + kindCard.modelData.accepts
                Accessible.checked: kindCard.checked
                onClicked: card.vm.kind = kindCard.modelData.kind

                background: Rectangle {
                    radius: Theme.radiusM
                    color: kindCard.checked ? Theme.accentSoft : Theme.bg
                    border.width: kindCard.checked ? 2 : 1
                    border.color: kindCard.checked ? Theme.accent : Theme.border

                    FocusRing {
                        control: kindCard
                    }
                }

                contentItem: RowLayout {
                    spacing: Theme.spacingS

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                        implicitWidth: 16
                        implicitHeight: 16
                        radius: 8
                        color: Theme.bg
                        border.width: 2
                        border.color: kindCard.checked ? Theme.accent : Theme.textMuted

                        Rectangle {
                            anchors.centerIn: parent
                            visible: kindCard.checked
                            width: 8
                            height: 8
                            radius: 4
                            color: Theme.accent
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            Layout.fillWidth: true
                            text: kindCard.modelData.title
                            font: Theme.fontUiStrong
                            color: Theme.text
                            wrapMode: Text.Wrap
                        }

                        Label {
                            Layout.fillWidth: true
                            text: kindCard.modelData.mcu
                            font: Theme.fontUiSmall
                            color: Theme.textMuted
                            wrapMode: Text.Wrap
                        }

                        Label {
                            Layout.fillWidth: true
                            text: kindCard.modelData.accepts
                            font: Theme.fontMonoSmall
                            color: Theme.textMuted
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }

    // Prerequisite (nRF Util), shown as soon as the kind is chosen.
    Rectangle {
        objectName: "prerequisite"
        Layout.fillWidth: true
        visible: card.vm ? card.vm.prerequisiteText !== "" : false
        implicitHeight: prerequisiteRow.implicitHeight + 2 * Theme.spacingS
        radius: Theme.radiusS
        color: Theme.errorPanelBg
        Accessible.role: Accessible.AlertMessage
        Accessible.name: card.vm ? card.vm.prerequisiteText : ""

        RowLayout {
            id: prerequisiteRow

            anchors.fill: parent
            anchors.margins: Theme.spacingS
            spacing: Theme.spacingS

            Icon {
                symbol: "error"
                color: Theme.error
            }

            Label {
                Layout.fillWidth: true
                text: card.vm ? card.vm.prerequisiteText : ""
                color: Theme.text
                wrapMode: Text.Wrap
            }

            HButton {
                objectName: "prerequisiteSettings"
                text: qsTr("Set path in Settings")
                onClicked: AppViewModel.openSettings("nrfutil")
            }

            HButton {
                kind: "ghost"
                text: qsTr("How to install ↗")
                onClicked: Qt.openUrlExternally(card.vm.nrfUtilInstallUrl)
            }
        }
    }

    // ADR-0057 Phase 0: the other MCU is not reachable through the same connector (CHESTER-M).
    Rectangle {
        objectName: "connectorWarning"
        Layout.fillWidth: true
        visible: card.vm ? card.vm.connectorWarning !== "" : false
        implicitHeight: warningRow.implicitHeight + 2 * Theme.spacingS
        radius: Theme.radiusS
        color: Theme.warningPanelBg
        Accessible.role: Accessible.AlertMessage
        Accessible.name: card.vm ? card.vm.connectorWarning : ""

        RowLayout {
            id: warningRow

            anchors.fill: parent
            anchors.margins: Theme.spacingS
            spacing: Theme.spacingS

            Icon {
                Layout.alignment: Qt.AlignTop
                symbol: "warning"
                color: Theme.warning
            }

            Label {
                Layout.fillWidth: true
                text: card.vm ? card.vm.connectorWarning : ""
                color: Theme.text
                wrapMode: Text.Wrap
            }
        }
    }
}
