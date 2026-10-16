#include "diag_commands.h"

#include "cli_app.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/device/session_registry.h"
#include "hio/diag/check.h"
#include "hio/diag/environment.h"
#include "hio/diag/report.h"
#include "jlink_commands.h"
#include "output.h"
#include "probe_options.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QtCore/qnamespace.h>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::cli {

namespace {

namespace fs = std::filesystem;
using diag::CheckResult;
using diag::CheckStatus;

constexpr std::string_view profileId = "chester";

std::optional<fs::path> pathOption(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    return fs::path(toQString(text).toStdU16String());
}

QJsonObject checkJson(const CheckResult& result) {
    QJsonObject json;
    json.insert(QStringLiteral("id"), toQString(result.id));
    json.insert(QStringLiteral("status"), toQString(diag::checkStatusId(result.status)));
    json.insert(QStringLiteral("title"), toQString(result.title));
    json.insert(QStringLiteral("result"), toQString(result.result));
    json.insert(QStringLiteral("error"),
                result.error ? QJsonValue(toJson(*result.error)) : QJsonValue(QJsonValue::Null));
    return json;
}

QJsonObject checksData(std::span<const CheckResult> results) {
    QJsonArray checks;
    for (const CheckResult& result : results) {
        checks.append(checkJson(result));
    }
    return QJsonObject{{QStringLiteral("checks"), checks}};
}

// The failed checks' message, cause and suggestion (ADR-0070, human output).
std::string failureDetails(std::span<const CheckResult> results) {
    std::string text;
    for (const CheckResult& result : results) {
        if (result.status != CheckStatus::Failed || !result.error) {
            continue;
        }
        text += std::format("\n{}: {}\n", result.title, result.error->message);
        if (!result.error->cause.empty()) {
            text += std::format("  Cause: {}\n", result.error->cause);
        }
        if (!result.error->suggestion.empty()) {
            text += std::format("  What to do: {}\n", result.error->suggestion);
        }
        if (!result.error->detail.empty()) {
            text += std::format("  Details: {}\n", result.error->detail);
        }
    }
    return text;
}

std::optional<Error> firstFailure(std::span<const CheckResult> results) {
    for (const CheckResult& result : results) {
        if (result.status == CheckStatus::Failed && result.error) {
            return result.error;
        }
    }
    return std::nullopt;
}

std::size_t countStatus(std::span<const CheckResult> results, CheckStatus status) {
    std::size_t count = 0;
    for (const CheckResult& result : results) {
        count += result.status == status ? 1 : 0;
    }
    return count;
}

std::string countsLine(std::span<const CheckResult> results) {
    const std::size_t failed = countStatus(results, CheckStatus::Failed);
    const std::size_t warnings = countStatus(results, CheckStatus::Warning);
    std::string text;
    if (failed > 0) {
        text += std::format("{} failed", failed);
    }
    if (warnings > 0) {
        text += std::format("{}{} warning{}", text.empty() ? "" : ", ", warnings,
                            warnings == 1 ? "" : "s");
    }
    return text.empty() ? std::string("All checks passed") : text;
}

diag::EnvironmentContext environment(const GlobalOptions& globals) {
    return diag::defaultEnvironment(pathOption(globals.jlinkLibrary), pathOption(globals.nrfutil));
}

// ---- doctor ------------------------------------------------------------------------------------

Result<CommandResult> doctor(const Context& context, const GlobalOptions& globals) {
    const diag::EnvironmentContext environmentContext = environment(globals);
    const std::vector<CheckResult> results =
        diag::CheckRunner::run(diag::environmentChecks(environmentContext), context.stop);
    if (context.stop.stop_requested()) {
        return fail(ErrorCode::Cancelled);
    }
    std::string text = diag::formatCheckLines(results);
    text += failureDetails(results);
    text += std::format("\n{}. Run `hio diag report` to create a report for HARDWARIO support.",
                        countsLine(results));
    CommandResult result{.data = checksData(results),
                         .humanText = std::move(text),
                         .streamed = false,
                         .failure = firstFailure(results)};
    return result;
}

// ---- diag report -------------------------------------------------------------------------------

struct ReportOptions {
    std::string output;
};

Result<CommandResult> report(const Context& context, const GlobalOptions& globals,
                             const ReportOptions& options) {
    if (options.output == "-") {
        Error error = makeError(ErrorCode::UsageInvalidArgument, "-o -");
        error.message = "The report is a zip file and cannot be written to the terminal.";
        error.suggestion = "Give a file name with -o, or leave -o out.";
        return std::unexpected(std::move(error));
    }
    const fs::path target = options.output.empty()
                                ? fs::path(diag::defaultReportFileName())
                                : fs::path(toQString(options.output).toStdU16String());
    diag::EnvironmentContext environmentContext = environment(globals);
    environmentContext.facts = std::make_shared<diag::EnvironmentFacts>();
    const std::vector<CheckResult> results =
        diag::CheckRunner::run(diag::environmentChecks(environmentContext), context.stop);
    if (context.stop.stop_requested()) {
        return fail(ErrorCode::Cancelled);
    }
    diag::ReportInput input;
    input.doctor = results;
    input.app = diag::appInfoJson("en");
    const QJsonObject tools = diag::toJson(*environmentContext.facts);
    input.tools = tools;
    input.probes = tools.value(QStringLiteral("probes")).toArray();
    input.logFiles = diag::recentLogFiles(log::defaultLogDirectory());
    input.settingsFile = Settings::defaultFilePath();
    input.createdUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    input.redaction = diag::currentRedaction();
    auto bytes = diag::ReportBuilder::writeZip(input, target, context.stop);
    if (!bytes) {
        return std::unexpected(std::move(bytes).error());
    }
    std::error_code error;
    const fs::path absolute = fs::absolute(target, error);
    const std::string path = toUtf8(error ? target : absolute);
    // The path and the first lines of the summary.
    const std::string summary = diag::ReportBuilder::summaryText(input);
    std::string head;
    std::size_t lines = 0;
    for (std::size_t i = 0; i < summary.size() && lines < 8; ++i) {
        head += summary[i];
        lines += summary[i] == '\n' ? 1 : 0;
    }
    QJsonObject data;
    data.insert(QStringLiteral("path"), toQString(path));
    data.insert(QStringLiteral("bytes"), static_cast<double>(*bytes));
    return CommandResult{
        .data = data,
        .humanText = std::format("Diagnostic report written: {} ({} bytes)\n\n{}...\n\nThe report "
                                 "stays on your computer; attach it to your support request.",
                                 path, *bytes, head),
        .streamed = false,
        .failure = std::nullopt};
}

// ---- chester app|lte check ---------------------------------------------------------------------

Result<CommandResult> check(const Context& context, const GlobalOptions& globals,
                            const ProbeOptions& group, const ProbeOptions& own,
                            std::string_view targetId) {
    const device::DeviceProfile& profile = *device::ProfileRegistry::find(profileId);
    const device::TargetProfile& target = *device::findTarget(profile, targetId);
    auto location = locateJLinkLibrary(globals);
    if (!location) {
        return std::unexpected(std::move(location).error());
    }
    device::SessionOptions options;
    options.profile = &profile;
    options.targetId = targetId;
    options.probeSn = serialNumberOf(group, own);
    auto speed = speedOf(group, own, target.defaultSpeedKhz);
    if (!speed) {
        return std::unexpected(std::move(speed).error());
    }
    options.speedKhz = *speed;
    options.latencyMs = target.defaultLatencyMs;
    options.attachRtt = false;
    options.connectTarget = false; // ADR-0069: probe-only session
    options.consoleFile = fs::path();
    if (const auto warning = device::slowSpeedWarning(*speed);
        warning && context.mode == OutputMode::Human) {
        std::cerr << "Warning: " << *warning << '\n';
    }
    device::SessionRegistry registry;
    auto session = device::DeviceSession::open(registry, *location, std::move(options));
    if (!session) {
        return std::unexpected(std::move(session).error());
    }
    device::DeviceSession& device = **session;

    std::vector<CheckResult> results;
    const std::vector<device::DiagnoseStepInfo> infos = device::diagnoseSteps(target);
    std::size_t width = 0;
    for (const device::DiagnoseStepInfo& info : infos) {
        width = std::max(width, static_cast<std::size_t>(toQString(info.title).size()));
    }
    const auto titleOf = [&infos](const std::string& id) {
        for (const device::DiagnoseStepInfo& info : infos) {
            if (info.id == id) {
                return info.title;
            }
        }
        return id;
    };
    QObject receiver;
    QObject::connect(&device, &device::DeviceSession::diagnoseStep, &receiver,
                     [&](device::OperationId /*id*/, const device::DiagnoseStep& step) {
                         CheckResult result{.id = step.id,
                                            .status = step.status,
                                            .title = titleOf(step.id),
                                            .result = step.result,
                                            .error = step.error};
                         QJsonObject event = checkJson(result);
                         event.insert(QStringLiteral("type"), QStringLiteral("check"));
                         context.output->printEvent(
                             event, diag::formatCheckLines(std::span(&result, 1), width));
                         results.push_back(std::move(result));
                     });
    auto id = device.diagnose(targetId);
    if (!id) {
        device.close();
        return std::unexpected(std::move(id).error());
    }
    device::OperationResult outcome = device::waitForOperation(device, *id, {}, context.stop);
    device.close();
    if (auto real = device.finishedResult(*id)) {
        outcome = std::move(*real);
    }
    if (!outcome.status) {
        return std::unexpected(outcome.status.error());
    }
    // Steps whose signal was not delivered before close() (none normally): from the result.
    for (std::size_t i = results.size(); i < outcome.checks.size(); ++i) {
        const device::DiagnoseStep& step = outcome.checks.at(i);
        results.push_back(CheckResult{.id = step.id,
                                      .status = step.status,
                                      .title = titleOf(step.id),
                                      .result = step.result,
                                      .error = step.error});
    }
    std::string text = failureDetails(results);
    text += std::format("\n{}.", countsLine(results));
    return CommandResult{.data = checksData(results),
                         .humanText = std::move(text),
                         .streamed = false,
                         .failure = firstFailure(results)};
}

} // namespace

void registerDiagCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals) {
    CLI::App* doctorCommand = app.add_subcommand(
        "doctor", "Check the J-Link Software, nRF Util, USB access, J-Link probes and folders");
    doctorCommand->callback([&slot, &globals] {
        slot.command = [&globals](const Context& context) { return doctor(context, globals); };
    });

    CLI::App* diagGroup = app.add_subcommand("diag", "Diagnostics for HARDWARIO support");
    diagGroup->require_subcommand(1);
    auto reportOptions = std::make_shared<ReportOptions>();
    CLI::App* reportCommand = diagGroup->add_subcommand(
        "report", "Write a diagnostic report (zip: summary, recent logs, settings; the user name, "
                  "home folder and host name are replaced)");
    reportCommand
        ->add_option("-o,--output", reportOptions->output,
                     "Report file (default: ./hio-diagnostics-YYYYMMDD-HHMMSS.zip)")
        ->option_text("FILE");
    reportCommand->callback([&slot, &globals, reportOptions] {
        slot.command = [&globals, reportOptions](const Context& context) {
            return report(context, globals, *reportOptions);
        };
    });
}

void registerCheckCommand(CLI::App& targetGroup, const std::shared_ptr<ProbeOptions>& group,
                          CommandSlot& slot, const GlobalOptions& globals,
                          std::string_view targetId) {
    auto own = std::make_shared<ProbeOptions>();
    CLI::App* checkCommand = targetGroup.add_subcommand(
        "check", targetId == "app"
                     ? "Check the connection to the application MCU step by step (probe, power, "
                       "connection, MCU type, console); never writes the device"
                     : "Check the connection to the LTE MCU step by step (probe, power, "
                       "connection, MCU type); never writes the device");
    addProbeOptions(*checkCommand, *own);
    checkCommand->callback([&slot, &globals, group, own, targetId] {
        slot.command = [&globals, group, own, targetId](const Context& context) {
            return check(context, globals, *group, *own, targetId);
        };
    });
}

} // namespace hio::cli
