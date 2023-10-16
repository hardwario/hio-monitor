import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.15
import hiomon 1.0

ApplicationWindow {
    id: appWindow
    visible: true
    width: 1024
    height: 680
    minimumWidth: 1024
    minimumHeight: 680
    title: "HARDWARIO Monitor " + APP_VERSION

    onActiveFocusItemChanged: console.log("activeFocusItem", activeFocusItem)

    Material.theme: Material.Dark
    Material.background: AppSettings.darkColor
    Material.accent: AppSettings.primaryColor

    FontLoader {
        id: textFont
        source: "../resources/fonts/UbuntuMono-Regular.ttf"
    }

    FontLoader {
        id: labelFont
        source: "../resources/fonts/Inter-Regular.ttf"
    }

    AppNotification {
        id: notify
    }

    RowLayout {
        anchors.fill: parent

        PagePanel {
            id: pagePanel
            Layout.fillHeight: true
            Layout.preferredWidth: 80
        }

        StackView {
            id: stackView
            Layout.fillHeight: true
            Layout.fillWidth: true
            padding: 0
            spacing: 0
            LoadingIndicator {
                id: loadingIndicator
                anchors.centerIn: stackView
            }
        }

        ToolPanel {
            id: toolPanel
            Layout.fillHeight: true
            Layout.preferredWidth: 80
        }
    }
}
