# libs/mcp_tools - the MCP device tools library hio_mcp_tools (plan/features/0013 C1-C3,
# ADR-0150): IDeviceProvider, the v1 tools (ADR-0099), the resources and the debug_device prompt
# (ADR-0107). Built on hio_mcp (protocol) and the device / firmware backends. The CLI (`hio mcp`)
# and the GUI (plan 0014) provide an IDeviceProvider and call registerTools().
#
# Backend library: QtCore + QtNetwork (through hio_firmware), never a GUI module (ADR-0004).

hio_add_backend_library(hio_mcp_tools
    include/hio/mcp/tools/device_provider.h
    include/hio/mcp/tools/register_tools.h
    include/hio/mcp/tools/tools_context.h
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
add_library(hio::mcp_tools ALIAS hio_mcp_tools)
target_include_directories(hio_mcp_tools PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(hio_mcp_tools PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hio_mcp_tools PUBLIC hio::mcp hio::device hio::firmware Qt6::Core)
# DeviceProviderNotifier, ToolsContext and the tool calls are QObjects.
set_target_properties(hio_mcp_tools PROPERTIES AUTOMOC ON)

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
