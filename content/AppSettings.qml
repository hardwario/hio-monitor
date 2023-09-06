pragma Singleton
import QtQuick 2.15

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
    property string attachIcon: "../resources/icons/attach.png"
    property string btIcon: "../resources/icons/bt.png"
    property string btDisconnectIcon: "../resources/icons/btdisconnect.png"
    property string btDiscoverIcon: "../resources/icons/btdiscover.png"
    property string clearIcon: "../resources/icons/clear.png"
    property string cliIcon: "../resources/icons/cli.png"
    property string detachIcon: "../resources/icons/detach.png"
    property string flashIcon: "../resources/icons/flash.png"
    property string resumeIcon: "../resources/icons/resume.png"
    property string searchIcon: "../resources/icons/search.png"
    property string pauseIcon: "../resources/icons/pause.png"
    property string selectIcon: "../resources/icons/select.png"
    property string filterIcon: "../resources/icons/filter.png"
    property string sendIcon: "../resources/icons/send.png"
    property string lineBreakIcon: "../resources/icons/wall.png"
    property string hwNoTitleIcon: "../resources/icons/hwNoTitle.png"
    property string hwTitleIcon: "../resources/icons/hwTitle.png"
    property string signalIcon: "../resources/icons/signal.png"
    property string weakSignalIcon: "../resources/icons/weaksignal.png"
    property string okaySignalIcon: "../resources/icons/okaysignal.png"
    property string goodSignalIcon: "../resources/icons/goodsignal.png"
    property string bestSignalIcon: "../resources/icons/bestsignal.png"
    property string undoIcon: "../resources/icons/undo.png"
    property string folderIcon: "../resources/icons/folder.png"
    property string startIcon: "../resources/icons/start.png"
    property string catalogIcon: "../resources/icons/catalog.png"
    property string batchIcon: "../resources/icons/batch.png"

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
}
