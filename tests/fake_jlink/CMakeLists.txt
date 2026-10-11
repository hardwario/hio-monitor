# Test-only fake JLinkARM library (plan/features/0002-m1-jlink-rtt.md, phase E1). Exports the
# J-Link C functions of libs/hw/jlink/src/jlink_abi.h and simulates probes, targets and RTT from
# a JSON scenario (HIO_FAKE_JLINK_SCENARIO). Links QtCore for QJsonDocument (decided 2026-10-09).
# Never installed or shipped.

add_library(hio_fake_jlink SHARED fake_jlink.cpp)
hio_apply_target_defaults(hio_fake_jlink)
target_include_directories(hio_fake_jlink PRIVATE "${PROJECT_SOURCE_DIR}/libs/hw/jlink/src")
target_link_libraries(hio_fake_jlink PRIVATE Qt6::Core)
set_target_properties(hio_fake_jlink PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# A library that exports only one J-Link function: loading it must fail with the missing names.
add_library(hio_fake_jlink_incomplete SHARED fake_jlink_incomplete.cpp)
hio_apply_target_defaults(hio_fake_jlink_incomplete)
set_target_properties(hio_fake_jlink_incomplete PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
