# libs/core - hio::Error, Result, JSON, logging, settings, bounded buffers, i18n, build info.
# Backend library: QtCore only (ADR-0004), static (ADR-0022).

hio_add_backend_library(hio_core
    include/hio/core/app_identity.h
    include/hio/core/bounded_queue.h
    include/hio/core/crash_handler.h
    include/hio/core/error.h
    include/hio/core/i18n.h
    include/hio/core/json.h
    include/hio/core/log.h
    include/hio/core/result.h
    include/hio/core/ring_buffer.h
    include/hio/core/settings.h
    include/hio/core/version.h
    src/app_identity.cpp
    src/crash_handler.cpp
    src/error.cpp
    src/i18n.cpp
    src/json.cpp
    src/log.cpp
    src/settings.cpp
    src/settings_internal.h
    src/version.cpp)
add_library(hio::core ALIAS hio_core)

target_include_directories(hio_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hio_core
    PUBLIC Qt6::Core
    PRIVATE hio::version_info)

# Crash handler (ADR-0123, plan/features/0015 B1): the platform part.
if(WIN32)
    target_sources(hio_core PRIVATE src/crash_handler_win.cpp)
    target_compile_definitions(hio_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # MiniDumpWriteDump: dbghelp is a Windows system library (not a vendor SDK).
    target_link_libraries(hio_core PRIVATE dbghelp)
else()
    target_sources(hio_core PRIVATE src/crash_handler_posix.cpp)
endif()

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
