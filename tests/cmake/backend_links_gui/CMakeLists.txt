# Configure-only test project for cmake/HWioBackendRules.cmake (ADR-0004).
#
# -DHWIO_TEST_LINK_GUI=ON  : a backend library links Qt6::Gui transitively (private dependency
#                           of a static library through an interface library) -> configuring
#                           must fail with the backend rule message.
# -DHWIO_TEST_LINK_GUI=OFF : the backend library links only Qt6::Core -> configuring succeeds.
#
# -DHWIO_TEST_RESTRICTED_LINK=ALLOWED|FORBIDDEN : fake_backend is restricted to hwio::fake_core
#                           (hwio_restrict_own_links, ADR-0150) and links hwio::fake_core (ALLOWED,
#                           configuring succeeds) or also hwio::fake_other (FORBIDDEN, must fail).
#
# The Qt targets are stand-ins (interface libraries), so the test does not need a Qt install.
cmake_minimum_required(VERSION 3.28)
project(hwio_backend_links_gui LANGUAGES CXX)

option(HWIO_TEST_LINK_GUI "Link a GUI Qt module from a backend library" ON)
set(HWIO_TEST_RESTRICTED_LINK "OFF" CACHE STRING "OFF, ALLOWED or FORBIDDEN")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake")
include(HWioCompilerFlags)
include(HWioHardening)
include(HWioBackendRules)

add_library(Qt6::Core INTERFACE IMPORTED)
add_library(Qt6::Gui INTERFACE IMPORTED)
target_link_libraries(Qt6::Gui INTERFACE Qt6::Core)

add_library(fake_widgets_helper INTERFACE)
if(HWIO_TEST_LINK_GUI)
    target_link_libraries(fake_widgets_helper INTERFACE Qt6::Gui)
else()
    target_link_libraries(fake_widgets_helper INTERFACE Qt6::Core)
endif()

hwio_add_backend_library(fake_backend dummy.cpp)
target_link_libraries(fake_backend PRIVATE fake_widgets_helper)

if(NOT HWIO_TEST_RESTRICTED_LINK STREQUAL "OFF")
    add_library(hwio_fake_core INTERFACE)
    add_library(hwio::fake_core ALIAS hwio_fake_core)
    add_library(hwio_fake_other INTERFACE)
    add_library(hwio::fake_other ALIAS hwio_fake_other)
    target_link_libraries(fake_backend PUBLIC hwio::fake_core)
    if(HWIO_TEST_RESTRICTED_LINK STREQUAL "FORBIDDEN")
        target_link_libraries(fake_backend PRIVATE hwio::fake_other)
    endif()
    hwio_restrict_own_links(fake_backend ALLOW hwio::fake_core)
endif()
