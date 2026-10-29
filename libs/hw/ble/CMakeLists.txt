# libs/hw/ble - the BLE transport (plan/features/0011-m5-ble-backend-cli.md, ADR-0080, ADR-0087,
# ADR-0127, ADR-0129): IBleApi with QtBleApi (the only code using QtBluetooth), the BleHost thread,
# NusLink (connect, chunking, write modes, timeouts, pairing wait) and BleScanner. No knowledge of
# device profiles or console lines (ADR-0129). Backend library: QtCore + QtBluetooth (ADR-0004
# allows it; LGPLv3, dynamically linked, ADR-0002), static (ADR-0022).

hwio_add_backend_library(hwio_hw_ble
    include/hwio/hw/ble/ble_api.h
    include/hwio/hw/ble/ble_host.h
    include/hwio/hw/ble/ble_scanner.h
    include/hwio/hw/ble/event_channel.h
    include/hwio/hw/ble/link_handler.h
    include/hwio/hw/ble/nus_link.h
    include/hwio/hw/ble/types.h
    src/ble_api.cpp
    src/ble_host.cpp
    src/ble_scanner.cpp
    src/ble_worker.cpp
    src/ble_worker.h
    src/controller_closer.cpp
    src/controller_closer.h
    src/nus_link.cpp
    src/qt_ble_api.cpp
    src/qt_ble_api.h
    src/types.cpp)
add_library(hwio::hw_ble ALIAS hwio_hw_ble)

# ADR-0127: the scenario-driven fake backend only in builds with HWIO_TEST_OVERRIDES (its sources
# are not part of the target otherwise; createBleApi() then always returns QtBleApi).
# With a multi-config generator the sources follow the define's configurations (never Release,
# RelWithDebInfo or MinSizeRel; see HWIO_TEST_OVERRIDES in the top-level CMakeLists.txt).
if(HWIO_TEST_OVERRIDES)
    set(_hwio_fake_ble_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/fake/fake_ble_api.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/fake/fake_ble_api.h")
    if(HWIO_TEST_OVERRIDES_DEFINE STREQUAL "HWIO_TEST_OVERRIDES")
        target_sources(hwio_hw_ble PRIVATE ${_hwio_fake_ble_sources})
    else()
        target_sources(hwio_hw_ble PRIVATE
            "$<$<NOT:$<CONFIG:Release,RelWithDebInfo,MinSizeRel>>:${_hwio_fake_ble_sources}>")
    endif()
    unset(_hwio_fake_ble_sources)
endif()
target_compile_definitions(hwio_hw_ble PRIVATE ${HWIO_TEST_OVERRIDES_DEFINE})

target_include_directories(hwio_hw_ble PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hwio_hw_ble PUBLIC hwio::core Qt6::Core Qt6::Bluetooth)
set_target_properties(hwio_hw_ble PROPERTIES AUTOMOC ON)

if(HWIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
