# Unit tests of libs/update (plan/features/0016 A1, A2): SemVer precedence, pickLatest on a saved
# (trimmed) GitHub release list, UpdateCheck against the local HTTP fixture of libs/firmware.

add_executable(hwio_update_tests
    main.cpp
    releases_test.cpp
    semver_test.cpp
    update_check_test.cpp)
hwio_apply_target_defaults(hwio_update_tests)
target_link_libraries(hwio_update_tests PRIVATE hwio::update Qt6::Network GTest::gmock)
# The local HTTP fixture (QTcpServer) shared with the firmware, device and CLI tests.
target_include_directories(hwio_update_tests PRIVATE "${PROJECT_SOURCE_DIR}/libs/firmware/tests")
target_compile_definitions(hwio_update_tests PRIVATE
    HWIO_UPDATE_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

# Under ThreadSanitizer the UpdateCheck tests (QNetworkAccessManager) are left out, for the reason
# documented in libs/firmware/tests/CMakeLists.txt (uninstrumented Qt HTTP thread).
set(_hwio_update_test_filter "")
if("thread" IN_LIST HWIO_SANITIZERS)
    set(_hwio_update_test_filter TEST_FILTER "-UpdateCheck.*")
endif()
gtest_discover_tests(hwio_update_tests
    DISCOVERY_TIMEOUT 60
    ${_hwio_update_test_filter}
    PROPERTIES LABELS "unit")
