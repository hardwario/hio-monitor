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
