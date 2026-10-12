// One device tab: device bar + page area (ADR-0043).
import QtQuick
import QtQuick.Layouts
import HardwarioMonitor

ColumnLayout {
    id: view

    required property DeviceTabViewModel tab
    required property int index
    // The model clears `tab` when the row is removed, before the delegate is destroyed; the
    // children keep the last tab until then.
    property DeviceTabViewModel stableTab: null

    spacing: 0

    Binding on stableTab {
        value: view.tab
        when: view.tab !== null
        restoreMode: Binding.RestoreNone
    }

    DeviceBar {
        Layout.fillWidth: true
        visible: view.stableTab !== null
        tab: view.stableTab
    }

    PageHost {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: view.stableTab !== null
        tab: view.stableTab
    }
}
