# app/gui - the hio-monitor desktop application (Qt Quick, ADR-0003, ADR-0018).
#
# hio_gui     static library with the QML module "HardwarioMonitor" (Main.qml, view models,
#             LanguageManager) and the embedded translations; not a backend library (it uses
#             QtQuick), so the ADR-0004 rule does not apply to it.
# hio-monitor the executable: src/main.cpp, links the module's static plugin.
# tst_gui     Qt Quick Test (QUICK_TEST_MAIN_WITH_SETUP) linking the same library.

find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Qml Quick QuickControls2 QuickTest)

qt_add_library(hio_gui STATIC)
hio_apply_target_defaults(hio_gui)
# The output directory ends in the module's target path so that qmllint and qmlls find it.
qt_add_qml_module(hio_gui
    URI HardwarioMonitor
    VERSION 1.0
    OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/qml/HardwarioMonitor"
    QML_FILES
        qml/Main.qml
    SOURCES
        src/gui_log.cpp
        src/gui_log.h
        src/language_manager.cpp
        src/language_manager.h
        src/viewmodels/app_view_model.cpp
        src/viewmodels/app_view_model.h)
target_include_directories(hio_gui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# The generated QML type registration includes the view model headers by file name.
target_include_directories(hio_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodels")
target_link_libraries(hio_gui PUBLIC
    hio::core
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2)

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
    # Qt is linked dynamically (ADR-0002), so Qt's own QML plugins are loaded at runtime and the
    # static-import scan is not needed; our own module's plugin is linked and imported explicitly.
    QT_QML_MODULE_NO_IMPORT_SCAN ON)

if(HIO_BUILD_TESTS)
    qt_add_executable(tst_gui tests/tst_gui.cpp)
    hio_apply_target_defaults(tst_gui)
    target_link_libraries(tst_gui PRIVATE hio_gui hio_guiplugin Qt6::QuickTest)
    target_compile_definitions(tst_gui PRIVATE
        "QUICK_TEST_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests\"")
    set_target_properties(tst_gui PROPERTIES
        WIN32_EXECUTABLE OFF
        MACOSX_BUNDLE OFF
        QT_QML_MODULE_NO_IMPORT_SCAN ON)
    add_test(NAME tst_gui COMMAND tst_gui)
    set_tests_properties(tst_gui PROPERTIES
        LABELS "qml"
        # No desktop input-method plugin (IBus over D-Bus) in tests: it starts uninstrumented Qt
        # threads that TSan reports. Older Qt reads QT_IM_MODULE, newer Qt QT_IM_MODULES.
        ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;QT_IM_MODULE=none;QT_IM_MODULES=none")
endif()
