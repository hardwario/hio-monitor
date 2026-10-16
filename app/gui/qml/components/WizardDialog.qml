// The wizard dialog (plan/features/0007, ADR-0066, ADR-0067): modal, centred,
// min(720, window − 48) × min(560, window − 48). Header: the title, the step list (compact below
// 800 px window width) and ×. Content: the current step rendered by its kind (Info, Checks,
// Choice, Embedded, Commands, Done). Footer: Back (left; hidden on the first step and while busy),
// the step's actions, the default button and Cancel. Enter = default button, Esc = Cancel (asks
// only while the wizard runs an operation). Presentation only: AppViewModel.wizard.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

Dialog {
    id: dialog

    readonly property WizardViewModel wizard: AppViewModel.wizard
    readonly property var step: dialog.wizard.step
    readonly property int windowWidth: parent ? parent.width : 960
    readonly property int windowHeight: parent ? parent.height : 600
    readonly property bool compact: dialog.windowWidth < 800

    objectName: "wizardDialog"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(720, dialog.windowWidth - 48)
    height: Math.min(560, dialog.windowHeight - 48)
    padding: Theme.spacingXl
    topPadding: Theme.spacingL
    closePolicy: Popup.NoAutoClose
    visible: dialog.wizard.open
    // The dialog's accessible name (Popup uses its title).
    title: dialog.wizard.title
    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusL
        border.color: Theme.border
    }

    function focusHeading() {
        heading.forceActiveFocus();
        if (dialog.wizard.stepAnnouncement !== "")
            heading.Accessible.announce(dialog.wizard.stepAnnouncement);
    }

    onOpened: dialog.focusHeading()

    Connections {
        target: dialog.wizard

        function onCancelConfirmationNeeded(text: string) {
            cancelConfirm.body = text;
            cancelConfirm.open();
        }

        function onRaiseRequested() {
            dialog.focusHeading();
        }
    }

    // A new step: focus moves to its heading and the step is announced.
    property string stepId: dialog.step.id !== undefined ? dialog.step.id : ""
    onStepIdChanged: if (dialog.opened) dialog.focusHeading()

    header: ColumnLayout {
        spacing: Theme.spacingS

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingXl
            Layout.rightMargin: Theme.spacingM
            Layout.topMargin: Theme.spacingL
            spacing: Theme.spacingS

            Label {
                objectName: "wizardTitle"
                Layout.fillWidth: true
                text: dialog.wizard.title
                font: Theme.fontTitle
                color: Theme.text
                elide: Text.ElideRight
            }

            HToolButton {
                objectName: "wizardClose"
                iconName: "close"
                text: qsTr("Cancel")
                display: AbstractButton.IconOnly
                onClicked: dialog.wizard.cancel(false)
            }
        }

        WizardStepList {
            objectName: "wizardStepList"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingXl
            Layout.rightMargin: Theme.spacingXl
            steps: dialog.wizard.steps
            compact: dialog.compact
            compactText: dialog.wizard.stepLabel
            visible: dialog.wizard.currentIndex >= 0
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
    }

    contentItem: ScrollView {
        id: scroll

        objectName: "wizardContent"
        clip: true
        contentWidth: availableWidth

        Keys.onReturnPressed: dialog.wizard.primary()
        Keys.onEnterPressed: dialog.wizard.primary()
        Keys.onEscapePressed: dialog.wizard.cancel(false)

        ColumnLayout {
            width: scroll.availableWidth
            spacing: Theme.spacingM

            Illustration {
                objectName: "wizardIllustration"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.min(480, scroll.availableWidth)
                Layout.preferredHeight: Layout.preferredWidth * 270 / 480
                visible: dialog.step.illustration !== undefined && dialog.step.illustration !== ""
                slot: dialog.step.illustration !== undefined ? dialog.step.illustration : ""
                caption: dialog.step.heading !== undefined ? dialog.step.heading : ""
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS

                Icon {
                    visible: dialog.step.kind === StepKind.Done
                    symbol: "success"
                    color: Theme.success
                    size: 28
                }

                Label {
                    id: heading

                    objectName: "wizardHeading"
                    Layout.fillWidth: true
                    text: dialog.step.heading !== undefined ? dialog.step.heading : ""
                    font: Theme.fontTitle
                    color: Theme.text
                    wrapMode: Text.Wrap
                    focusPolicy: Qt.StrongFocus
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: heading.text
                    Keys.onReturnPressed: dialog.wizard.primary()
                    Keys.onEnterPressed: dialog.wizard.primary()
                    Keys.onEscapePressed: dialog.wizard.cancel(false)
                }
            }

            Label {
                objectName: "wizardBody"
                Layout.fillWidth: true
                visible: text !== ""
                text: dialog.step.body !== undefined ? dialog.step.body : ""
                textFormat: Text.StyledText
                color: Theme.text
                wrapMode: Text.Wrap
            }

            HButton {
                objectName: "wizardLink"
                visible: dialog.step.linkText !== undefined && dialog.step.linkText !== ""
                kind: "ghost"
                text: dialog.step.linkText !== undefined ? dialog.step.linkText + " ↗" : ""
                onClicked: Qt.openUrlExternally(dialog.step.linkUrl)
            }

            CheckList {
                objectName: "wizardChecks"
                Layout.fillWidth: true
                visible: dialog.step.kind === StepKind.Checks
                model: dialog.wizard.checks
            }

            ColumnLayout {
                objectName: "wizardChoices"
                Layout.fillWidth: true
                visible: dialog.step.kind === StepKind.Choice
                spacing: Theme.spacingS

                Repeater {
                    model: dialog.step.kind === StepKind.Choice ? dialog.step.choices : []

                    delegate: AbstractButton {
                        id: card

                        required property var modelData

                        objectName: "wizardChoice"
                        Layout.fillWidth: true
                        checkable: true
                        checked: card.modelData.selected === true
                        focusPolicy: Qt.StrongFocus
                        implicitHeight: cardContent.implicitHeight + 2 * Theme.spacingM
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: card.modelData.title
                        Accessible.description: card.modelData.subtitle !== undefined ? card.modelData.subtitle : ""
                        onClicked: dialog.wizard.choose(card.modelData.id)

                        background: Rectangle {
                            radius: Theme.radiusM
                            color: card.checked ? Theme.accentSoft : Theme.bg
                            border.color: card.checked ? Theme.accent : Theme.border
                            border.width: card.checked ? 2 : 1

                            FocusRing {
                                control: card
                            }
                        }

                        contentItem: ColumnLayout {
                            id: cardContent

                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.spacingM
                                Layout.rightMargin: Theme.spacingM
                                text: card.modelData.title
                                font: Theme.fontUiStrong
                                color: Theme.text
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.spacingM
                                Layout.rightMargin: Theme.spacingM
                                visible: text !== ""
                                text: card.modelData.subtitle !== undefined ? card.modelData.subtitle : ""
                                color: Theme.textMuted
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }

            Loader {
                id: embeddedLoader

                objectName: "wizardEmbedded"
                Layout.fillWidth: true
                active: dialog.step.kind === StepKind.Embedded
                visible: embeddedLoader.active
                source: dialog.wizard.embeddedSource

                Binding {
                    target: embeddedLoader.item
                    property: "vm"
                    value: dialog.wizard.embedded
                    when: embeddedLoader.status === Loader.Ready
                }
            }

            CommandBlock {
                objectName: "wizardCommands"
                Layout.fillWidth: true
                visible: dialog.step.kind === StepKind.Commands
                commands: dialog.step.commands !== undefined ? dialog.step.commands : []
                onCopyRequested: text => dialog.wizard.copyText(text)
            }
        }
    }

    footer: RowLayout {
        spacing: Theme.spacingS

        HButton {
            objectName: "wizardBack"
            Layout.leftMargin: Theme.spacingXl
            Layout.bottomMargin: Theme.spacingL
            visible: dialog.wizard.canBack
            text: qsTr("Back")
            onClicked: dialog.wizard.back()
        }

        Item {
            Layout.fillWidth: true
        }

        Repeater {
            model: dialog.step.actions !== undefined ? dialog.step.actions : []

            delegate: HButton {
                required property var modelData

                objectName: "wizardAction"
                Layout.bottomMargin: Theme.spacingL
                text: modelData.text
                onClicked: dialog.wizard.action(modelData.id)
            }
        }

        HButton {
            objectName: "wizardPrimary"
            Layout.bottomMargin: Theme.spacingL
            visible: dialog.step.primaryText !== undefined && dialog.step.primaryText !== ""
            kind: "primary"
            enabled: dialog.wizard.canPrimary
            text: dialog.step.primaryText !== undefined ? dialog.step.primaryText : ""
            onClicked: dialog.wizard.primary()
        }

        HButton {
            objectName: "wizardCancel"
            Layout.rightMargin: Theme.spacingXl
            Layout.bottomMargin: Theme.spacingL
            text: dialog.step.kind === StepKind.Done ? qsTr("Close") : qsTr("Cancel")
            enabled: !dialog.wizard.closing
            onClicked: dialog.wizard.cancel(false)
        }
    }

    ConfirmDialog {
        id: cancelConfirm

        objectName: "wizardCancelConfirm"
        title: qsTr("Cancel the wizard?")
        acceptText: qsTr("Cancel and close")
        rejectText: qsTr("Keep going")
        onAccepted: dialog.wizard.cancel(true)
    }
}
