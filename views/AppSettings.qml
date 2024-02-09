pragma Singleton

import QtQuick

// AppSettings holds all the app settings/resources values.
// It meant to be instatiated in the main window to be accessible globally.
QtObject {

    property string hardwarioCatalogAppWebUrl: "https://docs.hardwario.com/chester/catalog-applications/catalog-applications/#application-firmware"
    property string hardwarioWebUrl: "https://www.hardwario.com/"
    property int maxViewLines: 100000

    // page names
    property string consoleWelcomeName: "WCondole"
    property string bluetoothWelcomeName: "WBluetooth"
    property string consoleName: "Condole"
    property string bluetoothName: "Bluetooth"
    property string flashName: "Flash"

    // Icons
    property string attachIcon: "qrc:/icons/attach"
    property string btIcon: "qrc:/icons/bt-icon"
    property string btDisconnectIcon: "qrc:/icons/bt-disconnect"
    property string btDiscoverIcon: "qrc:/icons/bt-discover"
    property string clearIcon: "qrc:/icons/clear"
    property string cliIcon: "qrc:/icons/cli"
    property string detachIcon: "qrc:/icons/detach"
    property string flashIcon: "qrc:/icons/flash"
    property string resumeIcon: "qrc:/icons/resume"
    property string searchIcon: "qrc:/icons/search"
    property string pauseIcon: "qrc:/icons/pause"
    property string selectIcon: "qrc:/icons/select"
    property string filterIcon: "qrc:/icons/filter"
    property string sendIcon: "qrc:/icons/send"
    property string lineBreakIcon: "qrc:/icons/wall"
    property string hwNoTitleIcon: "qrc:/icons/hw-no-title"
    property string hwTitleIcon: "qrc:/icons/hw-title"
    property string signalIcon: "qrc:/icons/signal-empty"
    property string weakSignalIcon: "qrc:/icons/signal-weak"
    property string okaySignalIcon: "qrc:/icons/signal-okay"
    property string goodSignalIcon: "qrc:/icons/signal-good"
    property string bestSignalIcon: "qrc:/icons/signal-best"
    property string undoIcon: "qrc:/icons/undo"
    property string folderIcon: "qrc:/icons/folder"
    property string startIcon: "qrc:/icons/start"
    property string catalogIcon: "qrc:/icons/catalog"
    property string batchIcon: "qrc:/icons/batch"
    property string openIcon: "qrc:/icons/open"

    // Colors
    property string hoverColor: "#424242"
    property string grayColor: "#6B6A6A"
    property string whiteColor: "#D1D5DA"
    property string darkColor: "#252532"
    property string primaryColor: "#F9826C"
    property string greenColor: "#85E89D"
    property string redColor: "#F97583"
    property string borderColor: "#2F363D"
    property string focusColor: "#79B8FF"
    property string wrnColor: "#FFAB70"
    property string clickIndicatorColor: "#808080"
}
