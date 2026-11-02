# CMake-level tests: the backend link rule (ADR-0004) must reject GUI Qt modules.
set(_hwio_rule_project "${CMAKE_CURRENT_SOURCE_DIR}/backend_links_gui")
set(_hwio_rule_common_args
    -S "${_hwio_rule_project}"
    -G "${CMAKE_GENERATOR}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")

add_test(NAME cmake_backend_rule_rejects_gui
    COMMAND "${CMAKE_COMMAND}" ${_hwio_rule_common_args}
        -B "${CMAKE_CURRENT_BINARY_DIR}/rejects_gui"
        -DHWIO_TEST_LINK_GUI=ON)
set_tests_properties(cmake_backend_rule_rejects_gui PROPERTIES
    PASS_REGULAR_EXPRESSION "Backend library 'fake_backend' links 'Qt6::Gui'.*ADR-0004"
    LABELS "cmake")

add_test(NAME cmake_backend_rule_accepts_core
    COMMAND "${CMAKE_COMMAND}" ${_hwio_rule_common_args}
        -B "${CMAKE_CURRENT_BINARY_DIR}/accepts_core"
        -DHWIO_TEST_LINK_GUI=OFF)
set_tests_properties(cmake_backend_rule_accepts_core PROPERTIES
    PASS_REGULAR_EXPRESSION "backend link rule checked for 1 libraries"
    FAIL_REGULAR_EXPRESSION "ADR-0004;CMake Error"
    LABELS "cmake")

# ADR-0150: hwio_restrict_own_links (hwio_mcp may link hwio::core only).
add_test(NAME cmake_restricted_links_rejects_other
    COMMAND "${CMAKE_COMMAND}" ${_hwio_rule_common_args}
        -B "${CMAKE_CURRENT_BINARY_DIR}/restricted_forbidden"
        -DHWIO_TEST_LINK_GUI=OFF -DHWIO_TEST_RESTRICTED_LINK=FORBIDDEN)
set_tests_properties(cmake_restricted_links_rejects_other PROPERTIES
    PASS_REGULAR_EXPRESSION "Library 'fake_backend' links 'hwio::fake_other'.*ADR-0150"
    LABELS "cmake")

add_test(NAME cmake_restricted_links_accepts_allowed
    COMMAND "${CMAKE_COMMAND}" ${_hwio_rule_common_args}
        -B "${CMAKE_CURRENT_BINARY_DIR}/restricted_allowed"
        -DHWIO_TEST_LINK_GUI=OFF -DHWIO_TEST_RESTRICTED_LINK=ALLOWED)
set_tests_properties(cmake_restricted_links_accepts_allowed PROPERTIES
    PASS_REGULAR_EXPRESSION "own-link restriction checked for 1 libraries"
    FAIL_REGULAR_EXPRESSION "ADR-0150;CMake Error"
    LABELS "cmake")

# ADR-0086 (plan/features/0011 D1): on macOS both binaries carry the Bluetooth usage description.
if(APPLE)
    add_test(NAME macos_cli_info_plist
        COMMAND "${CMAKE_COMMAND}" "-DHWIO_BINARY=$<TARGET_FILE:hwio>" -DHWIO_KIND=cli
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_macos_plist.cmake")
    add_test(NAME macos_gui_info_plist
        COMMAND "${CMAKE_COMMAND}"
            "-DHWIO_BINARY=$<TARGET_BUNDLE_CONTENT_DIR:hwio-monitor>/Info.plist" -DHWIO_KIND=gui
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_macos_plist.cmake")
    set_tests_properties(macos_cli_info_plist macos_gui_info_plist PROPERTIES LABELS "cmake")
endif()

# ADR-0184 (plan/features/0025, M-2): QML text bound to data is PlainText. The second test proves
# that the scanner finds the five problems of its fixture.
add_test(NAME qml_plaintext_check
    COMMAND "${CMAKE_COMMAND}" "-DHWIO_QML_DIR=${PROJECT_SOURCE_DIR}/app/gui/qml"
        "-DHWIO_QML_ALLOWLIST=${CMAKE_CURRENT_SOURCE_DIR}/qml_plaintext_allowlist.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_qml_plaintext.cmake")
set_tests_properties(qml_plaintext_check PROPERTIES
    PASS_REGULAR_EXPRESSION "qml_plaintext_check: [0-9]+ QML files OK"
    FAIL_REGULAR_EXPRESSION "ADR-0184"
    LABELS "cmake")
add_test(NAME qml_plaintext_check_detects
    COMMAND "${CMAKE_COMMAND}" "-DHWIO_QML_DIR=${CMAKE_CURRENT_SOURCE_DIR}/qml_plaintext_fixture"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_qml_plaintext.cmake")
set_tests_properties(qml_plaintext_check_detects PROPERTIES
    PASS_REGULAR_EXPRESSION "5 problem\\(s\\) in 1 files"
    LABELS "cmake")

# Plan 0028 (ADR-0202): only AppViewModel::copyToClipboard writes the clipboard (+ the toast).
add_test(NAME clipboard_write_check
    COMMAND "${CMAKE_COMMAND}" "-DHWIO_GUI_SRC=${PROJECT_SOURCE_DIR}/app/gui/src"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_clipboard_writes.cmake")
set_tests_properties(clipboard_write_check PROPERTIES
    PASS_REGULAR_EXPRESSION "clipboard_write_check: [0-9]+ files OK"
    FAIL_REGULAR_EXPRESSION "ADR-0202"
    LABELS "cmake")

# Plan 0028 (D15): the translation lint over i18n/*.ts (stray '&', '<', combining marks, invisible
# characters, placeholder sets); the second test proves it finds the six problems of its fixture.
if(HWIO_REQUIRE_PYTHON)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
else()
    find_package(Python3 COMPONENTS Interpreter)
endif()
if(Python3_Interpreter_FOUND)
    file(GLOB _hwio_ts_files "${PROJECT_SOURCE_DIR}/i18n/*.ts")
    add_test(NAME translation_lint
        COMMAND "${Python3_EXECUTABLE}" -I "${PROJECT_SOURCE_DIR}/tools/translation_lint.py"
            ${_hwio_ts_files})
    set_tests_properties(translation_lint PROPERTIES
        PASS_REGULAR_EXPRESSION "translation_lint: OK"
        LABELS "cmake")
    add_test(NAME translation_lint_detects
        COMMAND "${Python3_EXECUTABLE}" -I "${PROJECT_SOURCE_DIR}/tools/translation_lint.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/translation_lint_fixture.ts")
    set_tests_properties(translation_lint_detects PROPERTIES
        PASS_REGULAR_EXPRESSION "translation_lint: 6 problem\\(s\\)"
        LABELS "cmake")
    unset(_hwio_ts_files)
else()
    message(WARNING "Python 3 was not found: the translation_lint test is not added")
endif()

# ADR-0187 (plan/features/0025, M-5): the executables carry the hardening of HWioHardening.cmake
# (builds without sanitizers; macOS is not checked).
if(NOT HWIO_SANITIZERS AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32))
    set(_hwio_hardened_binaries "$<TARGET_FILE:hwio>")
    if(TARGET hwio-monitor)
        list(APPEND _hwio_hardened_binaries "$<TARGET_FILE:hwio-monitor>")
    endif()
    if(WIN32)
        find_program(HWIO_BINARY_READER NAMES llvm-readobj llvm-readobj-21 REQUIRED)
    else()
        find_program(HWIO_BINARY_READER NAMES readelf llvm-readelf llvm-readelf-21 REQUIRED)
    endif()
    add_test(NAME binary_hardening_check
        COMMAND "${CMAKE_COMMAND}" "-DHWIO_OS=${CMAKE_SYSTEM_NAME}"
            "-DHWIO_READER=${HWIO_BINARY_READER}"
            "-DHWIO_BINARIES=${_hwio_hardened_binaries}"
            "-DHWIO_EXPECT_FORTIFY=$<IF:$<CONFIG:Release,RelWithDebInfo>,ON,OFF>"
            "-DHWIO_ARCHIVES=$<TARGET_FILE:hwio_miniz>;$<TARGET_FILE:replxx>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_binary_hardening.cmake")
    set_tests_properties(binary_hardening_check PROPERTIES
        PASS_REGULAR_EXPRESSION "binary_hardening_check: [0-9]+ executables hardened"
        FAIL_REGULAR_EXPRESSION "ADR-0187"
        LABELS "cmake")
    unset(_hwio_hardened_binaries)
endif()
