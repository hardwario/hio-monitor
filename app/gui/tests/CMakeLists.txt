# Tests of app/gui (plan/features/0004-m3-gui-shell.md):
#   tst_gui        Qt Quick Test (tst_*.qml): components in light and dark, ErrorPanel, shell,
#                  shortcuts, layout at 960 x 600 and 1099 px in Czech; offscreen platform.
#   hio_gui_tests  GoogleTest view model tests against the fake J-Link library (tests/fake_jlink).
# Both run under ASan/UBSan and TSan via the presets.

set(HIO_GUI_TEST_SUPPORT
    support/fake_jlink_scenario.h
    support/test_page_view_model.cpp
    support/test_page_view_model.h)

qt_add_executable(tst_gui tst_gui.cpp ${HIO_GUI_TEST_SUPPORT})
hio_apply_target_defaults(tst_gui)
target_include_directories(tst_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tst_gui PRIVATE hio_gui hio_guiplugin Qt6::QuickTest Qt6::QuickControls2)
target_compile_definitions(tst_gui PRIVATE
    "QUICK_TEST_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
    "HIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hio_fake_jlink>\"")
add_dependencies(tst_gui hio_fake_jlink)
set_target_properties(tst_gui PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    AUTOMOC ON
    QT_QML_MODULE_NO_IMPORT_SCAN ON)
# One process per test file: every file creates its own main window and QML engine (several
# engines in one process leave QML binding memory behind that LeakSanitizer reports).
file(GLOB _hio_gui_qml_tests RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tst_*.qml")
foreach(_test IN LISTS _hio_gui_qml_tests)
    cmake_path(GET _test STEM _name)
    add_test(NAME "tst_gui.${_name}"
        COMMAND tst_gui -input "${CMAKE_CURRENT_SOURCE_DIR}/${_test}")
    set_tests_properties("tst_gui.${_name}" PROPERTIES
        LABELS "qml"
        # No desktop input-method plugin (IBus over D-Bus) in tests: it starts uninstrumented Qt
        # threads that TSan reports. Older Qt reads QT_IM_MODULE, newer Qt QT_IM_MODULES.
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none;QT_IM_MODULES=none;TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/sanitizers/tsan-qml.supp:halt_on_error=1:second_deadlock_stack=1")
endforeach()

add_executable(hio_gui_tests
    viewmodels/app_view_model_test.cpp
    viewmodels/connection_view_model_test.cpp
    viewmodels/device_tab_view_model_test.cpp
    viewmodels/error_info_test.cpp
    viewmodels/gui_test_support.h
    viewmodels/main.cpp
    viewmodels/page_registry_test.cpp
    viewmodels/probe_picker_test.cpp
    viewmodels/settings_store_test.cpp
    viewmodels/shortcut_registry_test.cpp
    viewmodels/single_instance_helper.h
    viewmodels/single_instance_test.cpp
    ${HIO_GUI_TEST_SUPPORT})
hio_apply_target_defaults(hio_gui_tests)
target_include_directories(hio_gui_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/viewmodels"
    "${PROJECT_SOURCE_DIR}/libs/device/tests")
target_link_libraries(hio_gui_tests PRIVATE hio_gui Qt6::Test GTest::gmock)
target_compile_definitions(hio_gui_tests PRIVATE
    "HIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hio_fake_jlink>\"")
set_target_properties(hio_gui_tests PROPERTIES AUTOMOC ON)
add_dependencies(hio_gui_tests hio_fake_jlink)
gtest_discover_tests(hio_gui_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES
        LABELS "unit"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none;QT_IM_MODULES=none")
