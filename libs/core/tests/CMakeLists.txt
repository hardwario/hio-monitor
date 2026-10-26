# Unit tests of libs/core (GoogleTest, ADR-0010).

add_executable(hio_core_tests
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
hio_apply_target_defaults(hio_core_tests)
target_include_directories(hio_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(hio_core_tests PRIVATE hio::core hio::version_info GTest::gmock)

# Crash handler tests (plan 0015 B1): a child process that installs the handler and crashes.
add_executable(hio_crash_child crash_child.cpp)
hio_apply_target_defaults(hio_crash_child)
target_link_libraries(hio_crash_child PRIVATE hio::core)
if(WIN32)
    target_compile_definitions(hio_crash_child PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
add_dependencies(hio_core_tests hio_crash_child)
target_compile_definitions(hio_core_tests PRIVATE
    "HIO_CRASH_CHILD=\"$<TARGET_FILE:hio_crash_child>\"")

# A small test catalog for hio::i18n::tr (context "hio::test"); the project catalogs are added
# to this target by cmake/HioTranslations.cmake (resource prefix /i18n).
qt_add_lrelease(
    LRELEASE_TARGET hio_core_tests_lrelease
    NO_GLOBAL_TARGET
    TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/tst_i18n_cs.ts"
    QM_FILES_OUTPUT_VARIABLE _hio_core_test_qm)
qt_add_resources(hio_core_tests "hio_core_test_i18n"
    PREFIX "/test-i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${_hio_core_test_qm}
    OUTPUT_TARGETS _hio_core_test_resource_targets)
hio_skip_linting(${_hio_core_test_resource_targets})

gtest_discover_tests(hio_core_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
