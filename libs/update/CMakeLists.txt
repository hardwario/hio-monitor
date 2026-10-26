# libs/update - the notify-only update check (plan/features/0016, ADR-0116): SemVer 2.0.0
# precedence, the GitHub release list filtered to v2.* tags, one bounded HTTP request.
# Backend library: QtCore + QtNetwork (ADR-0004), static (ADR-0022).

hio_add_backend_library(hio_update
    include/hio/update/releases.h
    include/hio/update/semver.h
    include/hio/update/update_check.h
    src/releases.cpp
    src/semver.cpp
    src/update_check.cpp)
add_library(hio::update ALIAS hio_update)

target_include_directories(hio_update PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hio_update PUBLIC hio::core Qt6::Core Qt6::Network)
# HIO_UPDATE_URL is honoured only in builds with HIO_TEST_OVERRIDES (like HIO_CATALOG_URL).
target_compile_definitions(hio_update PRIVATE ${HIO_TEST_OVERRIDES_DEFINE})
# UpdateCheck is a QObject.
set_target_properties(hio_update PROPERTIES AUTOMOC ON)
hio_restrict_own_links(hio_update ALLOW hio::core)

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
