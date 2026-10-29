# libs/update - the notify-only update check (plan/features/0016, ADR-0116): SemVer 2.0.0
# precedence, the GitHub release list filtered to v2.* tags, one bounded HTTP request.
# Backend library: QtCore + QtNetwork (ADR-0004), static (ADR-0022).

hwio_add_backend_library(hwio_update
    include/hwio/update/releases.h
    include/hwio/update/semver.h
    include/hwio/update/update_check.h
    src/releases.cpp
    src/semver.cpp
    src/update_check.cpp)
add_library(hwio::update ALIAS hwio_update)

target_include_directories(hwio_update PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hwio_update PUBLIC hwio::core Qt6::Core Qt6::Network)
# HWIO_UPDATE_URL is honoured only in builds with HWIO_TEST_OVERRIDES (like HWIO_CATALOG_URL).
target_compile_definitions(hwio_update PRIVATE ${HWIO_TEST_OVERRIDES_DEFINE})
# UpdateCheck is a QObject.
set_target_properties(hwio_update PROPERTIES AUTOMOC ON)
hwio_restrict_own_links(hwio_update ALLOW hwio::core)

if(HWIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
