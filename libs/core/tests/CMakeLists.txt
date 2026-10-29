# Unit tests of libs/core (GoogleTest, ADR-0010).

add_executable(hwio_core_tests
    bounded_queue_test.cpp
    crash_handler_test.cpp
    error_test.cpp
    i18n_test.cpp
    json_test.cpp
    log_test.cpp
    main.cpp
    ring_buffer_test.cpp
    settings_test.cpp
    version_test.cpp)
hwio_apply_target_defaults(hwio_core_tests)
target_include_directories(hwio_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(hwio_core_tests PRIVATE hwio::core hwio::version_info GTest::gmock)

# Crash handler tests (plan 0015 B1): a child process that installs the handler and crashes.
add_executable(hwio_crash_child crash_child.cpp)
hwio_apply_target_defaults(hwio_crash_child)
target_link_libraries(hwio_crash_child PRIVATE hwio::core)
if(WIN32)
    target_compile_definitions(hwio_crash_child PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
add_dependencies(hwio_core_tests hwio_crash_child)
target_compile_definitions(hwio_core_tests PRIVATE
    "HWIO_CRASH_CHILD=\"$<TARGET_FILE:hwio_crash_child>\"")

# A small test catalog for hwio::i18n::tr (context "hwio::test"); the project catalogs are added
# to this target by cmake/HWioTranslations.cmake (resource prefix /i18n).
qt_add_lrelease(
    LRELEASE_TARGET hwio_core_tests_lrelease
    NO_GLOBAL_TARGET
    TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/tst_i18n_cs.ts"
    QM_FILES_OUTPUT_VARIABLE _hwio_core_test_qm)
qt_add_resources(hwio_core_tests "hwio_core_test_i18n"
    PREFIX "/test-i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${_hwio_core_test_qm}
    OUTPUT_TARGETS _hwio_core_test_resource_targets)
hwio_skip_linting(${_hwio_core_test_resource_targets})

gtest_discover_tests(hwio_core_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
