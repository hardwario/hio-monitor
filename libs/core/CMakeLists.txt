# libs/core - hio::Error, Result, JSON, logging, settings, bounded buffers, i18n, build info.
# Backend library: QtCore only (ADR-0004), static (ADR-0022).

hio_add_backend_library(hio_core
    include/hio/core/app_identity.h
    include/hio/core/bounded_queue.h
    include/hio/core/error.h
    include/hio/core/i18n.h
    include/hio/core/json.h
    include/hio/core/log.h
    include/hio/core/result.h
    include/hio/core/ring_buffer.h
    include/hio/core/settings.h
    include/hio/core/version.h
    src/app_identity.cpp
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

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
