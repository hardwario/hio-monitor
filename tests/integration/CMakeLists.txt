# Hardware-in-the-loop tests (plan/features/0002-m1-jlink-rtt.md, phase G1). Opt-in: every test
# is skipped unless HIO_HIL=1. They need J-Link Software 8.x, a J-Link probe and a CHESTER with
# firmware that has the RTT shell. Environment:
#   HIO_HIL=1                  run them
#   HIO_JLINK_LIBRARY=PATH     the J-Link library (default: auto-discovery)
#   HIO_HIL_SN=SERIAL_NUMBER   the probe (default: the first free one)
#   HIO_HIL_SN2=SERIAL_NUMBER  a second probe with a second CHESTER (two-session test)
#   M2 flashing (hil_flash_test.cpp, plan/features/0003 G1): HIO_HIL_APP_HEX, HIO_HIL_FIRMWARE_ID,
#   HIO_HIL_LTE_SN, HIO_HIL_LTE_BUNDLE, HIO_HIL_DESTRUCTIVE=1 (erase --all / recover erase the PIB)
#   Plan 0021 phase 2 (hil_test.cpp): Hil.InfoShowAndCommandTreeThroughSilentQueries (HIO_HIL_SN)
# Run: ctest --preset linux-debug -L hil --output-on-failure   (with HIO_HIL=1 exported)

add_executable(hio_hil_tests
    hil_flash_test.cpp
    hil_test.cpp
    main.cpp)
hio_apply_target_defaults(hio_hil_tests)
target_link_libraries(hio_hil_tests PRIVATE hio::device GTest::gmock)

gtest_discover_tests(hio_hil_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "hil" TIMEOUT 300)
