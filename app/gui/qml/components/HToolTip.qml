// A tooltip whose text is always plain text (ADR-0184 amendment 2026-11-02, review R7): the
// Material ToolTip's contentItem is a Text with the default AutoText format, so a device or file
// string that looks like HTML would be rendered as rich text. Every tooltip of the app is an
// HToolTip declared inside its control (never the attached ToolTip.text, which uses the style's
// shared ToolTip); qml_plaintext_check enforces it.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ToolTip {
    id: tip

    // Plan 0028 (ADR-0203): long texts wrap at this content width (design system: 360 px).
    property real maxContentWidth: 360

    width: Math.min(tip.implicitWidth, tip.maxContentWidth + tip.leftPadding + tip.rightPadding)

    contentItem: Text {
        text: tip.text
        textFormat: Text.PlainText
        font: tip.font
        wrapMode: Text.Wrap
        color: tip.Material.foreground
    }
}
