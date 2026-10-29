# libs/core - hwio::Error, Result, JSON, logging, settings, bounded buffers, i18n, build info.
# Backend library: QtCore only (ADR-0004), static (ADR-0022).

hwio_add_backend_library(hwio_core
    include/hwio/core/app_identity.h
    include/hwio/core/bounded_queue.h
    include/hwio/core/crash_handler.h
    include/hwio/core/error.h
    include/hwio/core/i18n.h
    include/hwio/core/json.h
    include/hwio/core/log.h
    include/hwio/core/result.h
    include/hwio/core/ring_buffer.h
    include/hwio/core/settings.h
    include/hwio/core/version.h
    src/app_identity.cpp
    src/crash_handler.cpp
    src/error.cpp
    src/i18n.cpp
    src/json.cpp
    src/log.cpp
    src/settings.cpp
    src/settings_internal.h
    src/version.cpp)
add_library(hwio::core ALIAS hwio_core)

target_include_directories(hwio_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hwio_core
    PUBLIC Qt6::Core
    PRIVATE hwio::version_info)

# Crash handler (ADR-0123, plan/features/0015 B1): the platform part.
if(WIN32)
    target_sources(hwio_core PRIVATE src/crash_handler_win.cpp)
    target_compile_definitions(hwio_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # MiniDumpWriteDump: dbghelp is a Windows system library (not a vendor SDK).
    target_link_libraries(hwio_core PRIVATE dbghelp)
else()
    target_sources(hwio_core PRIVATE src/crash_handler_posix.cpp)
endif()

if(HWIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
