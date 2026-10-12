// A Material Symbols icon (ADR-0060) recoloured with a Theme colour.
import QtQuick
import QtQuick.Controls.impl
import HardwarioMonitor

IconImage {
    id: icon

    property string symbol: ""
    property int size: Theme.iconSize

    source: Theme.iconUrl(icon.symbol)
    sourceSize: Qt.size(icon.size, icon.size)
    width: icon.size
    height: icon.size
    color: Theme.text
    Accessible.ignored: true
}
