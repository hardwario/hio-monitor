#pragma once

// The `hio` command tree (ADR-0017, plan/0006-cli.md). Commands return data or an error and never
// print themselves (except streaming commands, through Context::output).
//
// Command groups live in separate files: selftest.cpp, jlink_commands.cpp (M1),
// chester_commands.cpp (M1), flash_commands.cpp (M2).

#include "hio/core/log.h"
#include "hio/core/result.h"
#include "output.h"

#include <QJsonValue>

#include <CLI/App.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace hio::cli {

struct Context {
    OutputMode mode = OutputMode::Human;
    std::stop_token stop;
    Output* output = nullptr;      // non-owning, for streaming commands; outlives the run
    std::filesystem::path logFile; // empty when logging to stderr only
};

struct CommandResult {
    QJsonValue data;       // "data" of the --json envelope
    std::string humanText; // printed in human mode (may be empty)
    // A streaming command (e.g. console) printed its own envelopes (NDJSON): main() then prints
    // only humanText in human mode and no final envelope with --json.
    bool streamed = false;
    // ADR-0070 (check commands): the command ran but a check failed. main() exits with this
    // error's category; --json prints the error envelope with "data"; human mode prints only
    // humanText (which already explains the failure).
    std::optional<Error> failure = std::nullopt;
};

using Command = std::function<Result<CommandResult>(const Context&)>;

// The command selected by parsing; registration functions store their action here.
struct CommandSlot {
    Command command;
};

struct GlobalOptions {
    bool json = false;
    bool version = false;
    log::Level logLevel = log::Level::Critical;
    std::string jlinkLibrary; // --jlink-library (empty = not given)
    std::string nrfutil;      // --nrfutil (empty = not given; ADR-0016)
};

enum class ParseOutcome : std::uint8_t {
    Run,      // run() executes the selected command
    ShowHelp, // help was requested (or no command given); print helpText()
};

class CliApp {
public:
    CliApp();
    ~CliApp();
    CliApp(const CliApp&) = delete;
    CliApp& operator=(const CliApp&) = delete;
    CliApp(CliApp&&) = delete;
    CliApp& operator=(CliApp&&) = delete;

    // `args` excludes the program name. Errors: `usage.invalid_argument` with CLI11's message.
    [[nodiscard]] Result<ParseOutcome> parse(const std::vector<std::string>& args);

    [[nodiscard]] const GlobalOptions& options() const noexcept { return m_options; }
    [[nodiscard]] std::string helpText() const;

    // Runs the command selected by parse().
    [[nodiscard]] Result<CommandResult> run(const Context& context) const;

private:
    std::unique_ptr<CLI::App> m_app;
    GlobalOptions m_options;
    std::string m_logLevelName;
    CommandSlot m_slot;
};

// "--json" anywhere before a "--" separator (used before parsing succeeds, e.g. for parse errors).
[[nodiscard]] bool jsonRequested(const std::vector<std::string>& args);

} // namespace hio::cli
