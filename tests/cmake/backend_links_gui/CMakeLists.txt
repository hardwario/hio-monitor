# Configure-only test project for cmake/HioBackendRules.cmake (ADR-0004).
#
# -DHIO_TEST_LINK_GUI=ON  : a backend library links Qt6::Gui transitively (private dependency
#                           of a static library through an interface library) -> configuring
#                           must fail with the backend rule message.
# -DHIO_TEST_LINK_GUI=OFF : the backend library links only Qt6::Core -> configuring succeeds.
#
# -DHIO_TEST_RESTRICTED_LINK=ALLOWED|FORBIDDEN : fake_backend is restricted to hio::fake_core
#                           (hio_restrict_own_links, ADR-0150) and links hio::fake_core (ALLOWED,
#                           configuring succeeds) or also hio::fake_other (FORBIDDEN, must fail).
#
# The Qt targets are stand-ins (interface libraries), so the test does not need a Qt install.
cmake_minimum_required(VERSION 3.28)
project(hio_backend_links_gui LANGUAGES CXX)

option(HIO_TEST_LINK_GUI "Link a GUI Qt module from a backend library" ON)
set(HIO_TEST_RESTRICTED_LINK "OFF" CACHE STRING "OFF, ALLOWED or FORBIDDEN")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake")
include(HioCompilerFlags)
include(HioHardening)
include(HioBackendRules)

add_library(Qt6::Core INTERFACE IMPORTED)
add_library(Qt6::Gui INTERFACE IMPORTED)
target_link_libraries(Qt6::Gui INTERFACE Qt6::Core)

add_library(fake_widgets_helper INTERFACE)
if(HIO_TEST_LINK_GUI)
    target_link_libraries(fake_widgets_helper INTERFACE Qt6::Gui)
else()
    target_link_libraries(fake_widgets_helper INTERFACE Qt6::Core)
endif()

hio_add_backend_library(fake_backend dummy.cpp)
target_link_libraries(fake_backend PRIVATE fake_widgets_helper)

if(NOT HIO_TEST_RESTRICTED_LINK STREQUAL "OFF")
    add_library(hio_fake_core INTERFACE)
    add_library(hio::fake_core ALIAS hio_fake_core)
    add_library(hio_fake_other INTERFACE)
    add_library(hio::fake_other ALIAS hio_fake_other)
    target_link_libraries(fake_backend PUBLIC hio::fake_core)
    if(HIO_TEST_RESTRICTED_LINK STREQUAL "FORBIDDEN")
        target_link_libraries(fake_backend PRIVATE hio::fake_other)
    endif()
    hio_restrict_own_links(fake_backend ALLOW hio::fake_core)
endif()
