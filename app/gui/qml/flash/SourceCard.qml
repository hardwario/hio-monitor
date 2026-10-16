// Flash step 2: the firmware file (drop zone, Browse…), the catalog ID (not for modem firmware;
// ADR-0054), the recent list filtered to the kind, the summary, the kind hint (ADR-0040) and load
// errors. Binds to a FirmwareSourceViewModel (also used by the flash wizard, 0010).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

FlashCard {
    id: card

    required property FirmwareSourceViewModel source
    property errorInfo noError // default-constructed: invalid (no error)
    readonly property bool busy: card.source !== null
                                 && (card.source.sourceState === FirmwareSourceViewModel.Loading
                                     || card.source.sourceState === FirmwareSourceViewModel.Downloading)

    signal browseRequested

    number: 2
    title: qsTr("Firmware file")

    // Drop zone (the whole page accepts drops) and Browse…
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: dropRow.implicitHeight + 2 * Theme.spacingM
        radius: Theme.radiusM
        color: Theme.surface
        border.color: Theme.border

        RowLayout {
            id: dropRow

            anchors.fill: parent
            anchors.margins: Theme.spacingM
            spacing: Theme.spacingM

            Icon {
                symbol: "flash"
                color: Theme.textMuted
            }

            Label {
                objectName: "dropText"
                Layout.fillWidth: true
                text: card.source ? card.source.dropText : ""
                color: Theme.textMuted
                wrapMode: Text.Wrap
            }

            HButton {
                objectName: "browseButton"
                text: qsTr("Browse…")
                toolTipText: qsTr("Choose a firmware file (%1)").arg(AppViewModel.shortcuts.sequenceText("flash.browse"))
                onClicked: card.browseRequested()
            }
        }
    }

    // Catalog ID (ADR-0054).
    ColumnLayout {
        Layout.fillWidth: true
        visible: card.source ? card.source.catalogAvailable : false
        spacing: Theme.spacingXs

        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingS

            Label {
                height: Theme.controlHeight
                verticalAlignment: Text.AlignVCenter
                text: qsTr("or firmware ID from the catalog")
                color: Theme.textMuted
            }

            TextField {
                id: idField

                objectName: "catalogIdField"
                width: 320
                height: Theme.controlHeight
                font: Theme.fontMono
                placeholderText: qsTr("e.g. %1").arg("3f2a9c1b…")
                maximumLength: 64
                Accessible.name: qsTr("Firmware ID from the catalog")
                onTextEdited: card.source.checkCatalogId(idField.text)
                onAccepted: card.source.loadCatalogId(idField.text)
            }

            HButton {
                objectName: "downloadButton"
                text: qsTr("Download")
                enabled: idField.text.trim() !== ""
                onClicked: card.source.loadCatalogId(idField.text)
            }

            HButton {
                kind: "ghost"
                text: qsTr("Open firmware catalog ↗")
                onClicked: Qt.openUrlExternally(card.source.catalogUrl)
            }
        }

        Label {
            objectName: "catalogIdError"
            Layout.fillWidth: true
            visible: card.source ? card.source.catalogIdError !== "" : false
            text: card.source ? card.source.catalogIdError : ""
            font: Theme.fontUiSmall
            color: Theme.error
            wrapMode: Text.Wrap
        }
    }

    // Loading / downloading.
    ColumnLayout {
        Layout.fillWidth: true
        visible: card.busy
        spacing: Theme.spacingXs

        Label {
            Layout.fillWidth: true
            text: card.source ? card.source.sourceSummary : ""
            color: Theme.text
            elide: Text.ElideMiddle
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingS

            ProgressBar {
                objectName: "downloadProgress"
                Layout.fillWidth: true
                from: 0
                to: 1
                value: card.source ? Math.max(0, card.source.downloadProgress) : 0
                indeterminate: card.source ? (card.source.sourceState === FirmwareSourceViewModel.Loading
                                              || card.source.downloadProgress < 0) : false
            }

            HButton {
                visible: card.source ? card.source.sourceState === FirmwareSourceViewModel.Downloading : false
                text: qsTr("Cancel")
                onClicked: card.source.cancelDownload()
            }
        }
    }

    // Summary of the loaded file.
    RowLayout {
        objectName: "sourceSummary"
        Layout.fillWidth: true
        visible: card.source ? card.source.sourceState === FirmwareSourceViewModel.Ready : false
        spacing: Theme.spacingS

        Icon {
            symbol: "success"
            color: Theme.success
        }

        Label {
            objectName: "summaryText"
            Layout.fillWidth: true
            text: card.source ? card.source.sourceSummary : ""
            color: Theme.text
            wrapMode: Text.WrapAnywhere
        }

        HToolButton {
            objectName: "clearSource"
            iconName: "close"
            text: qsTr("Remove the file")
            display: AbstractButton.IconOnly
            onClicked: card.source.clear()
        }
    }

    // A file of another kind: hint + [Switch] (the kind never changes silently, ADR-0040).
    Rectangle {
        objectName: "kindHint"
        Layout.fillWidth: true
        visible: card.source ? card.source.kindHint !== "" : false
        implicitHeight: hintRow.implicitHeight + 2 * Theme.spacingS
        radius: Theme.radiusS
        color: Theme.warningPanelBg
        Accessible.role: Accessible.AlertMessage
        Accessible.name: card.source ? card.source.kindHint : ""

        RowLayout {
            id: hintRow

            anchors.fill: parent
            anchors.margins: Theme.spacingS
            spacing: Theme.spacingS

            Icon {
                symbol: "info"
                color: Theme.warning
            }

            Label {
                Layout.fillWidth: true
                text: card.source ? card.source.sourceName + " — " + card.source.kindHint : ""
                color: Theme.text
                wrapMode: Text.Wrap
            }

            HButton {
                objectName: "switchKind"
                text: qsTr("Switch")
                onClicked: card.source.acceptSuggestedKind()
            }
        }
    }

    // A load or download error (Firmware category: "Choose another file").
    ErrorPanel {
        objectName: "sourceError"
        Layout.fillWidth: true
        visible: card.source ? card.source.error.valid : false
        error: card.source ? card.source.error : card.noError
        onActionTriggered: action => action === ErrorAction.ChooseFile ? card.browseRequested()
                                                                        : card.source.clear()
    }

    // Modem update note (modem firmware and LTE bundles).
    RowLayout {
        Layout.fillWidth: true
        visible: card.source ? card.source.modemNoteVisible : false
        spacing: Theme.spacingS

        Icon {
            Layout.alignment: Qt.AlignTop
            symbol: "info"
            color: Theme.textMuted
            size: 18
        }

        Label {
            objectName: "modemNote"
            Layout.fillWidth: true
            text: qsTr("Modem update takes 1–3 minutes. The console is disconnected meanwhile.")
            font: Theme.fontUiSmall
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
    }

    // Recent sources of this kind.
    ColumnLayout {
        Layout.fillWidth: true
        visible: card.source ? card.source.recent.count > 0 : false
        spacing: 0

        Label {
            text: qsTr("Recent")
            font: Theme.fontUiSmall
            color: Theme.textMuted
            Accessible.role: Accessible.Heading
        }

        Repeater {
            model: card.source ? card.source.recent : []

            delegate: ItemDelegate {
                id: recentRow

                required property int index
                required property string name
                required property string detail
                required property string typeText
                required property string dateText

                objectName: "recentRow" + recentRow.index
                Layout.fillWidth: true
                padding: Theme.spacingXs
                Accessible.name: recentRow.name + ", " + recentRow.typeText + ", " + recentRow.dateText
                ToolTip.visible: recentRow.hovered
                ToolTip.text: recentRow.detail
                ToolTip.delay: 600
                onClicked: card.source.loadRecent(recentRow.index)

                contentItem: RowLayout {
                    spacing: Theme.spacingM

                    Label {
                        Layout.fillWidth: true
                        text: recentRow.name
                        font: Theme.fontMonoSmall
                        color: Theme.text
                        elide: Text.ElideMiddle
                    }

                    Label {
                        text: recentRow.typeText
                        font: Theme.fontUiSmall
                        color: Theme.textMuted
                    }

                    Label {
                        text: recentRow.dateText
                        font: Theme.fontUiSmall
                        color: Theme.textMuted
                    }

                    HToolButton {
                        iconName: "close"
                        text: qsTr("Remove from the recent list")
                        display: AbstractButton.IconOnly
                        onClicked: card.source.removeRecent(recentRow.index)
                    }
                }
            }
        }
    }
}
