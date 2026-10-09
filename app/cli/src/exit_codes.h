#pragma once

// CLI exit codes = error category (ADR-0024, plan/0006-cli.md). Public contract for scripts.

#include "hio/core/error.h"

namespace hio::cli {

inline constexpr int exitSuccess = 0;

[[nodiscard]] int exitCodeFor(ErrorCategory category) noexcept;

} // namespace hio::cli
