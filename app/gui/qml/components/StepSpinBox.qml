// Editable spin box whose +/− (buttons with auto-repeat, Up/Down keys, mouse wheel) snap to the
// step grid: 1 → 10 → 20 up, 15 → 10 down (review S15); typed values are kept exactly.
//
// The box never assigns `value` itself (review S16): a step emits stepped(newValue) and the owner
// writes its view model, so the owner's `value:` binding stays intact. Typed values arrive as
// valueModified() (the SpinBox's own C++ setter, which keeps bindings). The built-in step is off
// (stepSize 0); the grid arithmetic is AppViewModel.stepValue().
import QtQuick
import QtQuick.Controls
import HardwarioMonitor

SpinBox {
    id: box

    property int step: 10
    // Wheel deltas not yet worth a step (touchpads send small deltas; one step per 120, S20).
    property real wheelRest: 0

    // A step on the grid in `direction` (+1 / -1) was requested; the owner applies it.
    signal stepped(int newValue)

    function stepBy(direction: int) {
        box.stepped(AppViewModel.stepValue(box.value, box.step, direction, box.from, box.to));
    }

    function wheel(delta: real) {
        box.wheelRest += delta;
        while (Math.abs(box.wheelRest) >= 120) {
            box.stepBy(box.wheelRest > 0 ? 1 : -1);
            box.wheelRest -= box.wheelRest > 0 ? 120 : -120;
        }
    }

    stepSize: 0
    editable: true
    wheelEnabled: false
    Keys.onUpPressed: box.stepBy(1)
    Keys.onDownPressed: box.stepBy(-1)

    Connections {
        target: box.up

        function onPressedChanged() {
            if (box.up.pressed)
                box.stepBy(1);
        }
    }

    Connections {
        target: box.down

        function onPressedChanged() {
            if (box.down.pressed)
                box.stepBy(-1);
        }
    }

    // Auto-repeat while +/− is held, like Qt's SpinBox (300 ms delay, then every 100 ms; S20).
    Timer {
        id: repeat

        objectName: "stepRepeat"
        interval: 300
        repeat: true
        running: box.up.pressed || box.down.pressed
        onRunningChanged: repeat.interval = 300
        onTriggered: {
            repeat.interval = 100;
            box.stepBy(box.up.pressed ? 1 : -1);
        }
    }

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => box.wheel(event.angleDelta.y)
    }

    FocusRing {
        control: box
        radiusOffset: Theme.radiusS
    }
}
