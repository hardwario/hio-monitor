# libs/firmware - firmware images: Intel HEX parser, FirmwareImage, read-only zip access (miniz,
# ADR-0041), LTE input classification (ADR-0040), firmware catalog download and bounded cache
# (ADR-0037), shared programming progress types (plan/features/0003-m2-flashing.md).
# Backend library: QtCore + QtNetwork (ADR-0004), static (ADR-0022).

hio_add_backend_library(hio_firmware
    include/hio/firmware/firmware_cache.h
    include/hio/firmware/firmware_catalog.h
    include/hio/firmware/firmware_image.h
    include/hio/firmware/intel_hex.h
    include/hio/firmware/lte_input.h
    include/hio/firmware/program_progress.h
    include/hio/firmware/zip_reader.h
    src/file_io.cpp
    src/file_io.h
    src/firmware_cache.cpp
    src/firmware_catalog.cpp
    src/firmware_image.cpp
    src/intel_hex.cpp
    src/lte_input.cpp
    src/program_progress.cpp
    src/zip_reader.cpp)
add_library(hio::firmware ALIAS hio_firmware)

target_include_directories(hio_firmware PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hio_firmware
    PUBLIC hio::core Qt6::Core Qt6::Network
    PRIVATE hio::miniz)
# CatalogOperation / FirmwareCatalog are QObjects.
set_target_properties(hio_firmware PROPERTIES AUTOMOC ON)

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
