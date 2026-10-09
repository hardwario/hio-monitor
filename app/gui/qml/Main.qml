// Main window of HARDWARIO Monitor (M0 skeleton). Presentation only (ADR-0003): all data and
// actions come from AppViewModel.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HardwarioMonitor

ApplicationWindow {
    id: window

    width: 1024
    height: 700
    visible: true
    title: qsTr("HARDWARIO Monitor")

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16

        Label {
            objectName: "headline"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("HARDWARIO Monitor %1 — nothing to see yet").arg(AppViewModel.version)
            font.pixelSize: 24
        }

        Label {
            objectName: "buildInfo"
            Layout.alignment: Qt.AlignHCenter
            text: AppViewModel.buildInfo
            font.pixelSize: 12
            opacity: 0.7
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8

            Label {
                text: qsTr("Language:")
            }

            ComboBox {
                id: languageSelector

                objectName: "languageSelector"
                model: AppViewModel.availableLanguages.map(
                           language => language.nativeName !== "" ? language.nativeName
                                                                  : qsTr("System"))
                currentIndex: AppViewModel.availableLanguages.findIndex(
                                  language => language.code === AppViewModel.language)
                onActivated: index => AppViewModel.setLanguage(
                                 AppViewModel.availableLanguages[index].code)
            }
        }
    }
}
