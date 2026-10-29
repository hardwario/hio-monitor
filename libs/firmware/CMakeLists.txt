# libs/firmware - firmware images: Intel HEX parser, FirmwareImage, read-only zip access (miniz,
# ADR-0041), LTE input classification (ADR-0040), firmware catalog download and bounded cache
# (ADR-0037), shared programming progress types (plan/features/0003-m2-flashing.md).
# Backend library: QtCore + QtNetwork (ADR-0004), static (ADR-0022).

hwio_add_backend_library(hwio_firmware
    include/hwio/firmware/designated_firmware.h
    include/hwio/firmware/firmware_cache.h
    include/hwio/firmware/firmware_catalog.h
    include/hwio/firmware/firmware_image.h
    include/hwio/firmware/intel_hex.h
    include/hwio/firmware/lte_input.h
    include/hwio/firmware/program_progress.h
    include/hwio/firmware/zip_reader.h
    src/catalog_page.cpp
    src/designated_firmware.cpp
    src/file_io.cpp
    src/file_io.h
    src/firmware_cache.cpp
    src/firmware_catalog.cpp
    src/firmware_image.cpp
    src/http_fetch.cpp
    src/http_fetch.h
    src/intel_hex.cpp
    src/lte_input.cpp
    src/program_progress.cpp
    src/zip_reader.cpp)
add_library(hwio::firmware ALIAS hwio_firmware)

target_include_directories(hwio_firmware PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hwio_firmware
    PUBLIC hwio::core Qt6::Core Qt6::Network
    PRIVATE hwio::miniz)
# HWIO_CATALOG_URL is honoured only in builds with HWIO_TEST_OVERRIDES (review R8).
target_compile_definitions(hwio_firmware PRIVATE ${HWIO_TEST_OVERRIDES_DEFINE})
# CatalogOperation / FirmwareCatalog are QObjects.
set_target_properties(hwio_firmware PROPERTIES AUTOMOC ON)

if(HWIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
# libFuzzer target catalog_page_fuzz (plan 0021; only the linux-fuzz preset of ADR-0152 sets
# HWIO_BUILD_FUZZERS).
if(HWIO_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
