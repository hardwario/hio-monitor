import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
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
        source: "qrc:/fonts/ubuntu-mono"
    }

    FontLoader {
        id: labelFont
        source: "qrc:/fonts/inter"
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
