# Tests of app/gui (plan/features/0004-m3-gui-shell.md):
#   tst_gui        Qt Quick Test (tst_*.qml): components in light and dark, ErrorPanel, shell,
#                  shortcuts, layout at 960 x 600 and 1099 px in Czech; offscreen platform.
#   hwio_gui_tests  GoogleTest view model tests against the fake J-Link library (tests/fake_jlink).
# Both run under ASan/UBSan and TSan via the presets.

set(HWIO_GUI_TEST_SUPPORT
    support/fake_jlink_scenario.h
    support/test_page_view_model.cpp
    support/test_page_view_model.h
    support/test_wizard.cpp
    support/test_wizard.h)

qt_add_executable(tst_gui tst_gui.cpp ${HWIO_GUI_TEST_SUPPORT})
hwio_apply_target_defaults(tst_gui)
target_include_directories(tst_gui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    # hex_builder.h, zip_builder.h (plan 0006 test files)
    "${PROJECT_SOURCE_DIR}/libs/firmware/tests")
target_link_libraries(tst_gui PRIVATE hwio_gui hwio_guiplugin hwio::miniz Qt6::QuickTest Qt6::QuickControls2)
target_compile_definitions(tst_gui PRIVATE
    "QUICK_TEST_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
    "HWIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hwio_fake_jlink>\"")
add_dependencies(tst_gui hwio_fake_jlink)
# M5 (plan 0011 E1): the environment checks include `bluetooth`; the tests read the fake BLE
# backend instead of the machine's Bluetooth stack (HWIO_TEST_OVERRIDES builds, ADR-0127).
set(_hwio_gui_test_ble "")
set(_hwio_gui_unit_test_ble "")
if(HWIO_TEST_OVERRIDES)
    set(_hwio_gui_ble_scenario "${PROJECT_SOURCE_DIR}/tests/ble_scenarios/chester.json")
    set(_hwio_gui_test_ble ";HWIO_FAKE_BLE_SCENARIO=${_hwio_gui_ble_scenario}")
    # gtest_discover_tests() splits PROPERTIES at ';': one variable per property value.
    set(_hwio_gui_unit_test_ble
        ENVIRONMENT_MODIFICATION "HWIO_FAKE_BLE_SCENARIO=set:${_hwio_gui_ble_scenario}")
endif()

set_target_properties(tst_gui PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
    AUTOMOC ON
    QT_QML_MODULE_NO_IMPORT_SCAN ON)
# One process per test file: every file creates its own main window and QML engine (several
# engines in one process leave QML binding memory behind that LeakSanitizer reports).
file(GLOB _hwio_gui_qml_tests RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tst_*.qml")
foreach(_test IN LISTS _hwio_gui_qml_tests)
    cmake_path(GET _test STEM _name)
    add_test(NAME "tst_gui.${_name}"
        COMMAND tst_gui -input "${CMAKE_CURRENT_SOURCE_DIR}/${_test}")
    set_tests_properties("tst_gui.${_name}" PROPERTIES
        LABELS "qml"
        # No desktop input-method plugin (IBus over D-Bus) in tests: it starts uninstrumented Qt
        # threads that TSan reports. Older Qt reads QT_IM_MODULE, newer Qt QT_IM_MODULES.
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none;QT_IM_MODULES=none;TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/sanitizers/tsan-qml.supp:halt_on_error=1:second_deadlock_stack=1${_hwio_gui_test_ble}")
endforeach()

add_executable(hwio_gui_tests
    viewmodels/app_view_model_test.cpp
    viewmodels/connection_view_model_test.cpp
    viewmodels/console_line_model_test.cpp
    viewmodels/console_view_model_test.cpp
    viewmodels/device_tab_view_model_test.cpp
    viewmodels/error_info_test.cpp
    viewmodels/find_filter_engine_test.cpp
    viewmodels/flash_view_model_test.cpp
    viewmodels/gui_test_support.h
    viewmodels/install_paths_test.cpp
    viewmodels/main.cpp
    viewmodels/page_registry_test.cpp
    viewmodels/probe_picker_test.cpp
    viewmodels/settings_store_test.cpp
    viewmodels/settings_view_model_test.cpp
    viewmodels/shortcut_registry_test.cpp
    viewmodels/single_instance_helper.h
    viewmodels/single_instance_test.cpp
    viewmodels/update_service_test.cpp
    viewmodels/wizard_test.cpp
    ${HWIO_GUI_TEST_SUPPORT})
hwio_apply_target_defaults(hwio_gui_tests)
target_include_directories(hwio_gui_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/viewmodels"
    "${PROJECT_SOURCE_DIR}/libs/device/tests"
    # hex_builder.h, zip_builder.h, http_fixture.h (plan 0006 tests)
    "${PROJECT_SOURCE_DIR}/libs/firmware/tests")
target_link_libraries(hwio_gui_tests PRIVATE hwio_gui hwio::miniz Qt6::Test GTest::gmock)
target_compile_definitions(hwio_gui_tests PRIVATE
    "HWIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hwio_fake_jlink>\""
    "HWIO_FAKE_JLINK_INCOMPLETE_LIBRARY=\"$<TARGET_FILE:hwio_fake_jlink_incomplete>\""
    "HWIO_FAKE_NRFUTIL=\"$<TARGET_FILE:hwio_fake_nrfutil>\"")
set_target_properties(hwio_gui_tests PROPERTIES AUTOMOC ON)
add_dependencies(hwio_gui_tests hwio_fake_jlink hwio_fake_jlink_incomplete hwio_fake_nrfutil)
# Under ThreadSanitizer the tests that download through QNetworkAccessManager (the catalog, the
# update check of plan 0016) are left out, as in
# libs/firmware/tests (Qt's HTTP thread is uninstrumented; plan/features/0003, review R11).
set(_hwio_gui_test_filter "")
if("thread" IN_LIST HWIO_SANITIZERS)
    set(_hwio_gui_test_filter TEST_FILTER "-FlashTest.CatalogDownloadAndErrors:FlashTest.DesignatedLteFirmware:FlashTest.CatalogInfoOfAnId:FlashTest.ACatalogDownloadWaitsForTheLookup:UpdateTest.AutomaticCheckShowsTheBannerAndStoresTheResult:UpdateTest.NoAutomaticCheckBeforeTheIntervalOrInDevelopmentBuilds:UpdateTest.DownloadOpensTheReleasePage:UpdateTest.SkippedVersionIsNotAnnouncedAgainButManualChecksIgnoreIt:UpdateTest.LaterHidesTheBanner:UpdateTest.UpToDate:UpdateTest.FailuresAreShownOnlyForManualChecks")
endif()
gtest_discover_tests(hwio_gui_tests
    DISCOVERY_TIMEOUT 60
    ${_hwio_gui_test_filter}
    PROPERTIES
        LABELS "unit"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none;QT_IM_MODULES=none"
        ${_hwio_gui_unit_test_ble})

# Console HIL (plan/features/0005-m3-console.md): skipped unless HWIO_HIL=1 and HWIO_HIL_SN is set
# (a CHESTER with the RTT shell on that J-Link probe). Label "hil" like tests/integration.
add_executable(hwio_gui_hil_tests
    console_hil_test.cpp
    viewmodels/gui_test_support.h
    viewmodels/main.cpp
    viewmodels/single_instance_helper.h
    ${HWIO_GUI_TEST_SUPPORT})
hwio_apply_target_defaults(hwio_gui_hil_tests)
target_include_directories(hwio_gui_hil_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/viewmodels")
target_link_libraries(hwio_gui_hil_tests PRIVATE hwio_gui Qt6::Test GTest::gmock)
target_compile_definitions(hwio_gui_hil_tests PRIVATE
    "HWIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hwio_fake_jlink>\"")
set_target_properties(hwio_gui_hil_tests PROPERTIES AUTOMOC ON)
gtest_discover_tests(hwio_gui_hil_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES
        LABELS "hil"
        TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_IM_MODULE=none;QT_IM_MODULES=none")
