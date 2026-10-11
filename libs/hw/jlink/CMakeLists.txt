# libs/hw/jlink - runtime-loaded SEGGER JLinkARM library: locator, loader (one copy per session),
# probe, RTT (plan/features/0002-m1-jlink-rtt.md, ADR-0006, ADR-0028..0030, ADR-0034).
# Backend library: QtCore only (ADR-0004), static (ADR-0022). The only module that calls the
# vendor library (CLAUDE.md: vendor APIs only inside libs/hw/* adapters).

hio_add_backend_library(hio_hw_jlink
    include/hio/hw/jlink/clock.h
    include/hio/hw/jlink/jlink_api.h
    include/hio/hw/jlink/jlink_library.h
    include/hio/hw/jlink/jlink_probe.h
    include/hio/hw/jlink/library_locator.h
    include/hio/hw/jlink/probe_enumerator.h
    include/hio/hw/jlink/programmer.h
    include/hio/hw/jlink/rtt_channel.h
    include/hio/hw/jlink/types.h
    src/clock.cpp
    src/jlink_abi.h
    src/jlink_library.cpp
    src/jlink_probe.cpp
    src/library_locator.cpp
    src/out_handlers.cpp
    src/out_handlers.h
    src/probe_enumerator.cpp
    src/programmer.cpp
    src/rtt_channel.cpp
    src/types.cpp)
add_library(hio::hw_jlink ALIAS hio_hw_jlink)

target_include_directories(hio_hw_jlink PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hio_hw_jlink PUBLIC hio::core hio::firmware Qt6::Core)

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
