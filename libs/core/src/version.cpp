#include "hio/core/version.h"

#include "hio/core/version_generated.h"

#include <QtVersion> // qVersion()

#include <string_view>

namespace hio {

BuildInfo buildInfo() {
    return BuildInfo{
        .version = generated::version,
        .gitHash = generated::gitHash,
        .compiler = generated::compiler,
        .qtRuntime = std::string_view(qVersion()),
        .qtCompiled = generated::qtCompiled,
    };
}

} // namespace hio
