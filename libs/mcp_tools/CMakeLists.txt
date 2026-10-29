# libs/mcp_tools - the MCP device tools library hwio_mcp_tools (plan/features/0013 C1-C3,
# ADR-0150): IDeviceProvider, the v1 tools (ADR-0099), the resources and the debug_device prompt
# (ADR-0107). Built on hwio_mcp (protocol) and the device / firmware backends. The CLI (`hwio mcp`)
# and the GUI (plan 0014) provide an IDeviceProvider and call registerTools().
#
# Backend library: QtCore + QtNetwork (through hwio_firmware), never a GUI module (ADR-0004).

hwio_add_backend_library(hwio_mcp_tools
    include/hwio/mcp/tools/device_provider.h
    include/hwio/mcp/tools/register_tools.h
    include/hwio/mcp/tools/tools_context.h
    src/prompts.cpp
    src/register_tools.cpp
    src/resources.cpp
    src/resources.h
    src/scan_scheduler.cpp
    src/scan_scheduler.h
    src/tool_support.cpp
    src/tool_support.h
    src/tools_context.cpp
    src/tools/console_tools.cpp
    src/tools/discovery_tools.cpp
    src/tools/firmware_tools.cpp
    src/tools/session_tools.cpp
    src/tools/target_tools.cpp
    src/tools/tools.h)
add_library(hwio::mcp_tools ALIAS hwio_mcp_tools)
target_include_directories(hwio_mcp_tools PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(hwio_mcp_tools PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hwio_mcp_tools PUBLIC hwio::mcp hwio::device hwio::firmware Qt6::Core)
# DeviceProviderNotifier, ToolsContext and the tool calls are QObjects.
set_target_properties(hwio_mcp_tools PROPERTIES AUTOMOC ON)

if(HWIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
