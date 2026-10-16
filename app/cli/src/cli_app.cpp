#include "cli_app.h"

#include "chester_commands.h"
#include "diag_commands.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/version.h"
#include "jlink_commands.h"
#include "selftest.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hio::cli {

namespace {

const std::map<std::string, log::Level>& logLevels() {
    // py-hardwario compatible names; "success" is an alias of "info".
    static const std::map<std::string, log::Level> levels{
        {"debug", log::Level::Debug},  {"info", log::Level::Info},
        {"success", log::Level::Info}, {"warning", log::Level::Warning},
        {"error", log::Level::Error},  {"critical", log::Level::Critical},
    };
    return levels;
}

Error usageError(std::string message) {
    Error error = makeError(ErrorCode::UsageInvalidArgument);
    error.message = std::move(message);
    error.cause.clear(); // CLI11's message already says what is wrong
    error.suggestion = "Run `hio --help`.";
    return error;
}

} // namespace

CliApp::CliApp()
    : m_app(std::make_unique<CLI::App>(
          "HARDWARIO Monitor command-line interface (console, logs and flashing of HARDWARIO "
          "devices).",
          "hio")),
      m_logLevelName("critical") {
    CLI::App& app = *m_app;
    app.set_help_flag("-h,--help", "Print this help message and exit");
    app.add_flag("--version", m_options.version, "Print the version and exit");
    app.add_flag("--json", m_options.json,
                 "Machine-readable output: one JSON envelope per line on stdout");
    std::vector<std::string> levelNames;
    for (const auto& [name, level] : logLevels()) {
        levelNames.push_back(name);
    }
    app.add_option("--log-level", m_logLevelName,
                   "Log messages on stderr from this level up (the log file always gets all)")
        ->check(CLI::IsMember(levelNames))
        ->capture_default_str()
        ->option_text("{debug,info,success,warning,error,critical}");
    app.add_option("--jlink-library", m_options.jlinkLibrary,
                   "SEGGER J-Link library to use (default: $HIO_JLINK_LIBRARY, the setting "
                   "jlink/libraryPath, or the installed J-Link Software)")
        ->option_text("PATH");
    app.add_option("--nrfutil", m_options.nrfutil,
                   "nRF Util for the modem firmware (default: $HIO_NRFUTIL, the setting "
                   "nrfutil/path, the PATH, or ~/.nrfutil/bin)")
        ->option_text("PATH");
    app.require_subcommand(0, 1);
    app.fallthrough(true);

    registerSelftestCommands(app, m_slot);
    registerJLinkCommands(app, m_slot, m_options);
    registerChesterCommands(app, m_slot, m_options);
    registerDiagCommands(app, m_slot, m_options);
}

CliApp::~CliApp() = default;

Result<ParseOutcome> CliApp::parse(const std::vector<std::string>& args) {
    std::vector<std::string> reversed(args.rbegin(), args.rend()); // CLI11 consumes from the back
    try {
        m_app->parse(reversed);
    } catch (const CLI::CallForHelp&) {
        return ParseOutcome::ShowHelp;
    } catch (const CLI::CallForAllHelp&) {
        return ParseOutcome::ShowHelp;
    } catch (const CLI::ParseError& error) {
        return std::unexpected(usageError(error.what()));
    }
    m_options.logLevel = logLevels().at(m_logLevelName);
    if (!m_options.version && !m_slot.command) {
        return ParseOutcome::ShowHelp;
    }
    return ParseOutcome::Run;
}

std::string CliApp::helpText() const { return m_app->help(); }

Result<CommandResult> CliApp::run(const Context& context) const {
    if (m_options.version) {
        const BuildInfo info = buildInfo();
        QJsonObject data;
        data.insert(QStringLiteral("name"), QStringLiteral("hio"));
        data.insert(QStringLiteral("version"), toQString(info.version));
        data.insert(QStringLiteral("gitHash"), toQString(info.gitHash));
        data.insert(QStringLiteral("qt"), toQString(info.qtRuntime));
        data.insert(QStringLiteral("compiler"), toQString(info.compiler));
        return CommandResult{.data = data, .humanText = std::format("hio {}", info.version)};
    }
    if (!m_slot.command) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "no command selected"));
    }
    return m_slot.command(context);
}

bool jsonRequested(const std::vector<std::string>& args) {
    const auto end = std::ranges::find(args, "--");
    return std::find(args.begin(), end, "--json") != end;
}

} // namespace hio::cli
