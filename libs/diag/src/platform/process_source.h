#pragma once

// The per-OS process sources (ADR-0073); one of them is compiled in per platform.

#include "hwio/diag/process_scan.h"

#include <memory>

namespace hwio::diag::platform {

// nullptr on an unsupported OS.
[[nodiscard]] std::shared_ptr<IProcessSource> makeProcessSource();

} // namespace hwio::diag::platform
