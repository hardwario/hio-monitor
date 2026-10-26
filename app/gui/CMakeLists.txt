# app/gui - the hio-monitor desktop application (Qt Quick, ADR-0003, ADR-0018;
# plan/features/0004-m3-gui-shell.md).
#
# hio_gui        static library with the QML module "HardwarioMonitor" (shell QML, components,
#                view models, workers), the embedded translations, fonts (ADR-0048, Git LFS) and
#                icons (ADR-0060); not a backend library (it uses QtQuick), so the ADR-0004 rule
#                does not apply to it.
# hio-monitor    the executable: src/main.cpp, links the module's static plugin.
# tests/         tst_gui (Qt Quick Test) and hio_gui_tests (GoogleTest view model tests).

find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Network Qml Quick QuickControls2 QuickTest Test)

set(HIO_GUI_QML_FILES
    qml/AlreadyRunningDialog.qml
    qml/Main.qml
    qml/Shortcuts.qml
    qml/Theme.qml
    qml/components/CheckList.qml
    qml/components/CommandBlock.qml
    qml/components/ConfirmDialog.qml
    qml/components/EmptyState.qml
    qml/components/ErrorPanel.qml
    qml/components/FocusRing.qml
    qml/components/HButton.qml
    qml/components/HToolButton.qml
    qml/components/Icon.qml
    qml/components/Illustration.qml
    qml/components/PhaseStepper.qml
    qml/components/ShortcutOverlay.qml
    qml/components/StateDot.qml
    qml/components/StepSpinBox.qml
    qml/components/Toast.qml
    qml/components/WarningBanner.qml
    qml/components/WizardDialog.qml
    qml/components/WizardStepList.qml
    qml/console/Banners.qml
    qml/console/ConsoleRow.qml
    qml/console/ConsoleSearchBar.qml
    qml/console/ConsoleView.qml
    qml/console/HistoryPopup.qml
    qml/console/ShellInput.qml
    qml/dialogs/ReportDialog.qml
    qml/flash/FlashBar.qml
    qml/flash/FlashCard.qml
    qml/flash/KindCards.qml
    qml/flash/MaintenanceSection.qml
    qml/flash/OptionsCard.qml
    qml/flash/ProgressCard.qml
    qml/flash/ResultCard.qml
    qml/flash/SourceCard.qml
    qml/pages/ChooseDevicePage.qml
    qml/pages/ConsolePage.qml
    qml/pages/FlashPage.qml
    qml/pages/PlaceholderPage.qml
    qml/pages/SettingsPage.qml
    qml/settings/AboutSection.qml
    qml/settings/GeneralSection.qml
    qml/settings/JLinkSection.qml
    qml/settings/NrfUtilSection.qml
    qml/settings/SettingsRow.qml
    qml/shell/AdvancedPopover.qml
    qml/shell/DeviceBar.qml
    qml/shell/DeviceTabBar.qml
    qml/shell/HelpMenu.qml
    qml/shell/NavRail.qml
    qml/shell/PageHost.qml
    qml/settings/ConsoleSection.qml
    qml/shell/TabView.qml)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

file(GLOB HIO_GUI_ICONS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/*.svg")
# Wizard illustration slots (ADR-0076, Git LFS): <slot>[-dark].svg / .png.
file(GLOB HIO_GUI_ILLUSTRATIONS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/wizard/*.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/wizard/*.png")

qt_add_library(hio_gui STATIC)
hio_apply_target_defaults(hio_gui)
# The output directory ends in the module's target path so that qmllint and qmlls find it.
qt_add_qml_module(hio_gui
    URI HardwarioMonitor
    VERSION 1.0
    OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/qml/HardwarioMonitor"
    QML_FILES ${HIO_GUI_QML_FILES}
    RESOURCES ${HIO_GUI_ICONS} ${HIO_GUI_ILLUSTRATIONS}
    SOURCES
        src/fonts.cpp
        src/fonts.h
        src/gui_log.cpp
        src/gui_log.h
        src/language_manager.cpp
        src/language_manager.h
        src/pages/builtin_pages.cpp
        src/pages/builtin_pages.h
        src/pages/console_page.cpp
        src/pages/console_page.h
        src/pages/firmware_services.cpp
        src/pages/firmware_services.h
        src/pages/flash_settings_pages.cpp
        src/pages/flash_settings_pages.h
        src/pages/placeholder_view_model.cpp
        src/pages/placeholder_view_model.h
        src/settings_store.cpp
        src/settings_store.h
        src/single_instance.cpp
        src/single_instance.h
        src/system_preferences.cpp
        src/system_preferences.h
        src/viewmodels/app_view_model.cpp
        src/viewmodels/app_view_model.h
        src/viewmodels/connection_view_model.cpp
        src/viewmodels/connection_view_model.h
        src/viewmodels/console_line_model.cpp
        src/viewmodels/console_line_model.h
        src/viewmodels/console_settings.cpp
        src/viewmodels/console_settings.h
        src/viewmodels/console_view_model.cpp
        src/viewmodels/console_view_model.h
        src/viewmodels/dependency_status.cpp
        src/viewmodels/dependency_status.h
        src/viewmodels/device_tab_list_model.cpp
        src/viewmodels/device_tab_list_model.h
        src/viewmodels/device_tab_view_model.cpp
        src/viewmodels/device_tab_view_model.h
        src/viewmodels/error_info.cpp
        src/viewmodels/error_info.h
        src/viewmodels/find_filter_engine.cpp
        src/viewmodels/find_filter_engine.h
        src/viewmodels/firmware_source_view_model.cpp
        src/viewmodels/firmware_source_view_model.h
        src/viewmodels/flash_view_model.cpp
        src/viewmodels/flash_view_model.h
        src/viewmodels/history_store.cpp
        src/viewmodels/history_store.h
        src/viewmodels/page_list_model.cpp
        src/viewmodels/page_list_model.h
        src/viewmodels/page_registry.cpp
        src/viewmodels/page_registry.h
        src/viewmodels/probe_picker_view_model.cpp
        src/viewmodels/probe_picker_view_model.h
        src/viewmodels/recent_sources_model.cpp
        src/viewmodels/recent_sources_model.h
        src/viewmodels/settings_view_model.cpp
        src/viewmodels/settings_view_model.h
        src/viewmodels/shortcut_registry.cpp
        src/viewmodels/shortcut_registry.h
        src/wizard/check_list_model.cpp
        src/wizard/check_list_model.h
        src/wizard/report_view_model.cpp
        src/wizard/report_view_model.h
        src/wizard/wizard.cpp
        src/wizard/wizard.h
        src/wizard/wizard_launcher.cpp
        src/wizard/wizard_launcher.h
        src/wizard/wizard_registry.cpp
        src/wizard/wizard_registry.h
        src/wizard/wizard_view_model.cpp
        src/wizard/wizard_view_model.h
        src/workers/batch_runner.cpp
        src/workers/batch_runner.h
        src/workers/diag_worker.cpp
        src/workers/diag_worker.h
        src/workers/firmware_loader.cpp
        src/workers/firmware_loader.h
        src/workers/probe_scanner.cpp
        src/workers/probe_scanner.h
    OUTPUT_TARGETS _hio_gui_qml_targets)
target_include_directories(hio_gui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# The generated QML type registration includes the view model headers by file name.
target_include_directories(hio_gui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pages"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodels"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wizard"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/workers")
target_link_libraries(hio_gui PUBLIC
    hio::core
    hio::device
    hio::diag
    hio::firmware
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2)
if(WIN32)
    target_compile_definitions(hio_gui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ADR-0044: Material, Dense variant (read by QQuickStyle from :/qtquickcontrols2.conf).
# ADR-0048: the bundled fonts (Git LFS) under :/fonts.
qt_add_resources(hio_gui "hio_gui_style"
    PREFIX "/"
    FILES qtquickcontrols2.conf
    OUTPUT_TARGETS _hio_gui_style_targets)
qt_add_resources(hio_gui "hio_gui_fonts"
    PREFIX "/fonts"
    BASE "assets/fonts"
    FILES
        assets/fonts/Inter-Medium.ttf
        assets/fonts/Inter-Regular.ttf
        assets/fonts/Inter-SemiBold.ttf
        assets/fonts/JetBrainsMono-Bold.ttf
        assets/fonts/JetBrainsMono-Regular.ttf
    OUTPUT_TARGETS _hio_gui_font_targets)
# Generated only: the qt_add_qml_module resource init objects and the two resources above.
hio_skip_linting(${_hio_gui_qml_targets} ${_hio_gui_style_targets} ${_hio_gui_font_targets})

qt_add_executable(hio-monitor src/main.cpp)
hio_apply_target_defaults(hio-monitor)
target_link_libraries(hio-monitor PRIVATE hio_gui hio_guiplugin)
set_target_properties(hio-monitor PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.hardwario.monitor"
    MACOSX_BUNDLE_BUNDLE_NAME "HARDWARIO Monitor"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    # ADR-0086: CMake's default keys + NSBluetoothAlwaysUsageDescription.
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
    # Qt is linked dynamically (ADR-0002), so Qt's own QML plugins are loaded at runtime and the
    # static-import scan is not needed; our own module's plugin is linked and imported explicitly.
    QT_QML_MODULE_NO_IMPORT_SCAN ON)

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
