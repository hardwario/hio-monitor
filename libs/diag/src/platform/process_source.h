#pragma once

// The per-OS process sources (ADR-0073); one of them is compiled in per platform.

#include "hio/diag/process_scan.h"

#include <memory>

namespace hio::diag::platform {

// nullptr on an unsupported OS.
[[nodiscard]] std::shared_ptr<IProcessSource> makeProcessSource();

} // namespace hio::diag::platform
