# Unit tests of libs/hw/nrfutil against the test-only fake nRF Util (tests/fake_nrfutil).

add_executable(hio_hw_nrfutil_tests
    main.cpp
    nrfutil_test.cpp)
hio_apply_target_defaults(hio_hw_nrfutil_tests)
target_link_libraries(hio_hw_nrfutil_tests PRIVATE hio::hw_nrfutil GTest::gmock)
target_compile_definitions(hio_hw_nrfutil_tests PRIVATE
    "HIO_FAKE_NRFUTIL=\"$<TARGET_FILE:hio_fake_nrfutil>\"")
add_dependencies(hio_hw_nrfutil_tests hio_fake_nrfutil)

gtest_discover_tests(hio_hw_nrfutil_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
