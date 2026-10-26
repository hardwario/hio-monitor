// `hio` - HARDWARIO Monitor command-line interface (ADR-0017, plan/0006-cli.md).
//
// English only (ADR-0027): no translator is ever installed; numbers use the C locale.
// Exceptions never leave main(): the whole body is wrapped at this top boundary (ADR-0007).

#include "cli_app.h"
#include "exit_codes.h"
#include "hio/core/app_identity.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "output.h"
#include "signals.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLocale>
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

using hio::cli::OutputMode;

// Used only when everything else failed (e.g. an exception before Output exists).
void printLastResortError(bool json) {
    try {
        hio::cli::Output output(json ? OutputMode::Json : OutputMode::Human, std::cout, std::cerr,
                                false);
        output.printError(hio::makeError(hio::ErrorCode::InternalUnexpected));
    } catch (...) {
        std::cerr << "Error: An unexpected internal error occurred.\n";
    }
}

int run(int& argc, char** argv) {
    QCoreApplication app(argc, argv);
    hio::initApplicationIdentity();
    // C locale for numbers (QCoreApplication applies the user's locale on Unix).
    std::setlocale(LC_NUMERIC, "C");
    QLocale::setDefault(QLocale::c());

    // Arguments from Qt are proper Unicode on every OS; CLI11 gets them as UTF-8.
    std::vector<std::string> args;
    const QStringList arguments = QCoreApplication::arguments();
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        args.push_back(hio::toStdString(arguments.at(i)));
    }

    hio::cli::CliApp cli;
    const auto parsed = cli.parse(args);
    // CLI11 stops at a parse error or --help before all flags are stored; scan the arguments then.
    const bool json = parsed && *parsed == hio::cli::ParseOutcome::Run
                          ? cli.options().json
                          : hio::cli::jsonRequested(args);
    const OutputMode mode = json ? OutputMode::Json : OutputMode::Human;
    const hio::cli::StderrColor stderrColor;
    hio::cli::Output output(mode, std::cout, std::cerr, stderrColor.enabled());

    if (!parsed) {
        output.printError(parsed.error());
        return hio::cli::exitCodeFor(hio::categoryOf(parsed.error().code));
    }
    if (*parsed == hio::cli::ParseOutcome::ShowHelp) {
        const std::string help = cli.helpText();
        output.printResult(QJsonObject{{QStringLiteral("help"), hio::toQString(help)}}, help);
        return hio::cli::exitSuccess;
    }

    auto logging = hio::log::Logging::init(
        hio::log::AppKind::Cli, {.stderrLevel = cli.options().logLevel, .logDir = std::nullopt});
    if (!logging) {
        output.printError(logging.error());
        return hio::cli::exitCodeFor(hio::categoryOf(logging.error().code));
    }

    std::stop_source stop;
    const hio::cli::SignalGuard signalGuard(stop);
    const hio::cli::Context context{.mode = mode,
                                    .stop = stop.get_token(),
                                    .output = &output,
                                    .logFile = (*logging)->currentFile()};
    auto result = cli.run(context);
    if (!result) {
        hio::Error error = std::move(result).error();
        if (hio::categoryOf(error.code) == hio::ErrorCategory::Internal &&
            !context.logFile.empty()) {
            const std::string logLine = std::format("Log file: {}", hio::toUtf8(context.logFile));
            error.detail = error.detail.empty() ? logLine : error.detail + "; " + logLine;
        }
        output.printError(error);
        return hio::cli::exitCodeFor(hio::categoryOf(error.code));
    }
    if (result->streamed) {
        if (mode == OutputMode::Human && !result->humanText.empty()) {
            std::cout << result->humanText << '\n' << std::flush;
        }
        return hio::cli::exitSuccess;
    }
    if (result->failure) {
        output.printFailure(*result->failure, result->data, result->humanText);
        return hio::cli::exitCodeFor(hio::categoryOf(result->failure->code));
    }
    output.printResult(result->data, result->humanText);
    return hio::cli::exitSuccess;
}

} // namespace

int main(int argc, char** argv) {
    bool json = false;
    try {
        // Raw argv, only to choose the format of the last-resort error below.
        const std::span<char*> rawArgs(argv, static_cast<std::size_t>(argc));
        if (rawArgs.size() > 1) {
            json = hio::cli::jsonRequested(
                std::vector<std::string>(rawArgs.begin() + 1, rawArgs.end()));
        }
        return run(argc, argv);
    } catch (...) {
        printLastResortError(json);
        return hio::cli::exitCodeFor(hio::ErrorCategory::Internal);
    }
}
