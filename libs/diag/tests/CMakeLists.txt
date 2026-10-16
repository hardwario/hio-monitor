# Unit tests of libs/diag (GoogleTest, ADR-0010): check runner, environment checks against the
# test-only fake J-Link library and fake nRF Util, USB access on a temporary sysfs tree, process
# matching with a fake source, redaction and the report zip. Run under ASan/UBSan and TSan.

add_executable(hio_diag_tests
    check_runner_test.cpp
    environment_test.cpp
    main.cpp
    process_scan_test.cpp
    report_test.cpp
    usb_access_test.cpp)
hio_apply_target_defaults(hio_diag_tests)
target_link_libraries(hio_diag_tests PRIVATE hio::diag hio::miniz GTest::gmock)
target_compile_definitions(hio_diag_tests PRIVATE
    "HIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hio_fake_jlink>\""
    "HIO_FAKE_NRFUTIL=\"$<TARGET_FILE:hio_fake_nrfutil>\"")
add_dependencies(hio_diag_tests hio_fake_jlink hio_fake_nrfutil)

gtest_discover_tests(hio_diag_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
