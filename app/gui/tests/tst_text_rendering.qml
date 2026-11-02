// Plan 0028 F3 (Phase C, C1): reproduction harness of the "odeš le" report (Settings → Console in
// Czech: a gap between "š" and "l", the "l" reddish). Renders the exact string, a Material Switch
// with it, and the Czech letters with a caron/acute before l, ť, ď, h in 12, 13 and 18 px, weights
// 400 and 600, with Text.QtRendering, Text.NativeRendering and Text.CurveRendering, light and dark.
// Measures the advance of "š" (and "šl" against "š" + "l") with FontMetrics and checks that the
// grabs of the grayscale render types have no coloured pixels.
//
// The grabs are written only when HWIO_TEXT_GRABS names a directory (developer machine, not
// committed); run it with QT_SCALE_FACTOR=1, 1.25, 1.5, 2 and on xcb / wayland to compare the device
// pixel ratios and platforms (the default ctest run is offscreen at 1.0). ADR-0205 regression
// check: with QSG_DISTANCEFIELD_ANTIALIASING=gray (what hwio-monitor's main() sets) the QtRendering
// and CurveRendering rows must have no coloured pixel on a real platform too.
import QtQuick
import QtQuick.Controls
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    readonly property string exact: "Načíst seznam příkazů ze zařízení po připojení (odešle „help“)"
    readonly property var samples: ["odešle", "šl ľl čl řl žl ďl ťl", "šť šď šh", "š l"]
    readonly property var renderTypes: [Text.QtRendering, Text.NativeRendering, Text.CurveRendering]
    readonly property var renderNames: ["qt", "native", "curve"]

    property var window: null

    function grabsDir() {
        return testSupport.textGrabsDir;
    }

    function initTestCase() {
        window = Qt.createQmlObject(`
            import QtQuick
            import QtQuick.Controls
            Window { width: 900; height: 200; visible: true; color: "black" }`, testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        if (window)
            window.destroy();
    }

    function test_glyphAdvances_data() {
        let rows = [];
        for (const size of [12, 13, 18]) {
            for (const weight of [Font.Normal, Font.DemiBold])
                rows.push({ tag: size + "px-" + weight, size: size, weight: weight });
        }
        return rows;
    }

    // The advance of "š" equals the one of "s" (Inter: the caron does not widen it) and "šl" is
    // "š" + "l" within 0.5 px (no kerning pair that opens a gap).
    function test_glyphAdvances(data) {
        const metrics = Qt.createQmlObject(`
            import QtQuick
            FontMetrics {}`, testCase);
        metrics.font = Qt.font({ family: Theme.fontUiFamily, pixelSize: data.size, weight: data.weight });
        const s = metrics.advanceWidth("s");
        const caron = metrics.advanceWidth("š");
        const l = metrics.advanceWidth("l");
        const pair = metrics.advanceWidth("šl");
        console.log(`${data.tag}: s=${s} š=${caron} l=${l} šl=${pair} es=${metrics.advanceWidth("e")} eš=${metrics.advanceWidth("eš")} ešl=${metrics.advanceWidth("ešl")} ešle=${metrics.advanceWidth("ešle")}`);
        verify(Math.abs(caron - s) < 0.5, `š ${caron} vs s ${s}`);
        verify(Math.abs(pair - (caron + l)) < 0.5, `šl ${pair} vs š + l ${caron + l}`);
        metrics.destroy();
    }

    function test_grabs_data() {
        let rows = [];
        for (let r = 0; r < renderTypes.length; ++r) {
            for (const dark of [false, true])
                rows.push({ tag: renderNames[r] + (dark ? "-dark" : "-light"), renderType: renderTypes[r], name: renderNames[r], dark: dark });
        }
        return rows;
    }

    function test_grabs(data) {
        window.color = data.dark ? "#1e1f22" : "#ffffff";
        const fg = data.dark ? "#e6e6e6" : "#1e1f22";
        let items = [];
        let y = 4;
        for (const size of [12, 13, 18]) {
            for (const weight of [Font.Normal, Font.DemiBold]) {
                for (const text of [exact].concat(samples)) {
                    const item = Qt.createQmlObject(`
                        import QtQuick
                        Text { textFormat: Text.PlainText }`, window.contentItem);
                    item.text = text;
                    item.font = Qt.font({ family: Theme.fontUiFamily, pixelSize: size, weight: weight });
                    item.renderType = data.renderType;
                    item.color = fg;
                    item.x = 4;
                    item.y = y;
                    y += item.implicitHeight + 2;
                    items.push(item);
                }
            }
        }
        // The control of the report: a Material Switch with the exact label.
        const control = Qt.createQmlObject(`
            import QtQuick
            import QtQuick.Controls
            import QtQuick.Controls.Material
            Switch { }`, window.contentItem);
        control.text = exact;
        control.Material.theme = data.dark ? Material.Dark : Material.Light;
        control.x = 4;
        control.y = y;
        y += control.implicitHeight;
        window.height = y + 4;
        tryCompare(window, "height", y + 4);
        waitForRendering(window.contentItem, 2000);
        wait(100);
        const image = grabImage(window.contentItem);
        verify(image.width > 0);
        if (grabsDir() !== "")
            image.save(grabsDir() + "/text-" + data.name + "-" + (data.dark ? "dark" : "light") + "-dpr" + Screen.devicePixelRatio + ".png");
        // Grayscale render types: no coloured fringe (|r - g|, |g - b| small on every pixel of the
        // Text rows; the Switch above uses the default render type).
        const textRows = Math.floor(control.y * image.height / window.height);
        let coloured = 0;
        for (let py = 0; py < textRows; py += 1) {
            for (let px = 0; px < image.width; px += 1) {
                const c = image.pixel(px, py);
                if (!c)
                    continue; // outside the grab (a window the screen clipped)
                if (Math.abs(c.r - c.g) > 0.12 || Math.abs(c.g - c.b) > 0.12)
                    ++coloured;
            }
        }
        console.log(`${data.tag} dpr ${Screen.devicePixelRatio}: ${coloured} coloured pixels in the text rows`);
        // ADR-0205: the product sets QSG_DISTANCEFIELD_ANTIALIASING=gray; offscreen has no subpixel AA.
        if (data.renderType !== Text.NativeRendering && (Qt.platform.pluginName === "offscreen" || testSupport.textAntialiasing === "gray"))
            verify(coloured === 0, `${coloured} coloured pixels`);
        for (const item of items)
            item.destroy();
        control.destroy();
    }

    name: "TextRendering"
    when: windowShown
}
