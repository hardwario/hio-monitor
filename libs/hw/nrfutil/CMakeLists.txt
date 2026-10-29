# libs/hw/nrfutil - Nordic nRF Util as an optional subprocess for the nRF91 modem firmware:
# locator, version check (ADR-0042), modem updater (ADR-0016) (plan/features/0003-m2-flashing.md).
# Backend library: QtCore only (ADR-0004), static (ADR-0022). The only module that starts nRF Util.

hwio_add_backend_library(hwio_hw_nrfutil
    include/hwio/hw/nrfutil/nrfutil.h
    src/nrfutil.cpp)
add_library(hwio::hw_nrfutil ALIAS hwio_hw_nrfutil)

target_include_directories(hwio_hw_nrfutil PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hwio_hw_nrfutil PUBLIC hwio::core hwio::firmware Qt6::Core)

if(HWIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
