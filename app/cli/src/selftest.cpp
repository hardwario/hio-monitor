#include "selftest.h"

#include "cli_app.h"
#include "cli_log.h"
#include "hio/core/error.h"
#include "hio/core/result.h"

#include <QJsonValue>
#include <QLoggingCategory>

#include <CLI/CLI.hpp>

#include <condition_variable>
#include <expected>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace hio::cli {

namespace {

Result<CommandResult> selftestError(const std::string& id) {
    const auto code = errorCodeFromId(id);
    if (!code) {
        Error error = makeError(ErrorCode::UsageInvalidArgument,
                                std::format("_selftest error: unknown error id '{}'", id));
        error.message = std::format("Unknown error id '{}'.", id);
        error.suggestion = "Use an id from the error table, e.g. io.file_not_found.";
        return std::unexpected(std::move(error));
    }
    return std::unexpected(makeError(*code));
}

Result<CommandResult> selftestWait(const Context& context) {
    qCDebug(lcCli) << "_selftest wait: waiting for cancellation (Ctrl+C / SIGTERM)";
    std::mutex mutex;
    std::condition_variable_any wakeUp;
    std::unique_lock lock(mutex);
    wakeUp.wait(lock, context.stop, [] { return false; });
    qCDebug(lcCli) << "_selftest wait: cancelled";
    return fail(ErrorCode::Cancelled);
}

} // namespace

void registerSelftestCommands(CLI::App& app, CommandSlot& slot) {
    CLI::App* selftest = app.add_subcommand("_selftest", "Internal self-tests");
    selftest->group(""); // hidden from --help
    selftest->require_subcommand(1);

    CLI::App* error = selftest->add_subcommand("error", "Fail with the given error id");
    auto id = std::make_shared<std::string>();
    error->add_option("id", *id, "Error id, e.g. io.file_not_found")->required();
    error->callback([&slot, id] {
        slot.command = [id](const Context& /*context*/) { return selftestError(*id); };
    });

    CLI::App* wait = selftest->add_subcommand("wait", "Block until cancelled");
    wait->callback([&slot] { slot.command = selftestWait; });
}

} // namespace hio::cli
