# Unit tests of libs/hw/jlink (GoogleTest + GMock, ADR-0010). JLinkProbe and RttChannel run
# against a GMock of IJLinkApi on simulated time; JLinkLibrary loads copies of the test-only fake
# library (tests/fake_jlink).

add_executable(hio_hw_jlink_tests
    fake_clock.h
    jlink_library_test.cpp
    jlink_probe_test.cpp
    library_locator_test.cpp
    main.cpp
    mock_jlink_api.h
    out_handlers_test.cpp
    rtt_channel_test.cpp
    types_test.cpp)
hio_apply_target_defaults(hio_hw_jlink_tests)
target_include_directories(hio_hw_jlink_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(hio_hw_jlink_tests PRIVATE hio::hw_jlink GTest::gmock)
target_compile_definitions(hio_hw_jlink_tests PRIVATE
    "HIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hio_fake_jlink>\""
    "HIO_FAKE_JLINK_INCOMPLETE_LIBRARY=\"$<TARGET_FILE:hio_fake_jlink_incomplete>\"")
add_dependencies(hio_hw_jlink_tests hio_fake_jlink hio_fake_jlink_incomplete)

gtest_discover_tests(hio_hw_jlink_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
