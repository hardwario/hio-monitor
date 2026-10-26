# Unit tests of libs/update (plan/features/0016 A1, A2): SemVer precedence, pickLatest on a saved
# (trimmed) GitHub release list, UpdateCheck against the local HTTP fixture of libs/firmware.

add_executable(hio_update_tests
    main.cpp
    releases_test.cpp
    semver_test.cpp
    update_check_test.cpp)
hio_apply_target_defaults(hio_update_tests)
target_link_libraries(hio_update_tests PRIVATE hio::update Qt6::Network GTest::gmock)
# The local HTTP fixture (QTcpServer) shared with the firmware, device and CLI tests.
target_include_directories(hio_update_tests PRIVATE "${PROJECT_SOURCE_DIR}/libs/firmware/tests")
target_compile_definitions(hio_update_tests PRIVATE
    HIO_UPDATE_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

# Under ThreadSanitizer the UpdateCheck tests (QNetworkAccessManager) are left out, for the reason
# documented in libs/firmware/tests/CMakeLists.txt (uninstrumented Qt HTTP thread).
set(_hio_update_test_filter "")
if("thread" IN_LIST HIO_SANITIZERS)
    set(_hio_update_test_filter TEST_FILTER "-UpdateCheck.*")
endif()
gtest_discover_tests(hio_update_tests
    DISCOVERY_TIMEOUT 60
    ${_hio_update_test_filter}
    PROPERTIES LABELS "unit")
