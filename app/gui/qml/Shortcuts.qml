// The global keyboard shortcuts (ADR-0051). The key sequences come from the ShortcutRegistry (the
// single source, checked for duplicates); the handlers call AppViewModel.
pragma ComponentBehavior: Bound

import QtQuick
import QtQml.Models
import HardwarioMonitor

Item {
    id: shortcuts

    readonly property ShortcutRegistry registry: AppViewModel.shortcuts

    Shortcut {
        sequence: shortcuts.registry.portableText("tab.new")
        onActivated: AppViewModel.newTab()
    }

    Shortcut {
        sequence: shortcuts.registry.portableText("tab.close")
        onActivated: AppViewModel.closeTab(AppViewModel.currentTab)
    }

    Shortcut {
        sequences: [shortcuts.registry.portableText("tab.next"), shortcuts.registry.portableText("tab.next.alt")]
        onActivated: AppViewModel.nextTab()
    }

    Shortcut {
        sequences: [shortcuts.registry.portableText("tab.previous"), shortcuts.registry.portableText("tab.previous.alt")]
        onActivated: AppViewModel.previousTab()
    }

    Shortcut {
        sequence: shortcuts.registry.portableText("settings.open")
        onActivated: AppViewModel.openGlobalPage("settings")
    }

    Shortcut {
        sequence: shortcuts.registry.portableText("device.toggle")
        onActivated: AppViewModel.toggleConnection()
    }

    Shortcut {
        sequence: shortcuts.registry.portableText("device.reconnect")
        onActivated: AppViewModel.reconnect()
    }

    Shortcut {
        sequences: [shortcuts.registry.portableText("help.shortcuts"), shortcuts.registry.portableText("help.shortcuts.alt")]
        onActivated: AppViewModel.shortcutOverlayVisible = true
    }

    // Ctrl+1, Ctrl+2, ...: the per-tab pages in rail order.
    Instantiator {
        model: AppViewModel.pages

        delegate: Shortcut {
            id: pageShortcut

            required property string pageId
            required property bool isGlobal

            enabled: !pageShortcut.isGlobal
            sequence: shortcuts.registry.portableText("page." + pageShortcut.pageId)
            onActivated: AppViewModel.showPage(pageShortcut.pageId)
        }
    }
}
