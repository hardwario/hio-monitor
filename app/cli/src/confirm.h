#pragma once

// Confirmation of destructive operations (ADR-0038, plan/features/0003): `erase --all`,
// `lte erase` and `recover`. On a TTY stdin (human mode) an interactive [y/N] question naming what
// will be lost; otherwise --yes is required (usage.confirmation_required, exit 2). --yes never
// changes anything else.

#include "cli_app.h"
#include "hio/core/result.h"

#include <functional>
#include <optional>
#include <string>

namespace hio::cli {

// Asks `question` (it should end with "Continue? [y/N] "). Errors: usage.confirmation_required
// (not interactive and not `yes`), cancelled (the answer was not yes).
// `ask` reads the answer (default: askYesNo on stderr/stdin; tests replace it); `interactive`
// overrides the TTY detection (tests).
[[nodiscard]] Status confirmDestructive(const Context& context, bool yes,
                                        const std::string& question,
                                        const std::function<bool(const std::string&)>& ask = {},
                                        std::optional<bool> interactive = std::nullopt);

} // namespace hio::cli
