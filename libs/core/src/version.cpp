#include "hwio/core/version.h"

#include "hwio/core/package_kind.h"
#include "hwio/core/version_generated.h"

#include <QtVersion> // qVersion()

#include <string_view>

namespace hwio {

BuildInfo buildInfo() {
    return BuildInfo{
        .version = generated::version,
        .gitHash = generated::gitHash,
        .compiler = generated::compiler,
        .qtRuntime = std::string_view(qVersion()),
        .qtCompiled = generated::qtCompiled,
        .packageKind = generated::packageKind,
    };
}

} // namespace hwio
