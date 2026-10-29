# Hardware-in-the-loop tests (plan/features/0002-m1-jlink-rtt.md, phase G1). Opt-in: every test
# is skipped unless HWIO_HIL=1. They need J-Link Software 8.x, a J-Link probe and a CHESTER with
# firmware that has the RTT shell. Environment:
#   HWIO_HIL=1                  run them
#   HWIO_JLINK_LIBRARY=PATH     the J-Link library (default: auto-discovery)
#   HWIO_HIL_SN=SERIAL_NUMBER   the probe (default: the first free one)
#   HWIO_HIL_SN2=SERIAL_NUMBER  a second probe with a second CHESTER (two-session test)
#   M2 flashing (hil_flash_test.cpp, plan/features/0003 G1): HWIO_HIL_APP_HEX, HWIO_HIL_FIRMWARE_ID,
#   HWIO_HIL_LTE_SN, HWIO_HIL_LTE_BUNDLE, HWIO_HIL_DESTRUCTIVE=1 (erase --all / recover erase the PIB)
#   Plan 0021 phase 2 (hil_test.cpp): Hil.InfoShowAndCommandTreeThroughSilentQueries (HWIO_HIL_SN)
# Run: ctest --preset linux-debug -L hil --output-on-failure   (with HWIO_HIL=1 exported)

add_executable(hwio_hil_tests
    hil_flash_test.cpp
    hil_test.cpp
    main.cpp)
hwio_apply_target_defaults(hwio_hil_tests)
target_link_libraries(hwio_hil_tests PRIVATE hwio::device GTest::gmock)

gtest_discover_tests(hwio_hil_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "hil" TIMEOUT 300)
