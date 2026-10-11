#include "confirm.h"

#include "cli_app.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "output.h"
#include "terminal.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace hio::cli {

Status confirmDestructive(const Context& context, bool yes, const std::string& question,
                          const std::function<bool(const std::string&)>& ask,
                          std::optional<bool> interactive) {
    if (yes) {
        return {};
    }
    const bool canAsk =
        interactive.value_or(context.mode == OutputMode::Human && stdinIsTerminal());
    if (!canAsk) {
        // The question without its "Continue? [y/N]" line says what would be lost.
        const std::size_t end = question.find('\n');
        Error error = makeError(ErrorCode::UsageConfirmationRequired, question.substr(0, end));
        return std::unexpected(std::move(error));
    }
    const bool confirmed = ask ? ask(question) : askYesNo(question);
    if (!confirmed) {
        return std::unexpected(makeError(ErrorCode::Cancelled, "not confirmed"));
    }
    return {};
}

} // namespace hio::cli
