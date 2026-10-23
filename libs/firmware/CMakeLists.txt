# libs/firmware - firmware images: Intel HEX parser, FirmwareImage, read-only zip access (miniz,
# ADR-0041), LTE input classification (ADR-0040), firmware catalog download and bounded cache
# (ADR-0037), shared programming progress types (plan/features/0003-m2-flashing.md).
# Backend library: QtCore + QtNetwork (ADR-0004), static (ADR-0022).

hio_add_backend_library(hio_firmware
    include/hio/firmware/designated_firmware.h
    include/hio/firmware/firmware_cache.h
    include/hio/firmware/firmware_catalog.h
    include/hio/firmware/firmware_image.h
    include/hio/firmware/intel_hex.h
    include/hio/firmware/lte_input.h
    include/hio/firmware/program_progress.h
    include/hio/firmware/zip_reader.h
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
add_library(hio::firmware ALIAS hio_firmware)

target_include_directories(hio_firmware PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hio_firmware
    PUBLIC hio::core Qt6::Core Qt6::Network
    PRIVATE hio::miniz)
# HIO_CATALOG_URL is honoured only in builds with HIO_TEST_OVERRIDES (review R8).
target_compile_definitions(hio_firmware PRIVATE ${HIO_TEST_OVERRIDES_DEFINE})
# CatalogOperation / FirmwareCatalog are QObjects.
set_target_properties(hio_firmware PROPERTIES AUTOMOC ON)

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
# libFuzzer target catalog_page_fuzz (plan 0021; the linux-fuzz preset of ADR-0152 sets
# HIO_BUILD_FUZZERS - the option and hio_add_fuzzer() come with M6, until then this is inert).
if(HIO_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
