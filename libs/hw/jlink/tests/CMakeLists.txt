# Unit tests of libs/hw/jlink (GoogleTest + GMock, ADR-0010). JLinkProbe and RttChannel run
# against a GMock of IJLinkApi on simulated time; JLinkLibrary loads copies of the test-only fake
# library (tests/fake_jlink).

add_executable(hwio_hw_jlink_tests
    fake_clock.h
    jlink_library_test.cpp
    jlink_probe_test.cpp
    library_locator_test.cpp
    main.cpp
    mock_jlink_api.h
    out_handlers_test.cpp
    programmer_test.cpp
    rtt_channel_test.cpp
    types_test.cpp)
hwio_apply_target_defaults(hwio_hw_jlink_tests)
target_include_directories(hwio_hw_jlink_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(hwio_hw_jlink_tests PRIVATE hwio::hw_jlink GTest::gmock)
target_compile_definitions(hwio_hw_jlink_tests PRIVATE
    "HWIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hwio_fake_jlink>\""
    "HWIO_FAKE_JLINK_INCOMPLETE_LIBRARY=\"$<TARGET_FILE:hwio_fake_jlink_incomplete>\""
    "HWIO_FAKE_JLINK_BASIC_LIBRARY=\"$<TARGET_FILE:hwio_fake_jlink_basic>\"")
add_dependencies(hwio_hw_jlink_tests hwio_fake_jlink hwio_fake_jlink_incomplete hwio_fake_jlink_basic)

gtest_discover_tests(hwio_hw_jlink_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
