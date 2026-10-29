# Unit tests of libs/diag (GoogleTest, ADR-0010): check runner, environment checks against the
# test-only fake J-Link library and fake nRF Util, USB access on a temporary sysfs tree, process
# matching with a fake source, redaction and the report zip. Run under ASan/UBSan and TSan.

add_executable(hwio_diag_tests
    check_runner_test.cpp
    environment_test.cpp
    main.cpp
    process_scan_test.cpp
    report_test.cpp
    usb_access_test.cpp)
hwio_apply_target_defaults(hwio_diag_tests)
target_link_libraries(hwio_diag_tests PRIVATE hwio::diag hwio::miniz GTest::gmock)
target_compile_definitions(hwio_diag_tests PRIVATE
    "HWIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hwio_fake_jlink>\""
    "HWIO_FAKE_NRFUTIL=\"$<TARGET_FILE:hwio_fake_nrfutil>\""
    "HWIO_PACKAGED_UDEV_RULE=\"${PROJECT_SOURCE_DIR}/packaging/linux/60-hardwario-jlink.rules\"")
add_dependencies(hwio_diag_tests hwio_fake_jlink hwio_fake_nrfutil)

gtest_discover_tests(hwio_diag_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
