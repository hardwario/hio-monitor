#include "hio/mcp/tools/register_tools.h"

#include "hio/core/result.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/tools/tools_context.h"
#include "resources.h"
#include "tools/tools.h"

namespace hio::mcp::tools {

hio::Status registerTools(hio::mcp::Registry& registry, ToolsContext& context) {
    for (const auto& add : {&detail::registerDiscoveryTools, &detail::registerConsoleTools,
                            &detail::registerSessionTools, &detail::registerTargetTools,
                            &detail::registerFirmwareTools, &detail::registerResources}) {
        if (auto added = (*add)(registry, context); !added) {
            return added;
        }
    }
    return detail::registerPrompts(registry);
}

} // namespace hio::mcp::tools
