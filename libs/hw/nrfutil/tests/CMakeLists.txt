# Unit tests of libs/hw/nrfutil against the test-only fake nRF Util (tests/fake_nrfutil).

add_executable(hwio_hw_nrfutil_tests
    main.cpp
    nrfutil_test.cpp)
hwio_apply_target_defaults(hwio_hw_nrfutil_tests)
target_link_libraries(hwio_hw_nrfutil_tests PRIVATE hwio::hw_nrfutil GTest::gmock)
target_compile_definitions(hwio_hw_nrfutil_tests PRIVATE
    "HWIO_FAKE_NRFUTIL=\"$<TARGET_FILE:hwio_fake_nrfutil>\"")
add_dependencies(hwio_hw_nrfutil_tests hwio_fake_nrfutil)

gtest_discover_tests(hwio_hw_nrfutil_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
