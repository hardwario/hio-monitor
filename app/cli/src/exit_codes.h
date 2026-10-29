#pragma once

// CLI exit codes = error category (ADR-0024, plan/0006-cli.md). Public contract for scripts.

#include "hwio/core/error.h"

namespace hwio::cli {

inline constexpr int exitSuccess = 0;

[[nodiscard]] int exitCodeFor(ErrorCategory category) noexcept;

} // namespace hwio::cli
