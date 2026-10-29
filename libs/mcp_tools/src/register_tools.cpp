#include "hwio/mcp/tools/register_tools.h"

#include "hwio/core/result.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/tools/tools_context.h"
#include "resources.h"
#include "tools/tools.h"

namespace hwio::mcp::tools {

hwio::Status registerTools(hwio::mcp::Registry& registry, ToolsContext& context) {
    for (const auto& add : {&detail::registerDiscoveryTools, &detail::registerConsoleTools,
                            &detail::registerSessionTools, &detail::registerTargetTools,
                            &detail::registerFirmwareTools, &detail::registerResources}) {
        if (auto added = (*add)(registry, context); !added) {
            return added;
        }
    }
    return detail::registerPrompts(registry);
}

} // namespace hwio::mcp::tools
