// The connection state dot (design system § 5): grey idle, amber connecting / busy, green
// connected, red error. Colour is never the only signal: a text label always accompanies it.
import QtQuick
import HardwarioMonitor

Rectangle {
    id: dot

    property string kind: "idle" // idle | connecting | reconnecting | connected | busy | error

    width: 8
    height: 8
    radius: 4
    color: dot.kind === "connected" ? Theme.success
         : dot.kind === "error" ? Theme.error
         : dot.kind === "connecting" || dot.kind === "reconnecting" || dot.kind === "busy" ? Theme.warning
         : Theme.textMuted
    Accessible.ignored: true
}
