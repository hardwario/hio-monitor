// `hwio` - HARDWARIO Monitor command-line interface (ADR-0017, plan/0006-cli.md).
//
// English only (ADR-0027): no translator is ever installed; numbers use the C locale.
// Exceptions never leave main(): the whole body is wrapped at this top boundary (ADR-0007).

#include "cli_app.h"
#include "exit_codes.h"
#include "hwio/core/app_identity.h"
#include "hwio/core/crash_handler.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/version.h"
#include "output.h"
#include "signals.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkProxyFactory>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QtContainerFwd>
#include <QtTypes>

#include <clocale>
#include <cstddef>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace {

using hwio::cli::OutputMode;

// Used only when everything else failed (e.g. an exception before Output exists).
void printLastResortError(bool json) {
    try {
        hwio::cli::Output output(json ? OutputMode::Json : OutputMode::Human, std::cout, std::cerr,
                                 false);
        output.printError(hwio::makeError(hwio::ErrorCode::InternalUnexpected));
    } catch (...) {
        std::cerr << "Error: An unexpected internal error occurred.\n";
    }
}

int run(int& argc, char** argv) {
    QCoreApplication app(argc, argv);
    hwio::initApplicationIdentity();
    // C locale for numbers (QCoreApplication applies the user's locale on Unix).
    std::setlocale(LC_NUMERIC, "C");
    QLocale::setDefault(QLocale::c());
    // Plan 0016: downloads and the update check use the system proxy settings.
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    // Arguments from Qt are proper Unicode on every OS; CLI11 gets them as UTF-8.
    std::vector<std::string> args;
    const QStringList arguments = QCoreApplication::arguments();
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        args.push_back(hwio::toStdString(arguments.at(i)));
    }

    hwio::cli::CliApp cli;
    const auto parsed = cli.parse(args);
    // CLI11 stops at a parse error or --help before all flags are stored; scan the arguments then.
    const bool json = parsed && *parsed == hwio::cli::ParseOutcome::Run
                          ? cli.options().json
                          : hwio::cli::jsonRequested(args);
    const OutputMode mode = json ? OutputMode::Json : OutputMode::Human;
    const hwio::cli::StderrColor stderrColor;
    hwio::cli::Output output(mode, std::cout, std::cerr, stderrColor.enabled());

    if (!parsed) {
        output.printError(parsed.error());
        return hwio::cli::exitCodeFor(hwio::categoryOf(parsed.error().code));
    }
    if (*parsed == hwio::cli::ParseOutcome::ShowHelp) {
        const std::string help = cli.helpText();
        output.printResult(QJsonObject{{QStringLiteral("help"), hwio::toQString(help)}}, help);
        return hwio::cli::exitSuccess;
    }

    auto logging = hwio::log::Logging::init(
        hwio::log::AppKind::Cli, {.stderrLevel = cli.options().logLevel, .logDir = std::nullopt});
    if (!logging) {
        output.printError(logging.error());
        return hwio::cli::exitCodeFor(hwio::categoryOf(logging.error().code));
    }

    std::stop_source stop;
    const hwio::cli::SignalGuard signalGuard(stop);
    const hwio::cli::Context context{.mode = mode,
                                     .stop = stop.get_token(),
                                     .output = &output,
                                     .logFile = (*logging)->currentFile()};
    auto result = cli.run(context);
    if (!result) {
        hwio::Error error = std::move(result).error();
        if (hwio::categoryOf(error.code) == hwio::ErrorCategory::Internal &&
            !context.logFile.empty()) {
            const std::string logLine = std::format("Log file: {}", hwio::toUtf8(context.logFile));
            error.detail = error.detail.empty() ? logLine : error.detail + "; " + logLine;
        }
        output.printError(error);
        return hwio::cli::exitCodeFor(hwio::categoryOf(error.code));
    }
    if (result->streamed) {
        if (mode == OutputMode::Human && !result->humanText.empty()) {
            std::cout << result->humanText << '\n' << std::flush;
        }
        return hwio::cli::exitSuccess;
    }
    if (result->failure) {
        output.printFailure(*result->failure, result->data, result->humanText);
        return hwio::cli::exitCodeFor(hwio::categoryOf(result->failure->code));
    }
    output.printResult(result->data, result->humanText);
    return hwio::cli::exitSuccess;
}

} // namespace

int main(int argc, char** argv) {
    bool json = false;
    try {
        // ADR-0123: local crash files, first thing (the identity gives the data directory).
        hwio::initApplicationIdentity();
        const hwio::BuildInfo build = hwio::buildInfo();
        const std::string version = std::format("{} (git {})", build.version, build.gitHash);
        if (auto crash = hwio::crash::install({.directory = hwio::crash::defaultDirectory(),
                                               .appName = "hwio",
                                               .version = version});
            !crash) {
            std::cerr << "hwio: crash handler not installed: " << crash.error().detail << '\n';
        }
        // Raw argv, only to choose the format of the last-resort error below.
        const std::span<char*> rawArgs(argv, static_cast<std::size_t>(argc));
        if (rawArgs.size() > 1) {
            json = hwio::cli::jsonRequested(
                std::vector<std::string>(rawArgs.begin() + 1, rawArgs.end()));
        }
        return run(argc, argv);
    } catch (...) {
        printLastResortError(json);
        return hwio::cli::exitCodeFor(hwio::ErrorCategory::Internal);
    }
}
