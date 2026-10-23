# CMake-level tests: the backend link rule (ADR-0004) must reject GUI Qt modules.
set(_hio_rule_project "${CMAKE_CURRENT_SOURCE_DIR}/backend_links_gui")
set(_hio_rule_common_args
    -S "${_hio_rule_project}"
    -G "${CMAKE_GENERATOR}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")

add_test(NAME cmake_backend_rule_rejects_gui
    COMMAND "${CMAKE_COMMAND}" ${_hio_rule_common_args}
        -B "${CMAKE_CURRENT_BINARY_DIR}/rejects_gui"
        -DHIO_TEST_LINK_GUI=ON)
set_tests_properties(cmake_backend_rule_rejects_gui PROPERTIES
    PASS_REGULAR_EXPRESSION "Backend library 'fake_backend' links 'Qt6::Gui'.*ADR-0004"
    LABELS "cmake")

add_test(NAME cmake_backend_rule_accepts_core
    COMMAND "${CMAKE_COMMAND}" ${_hio_rule_common_args}
        -B "${CMAKE_CURRENT_BINARY_DIR}/accepts_core"
        -DHIO_TEST_LINK_GUI=OFF)
set_tests_properties(cmake_backend_rule_accepts_core PROPERTIES
    PASS_REGULAR_EXPRESSION "backend link rule checked for 1 libraries"
    FAIL_REGULAR_EXPRESSION "ADR-0004;CMake Error"
    LABELS "cmake")

# ADR-0150: hio_restrict_own_links (hio_mcp may link hio::core only).
add_test(NAME cmake_restricted_links_rejects_other
    COMMAND "${CMAKE_COMMAND}" ${_hio_rule_common_args}
        -B "${CMAKE_CURRENT_BINARY_DIR}/restricted_forbidden"
        -DHIO_TEST_LINK_GUI=OFF -DHIO_TEST_RESTRICTED_LINK=FORBIDDEN)
set_tests_properties(cmake_restricted_links_rejects_other PROPERTIES
    PASS_REGULAR_EXPRESSION "Library 'fake_backend' links 'hio::fake_other'.*ADR-0150"
    LABELS "cmake")

add_test(NAME cmake_restricted_links_accepts_allowed
    COMMAND "${CMAKE_COMMAND}" ${_hio_rule_common_args}
        -B "${CMAKE_CURRENT_BINARY_DIR}/restricted_allowed"
        -DHIO_TEST_LINK_GUI=OFF -DHIO_TEST_RESTRICTED_LINK=ALLOWED)
set_tests_properties(cmake_restricted_links_accepts_allowed PROPERTIES
    PASS_REGULAR_EXPRESSION "own-link restriction checked for 1 libraries"
    FAIL_REGULAR_EXPRESSION "ADR-0150;CMake Error"
    LABELS "cmake")

# ADR-0086 (plan/features/0011 D1): on macOS both binaries carry the Bluetooth usage description.
if(APPLE)
    add_test(NAME macos_cli_info_plist
        COMMAND "${CMAKE_COMMAND}" "-DHIO_BINARY=$<TARGET_FILE:hio>" -DHIO_KIND=cli
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_macos_plist.cmake")
    add_test(NAME macos_gui_info_plist
        COMMAND "${CMAKE_COMMAND}"
            "-DHIO_BINARY=$<TARGET_BUNDLE_CONTENT_DIR:hio-monitor>/Info.plist" -DHIO_KIND=gui
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_macos_plist.cmake")
    set_tests_properties(macos_cli_info_plist macos_gui_info_plist PROPERTIES LABELS "cmake")
endif()
