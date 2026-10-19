#include "pages/console_page.h"

#include "gui_log.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/console_settings.h"
#include "viewmodels/console_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/page_registry.h"
#include "viewmodels/shortcut_registry.h"

#include <QKeySequence>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtCore/qttranslation.h>

#include <array>

namespace hio::gui {

namespace {

const QString consolePageId = QStringLiteral("console");

void registerShortcuts(ShortcutRegistry& registry) {
    struct Shortcut {
        const char* id;
        const char* keys;
        const char* description;
    };
    // ADR-0051. Keys handled inside a view or the input (End, Ctrl+A, Ctrl+C, Ctrl+R) are listed
    // for the overlay and the tooltips; the page creates QML Shortcuts for the others.
    static constexpr std::array shortcuts{
        Shortcut{.id = "console.find",
                 .keys = "Ctrl+F",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Search")},
        Shortcut{.id = "console.findNext",
                 .keys = "F3",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Next match")},
        Shortcut{.id = "console.findPrevious",
                 .keys = "Shift+F3",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Previous match")},
        Shortcut{.id = "console.filter",
                 .keys = "Ctrl+L",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Filter the log")},
        Shortcut{.id = "console.clear",
                 .keys = "Ctrl+K",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Clear the focused view")},
        Shortcut{.id = "console.save",
                 .keys = "Ctrl+S",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Save the focused view")},
        Shortcut{.id = "console.pause",
                 .keys = "Ctrl+P",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Pause or resume the view")},
        Shortcut{.id = "console.follow",
                 .keys = "End",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Follow new lines")},
        Shortcut{.id = "console.batch",
                 .keys = "Ctrl+B",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Run a batch file")},
        Shortcut{.id = "console.focusInput",
                 .keys = "Ctrl+E",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Focus the command input")},
        Shortcut{.id = "console.history",
                 .keys = "Ctrl+R",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Search the command history")},
        Shortcut{.id = "console.selectAll",
                 .keys = "Ctrl+A",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Select all lines")},
        Shortcut{.id = "console.copy",
                 .keys = "Ctrl+C",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Copy the selected lines")},
        Shortcut{.id = "console.fontLarger",
                 .keys = "Ctrl+=",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Console font size")},
        Shortcut{.id = "console.fontSmaller",
                 .keys = "Ctrl+-",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Console font size")},
        Shortcut{.id = "console.fontReset",
                 .keys = "Ctrl+0",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Console font size")},
    };
    for (const Shortcut& shortcut : shortcuts) {
        if (const Status added = registry.add(QString::fromLatin1(shortcut.id),
                                              QKeySequence(QString::fromLatin1(shortcut.keys)),
                                              consolePageId, shortcut.description);
            !added) {
            qCWarning(lcGui) << "Console shortcut" << shortcut.id
                             << "not registered:" << toQString(added.error().detail);
        }
    }
}

} // namespace

void registerConsolePage(AppViewModel& app) {
    ConsoleSettings::registerKeys(*app.settingsStore());
    // Qt parent ownership: AppViewModel (found by ConsoleSettings::create for QML).
    auto* settings = new ConsoleSettings( // NOLINT(cppcoreguidelines-owning-memory)
        app.settingsStore(), app.sessionRegistry().options().consoleDirectory, &app);
    HistoryStore* history = &app.historyStore();

    app.pageRegistry().add(PageDescriptor{
        .id = consolePageId,
        .titleSourceText = QT_TRANSLATE_NOOP("Pages", "Console"),
        .icon = QStringLiteral("console"),
        .qml = QUrl(QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/pages/ConsolePage.qml")),
        .order = 10,
        .global = false,
        .createViewModel = [settings, history,
                            appModel = &app](DeviceTabViewModel* tab) -> QObject* {
            // Qt parent ownership: the tab.
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            return new ConsoleViewModel(
                tab, ConsoleContext{.settings = settings, .history = history, .app = appModel});
        }});
    app.pageRegistry().addSettingsSection(SettingsSectionDescriptor{
        .id = consolePageId,
        .titleSourceText = QT_TRANSLATE_NOOP("Pages", "Console"),
        .qml = QUrl(QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/settings/ConsoleSection.qml")),
        .order = 20});
    registerShortcuts(*app.shortcuts());
}

} // namespace hio::gui
