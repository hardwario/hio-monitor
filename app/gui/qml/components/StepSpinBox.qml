// Editable spin box whose +/− (buttons, Up/Down keys, mouse wheel) snap to the step grid:
// 1 → 10 → 20 up, 15 → 10 down (review S15); typed values are kept exactly. The built-in step is
// off (stepSize 0); the steps go through AppViewModel.stepValue().
import QtQuick
import QtQuick.Controls
import HardwarioMonitor

SpinBox {
    id: box

    property int step: 10

    // One step on the grid in `direction` (+1 / -1), then valueModified() as a built-in step.
    function stepBy(direction: int) {
        box.value = AppViewModel.stepValue(box.value, box.step, direction, box.from, box.to);
        box.valueModified();
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

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => box.stepBy(event.angleDelta.y > 0 ? 1 : -1)
    }

    FocusRing {
        control: box
        radiusOffset: Theme.radiusS
    }
}
