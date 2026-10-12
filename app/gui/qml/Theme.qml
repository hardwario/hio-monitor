// Design tokens of HARDWARIO Monitor (plan/gui/design-system.md, ADR-0044, ADR-0047, ADR-0048).
// Components never hard-code colours, sizes or fonts; they read them here. The theme follows
// AppViewModel.theme ("system" | "light" | "dark") and, for "system", the OS colour scheme.
pragma Singleton

import QtQuick
import HardwarioMonitor

QtObject {
    readonly property bool dark: AppViewModel.theme === "dark"
                                 || (AppViewModel.theme === "system"
                                     && Application.styleHints.colorScheme === Qt.ColorScheme.Dark)

    // ---- colours (design system § 1) ----
    readonly property color bg: dark ? "#1E1F26" : "#FFFFFF"
    readonly property color surface: dark ? "#262833" : "#F5F6F8"
    readonly property color surfaceRaised: dark ? "#2E3040" : "#FFFFFF"
    readonly property color consoleBg: dark ? "#191A20" : "#FBFBFC"
    readonly property color border: dark ? "#3A3D4D" : "#D5D9DF"
    readonly property color text: dark ? "#E6E8EE" : "#1B1F24"
    readonly property color textMuted: dark ? "#9BA3B4" : "#57606A"
    readonly property color accent: dark ? "#79B8FF" : "#0B5FCC"
    readonly property color accentSoft: dark ? "#24344A" : "#E3EEFC"
    readonly property color onAccent: dark ? "#0B1A2E" : "#FFFFFF"
    readonly property color success: dark ? "#56D364" : "#1A7F37"
    readonly property color warning: dark ? "#FFAB70" : "#8A5A00"
    readonly property color error: dark ? "#FF7B86" : "#C4232E"
    readonly property color logDebug: dark ? "#8B93A6" : "#636C76"
    readonly property color logInfo: text
    readonly property color shellEcho: accent
    readonly property color selection: dark ? "#264F78" : "#CCE0FF"
    readonly property color findMatch: dark ? "#6B5418" : "#FFE08A"
    readonly property color findCurrent: dark ? "#B07A10" : "#FFB224"
    readonly property color errorPanelBg: dark ? "#3A2228" : "#FFF1F1"
    readonly property color warningPanelBg: dark ? "#3A3020" : "#FFF6E0"
    readonly property color scrim: dark ? "#80000000" : "#6B000000"

    // ---- typography (§ 2) ----
    readonly property string fontUiFamily: "Inter"
    readonly property string fontMonoFamily: "JetBrains Mono"
    readonly property font fontUi: Qt.font({ family: fontUiFamily, pixelSize: 13, weight: Font.Normal })
    readonly property font fontUiSmall: Qt.font({ family: fontUiFamily, pixelSize: 12, weight: Font.Normal })
    readonly property font fontUiStrong: Qt.font({ family: fontUiFamily, pixelSize: 13, weight: Font.DemiBold })
    readonly property font fontTitle: Qt.font({ family: fontUiFamily, pixelSize: 18, weight: Font.DemiBold })
    readonly property font fontMono: Qt.font({ family: fontMonoFamily, pixelSize: 13, weight: Font.Normal })
    readonly property font fontMonoSmall: Qt.font({ family: fontMonoFamily, pixelSize: 12, weight: Font.Normal })

    // ---- spacing, sizes, shape (§ 3) ----
    readonly property int spacingXs: 4
    readonly property int spacingS: 8
    readonly property int spacingM: 12
    readonly property int spacingL: 16
    readonly property int spacingXl: 24
    readonly property int spacingXxl: 32
    readonly property int controlHeight: 32
    readonly property int toolButtonSize: 28
    readonly property int iconSize: 20
    readonly property int railItemSize: 56
    readonly property int railWidth: 72
    readonly property int railWidthCompact: 52
    readonly property int tabHeight: 36
    readonly property int deviceBarHeight: 48
    readonly property int radiusS: 4
    readonly property int radiusM: 6
    readonly property int radiusL: 10
    readonly property int focusRingWidth: 2

    // ---- motion (§ 6): ≤ 150 ms, none when the OS asks for reduced motion ----
    readonly property int animationMs: AppViewModel.reducedMotion ? 0 : 150

    // Icons (ADR-0060): app/gui/assets/icons/<name>.svg in the module's resources.
    function iconUrl(name: string): url {
        return name === "" ? "" : Qt.resolvedUrl("../assets/icons/" + name + ".svg");
    }
}
