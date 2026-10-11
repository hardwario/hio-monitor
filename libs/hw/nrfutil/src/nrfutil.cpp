#include "hio/hw/nrfutil/nrfutil.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"
#include "hio/firmware/program_progress.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QtContainerFwd>
#include <QtCore/qnamespace.h>
#include <QtEnvironmentVariables>
#include <QtLogging>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <regex>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::nrfutil {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

#ifdef _WIN32
constexpr const char* executableName = "nrfutil.exe";
#else
constexpr const char* executableName = "nrfutil";
#endif

// Bounds of the captured output (plan/features/0003 Bounds).
constexpr std::size_t tailBytes = 4096;            // kept for `detail`
constexpr std::size_t maxLineBytes = 64ULL * 1024; // longer NDJSON lines are skipped

std::optional<fs::path> nonEmpty(std::optional<fs::path> path) {
    if (path && path->empty()) {
        return std::nullopt;
    }
    return path;
}

QString toQ(const fs::path& path) { return toQString(toUtf8(path)); }

Error notFound(std::string detail) {
    return makeError(ErrorCode::DependencyNrfUtilNotFound, std::move(detail));
}

Result<fs::path> useOverride(const fs::path& path, std::string_view source) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return std::unexpected(notFound(std::format("{} {} does not exist", source, toUtf8(path))));
    }
    qCInfo(log::lcFlash).noquote() << "nRF Util from" << toQString(source) << toQ(path);
    return path;
}

// One stream of a child process: counts everything, keeps the last tailBytes, splits lines for
// the parser (until maxBytes were seen; the rest is dropped and counted).
class StreamCapture {
public:
    explicit StreamCapture(std::size_t maxBytes) : m_maxBytes(maxBytes) {}

    template <class LineFn> void append(const QByteArray& chunk, const LineFn& onLine) {
        for (const char c : chunk) {
            ++m_seen;
            if (m_seen > m_maxBytes) {
                ++m_dropped;
                continue;
            }
            m_tail.push_back(c);
            if (c == '\n') {
                if (!m_lineTooLong) {
                    onLine(m_line);
                }
                m_line.clear();
                m_lineTooLong = false;
            } else if (m_line.size() < maxLineBytes) {
                m_line.push_back(c);
            } else {
                m_lineTooLong = true;
            }
        }
        if (m_tail.size() > 2 * tailBytes) {
            m_tail.erase(0, m_tail.size() - tailBytes);
        }
    }

    [[nodiscard]] std::string tail() const {
        std::string text =
            m_tail.size() > tailBytes ? m_tail.substr(m_tail.size() - tailBytes) : m_tail;
        if (m_dropped > 0) {
            text += std::format(" [{} bytes dropped]", m_dropped);
        }
        return text;
    }

private:
    std::size_t m_maxBytes;
    std::uint64_t m_seen = 0;
    std::uint64_t m_dropped = 0;
    std::string m_tail;
    std::string m_line;
    bool m_lineTooLong = false;
};

struct ProcessOutput {
    int exitCode = -1;
    bool crashed = false;
    std::string out;
    std::string err;
};

// Runs `executable args` to completion (short probes such as --version).
Result<ProcessOutput> runShort(const fs::path& executable, const QStringList& arguments,
                               std::chrono::milliseconds timeout) {
    QProcess process;
    process.setProgram(toQ(executable));
    process.setArguments(arguments);
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(static_cast<int>(timeout.count()))) {
        return std::unexpected(notFound(std::format("{} cannot be started: {}", toUtf8(executable),
                                                    process.errorString().toStdString())));
    }
    if (!process.waitForFinished(static_cast<int>(timeout.count()))) {
        process.kill();
        process.waitForFinished();
        return std::unexpected(makeError(
            ErrorCode::Timeout,
            std::format("{} {} did not finish within {} ms", toUtf8(executable),
                        arguments.join(QLatin1Char(' ')).toStdString(), timeout.count())));
    }
    constexpr qint64 maxProbeBytes = qint64{64} * 1024;
    return ProcessOutput{.exitCode = process.exitCode(),
                         .crashed = process.exitStatus() == QProcess::CrashExit,
                         .out = process.read(maxProbeBytes).toStdString(),
                         .err = process.readAllStandardError().left(maxProbeBytes).toStdString()};
}

std::string firstLine(const std::string& text) {
    const std::size_t end = text.find_first_of("\r\n");
    return text.substr(0, end);
}

} // namespace

// ---- locating -------------------------------------------------------------------------------

SettingKey<fs::path> pathSetting() {
    return SettingKey<fs::path>{.path = "nrfutil/path", .defaultValue = fs::path()};
}

LocatorOptions defaultLocatorOptions(std::optional<fs::path> option, const Settings* settings) {
    LocatorOptions options;
    options.option = nonEmpty(std::move(option));
    const QString environment = qEnvironmentVariable("HIO_NRFUTIL");
    if (!environment.isEmpty()) {
        options.environment = fs::path(environment.toStdU16String());
    }
    if (settings != nullptr) {
        options.setting = nonEmpty(settings->get(pathSetting()));
    }
    const QString pathVariable = qEnvironmentVariable("PATH");
    for (const QString& directory : pathVariable.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
        options.searchPath.emplace_back(directory.toStdU16String());
    }
    const fs::path home(QDir::homePath().toStdU16String());
    options.installDirs.push_back(home / ".nrfutil" / "bin");
#ifdef _WIN32
    const QString userProfile = qEnvironmentVariable("USERPROFILE");
    if (!userProfile.isEmpty()) {
        options.installDirs.push_back(fs::path(userProfile.toStdU16String()) / ".nrfutil" / "bin");
    }
#else
    options.installDirs.emplace_back("/usr/local/bin");
#endif
    return options;
}

Result<fs::path> locate(const LocatorOptions& options) {
    if (options.option) {
        return useOverride(*options.option, "--nrfutil");
    }
    if (options.environment) {
        return useOverride(*options.environment, "HIO_NRFUTIL");
    }
    if (options.setting) {
        return useOverride(*options.setting, "setting nrfutil/path");
    }
    std::vector<std::string> searched;
    const auto search =
        [&searched](const std::vector<fs::path>& directories) -> std::optional<fs::path> {
        for (const fs::path& directory : directories) {
            fs::path candidate = directory / executableName;
            searched.push_back(toUtf8(candidate));
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) {
                return candidate;
            }
        }
        return std::nullopt;
    };
    for (const auto* directories : {&options.searchPath, &options.installDirs}) {
        if (auto found = search(*directories)) {
            qCInfo(log::lcFlash).noquote() << "nRF Util found:" << toQ(*found);
            return *found;
        }
    }
    std::string detail = "searched:";
    for (const std::string& path : searched) {
        detail += " " + path;
    }
    return std::unexpected(notFound(searched.empty() ? std::string("nothing searched") : detail));
}

// ---- versions -------------------------------------------------------------------------------

std::string formatVersion(const Version& version) {
    return std::format("{}.{}.{}", version.major, version.minor, version.patch);
}

std::optional<Version> parseVersion(std::string_view text) {
    static const std::regex pattern(R"((\d{1,4})\.(\d{1,4})\.(\d{1,4}))");
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_search(text.begin(), text.end(), match, pattern)) {
        return std::nullopt;
    }
    return Version{.major = std::stoi(match[1].str()),
                   .minor = std::stoi(match[2].str()),
                   .patch = std::stoi(match[3].str())};
}

Result<NrfUtilInfo> checkVersions(const fs::path& executable, std::chrono::milliseconds timeout) {
    auto core = runShort(executable, {QStringLiteral("--version")}, timeout);
    if (!core) {
        return std::unexpected(std::move(core).error());
    }
    const auto coreVersion = parseVersion(core->out);
    if (core->exitCode != 0 || !coreVersion) {
        return std::unexpected(
            notFound(std::format("{} --version: exit {}, \"{}\"", toUtf8(executable),
                                 core->exitCode, firstLine(core->out + core->err))));
    }
    auto device =
        runShort(executable, {QStringLiteral("device"), QStringLiteral("--version")}, timeout);
    if (!device) {
        return std::unexpected(std::move(device).error());
    }
    const auto deviceVersion = parseVersion(device->out);
    if (device->exitCode != 0 || !deviceVersion) {
        return std::unexpected(
            makeError(ErrorCode::DependencyNrfUtilDeviceMissing,
                      std::format("{} device --version: exit {}, \"{}\"", toUtf8(executable),
                                  device->exitCode, firstLine(device->err + device->out))));
    }
    const auto tooOld = [&](const Version& found, const Version& minimum, std::string_view what) {
        Error error = makeError(
            ErrorCode::DependencyNrfUtilTooOld,
            std::format("{} {}, minimum {}", what, formatVersion(found), formatVersion(minimum)));
        error.message = QCoreApplication::translate("hio::nrfutil", "nRF Util %1 is too old.")
                            .arg(toQString(formatVersion(found)))
                            .toStdString();
        return error;
    };
    if (*coreVersion < minimumCoreVersion) {
        return std::unexpected(tooOld(*coreVersion, minimumCoreVersion, "nrfutil"));
    }
    if (*deviceVersion < minimumDeviceVersion) {
        return std::unexpected(tooOld(*deviceVersion, minimumDeviceVersion, "nrfutil-device"));
    }
    qCInfo(log::lcFlash).noquote()
        << QStringLiteral("nRF Util %1 (device %2) at %3")
               .arg(toQString(formatVersion(*coreVersion)),
                    toQString(formatVersion(*deviceVersion)), toQ(executable));
    return NrfUtilInfo{.executable = executable, .core = *coreVersion, .device = *deviceVersion};
}

// ---- modem update ---------------------------------------------------------------------------

JsonEvent parseJsonEvent(const QJsonObject& line) {
    JsonEvent event;
    const QString type = line.value(QStringLiteral("type")).toString();
    const QJsonObject data = line.value(QStringLiteral("data")).toObject();
    const QJsonValue progress = data.value(QStringLiteral("progress"))
                                    .toObject()
                                    .value(QStringLiteral("progressPercentage"));
    if (progress.isDouble()) {
        event.percent = std::clamp(progress.toInt(), 0, 100);
    }
    if (type == QStringLiteral("task_end")) {
        const QString result = data.value(QStringLiteral("result")).toString();
        event.success = result == QStringLiteral("success");
    }
    if (type == QStringLiteral("error")) {
        event.success = false;
    }
    QString message = data.value(QStringLiteral("message")).toString();
    const QJsonObject error = data.value(QStringLiteral("error")).toObject();
    if (message.isEmpty()) {
        message = error.value(QStringLiteral("description")).toString();
    }
    if (message.isEmpty()) {
        message = error.value(QStringLiteral("message")).toString();
    }
    event.message = message.toStdString();
    return event;
}

NrfUtilModemUpdater::NrfUtilModemUpdater(fs::path executable, ModemUpdaterTiming timing)
    : m_executable(std::move(executable)), m_timing(timing) {}

std::vector<std::string> NrfUtilModemUpdater::programArguments(const fs::path& modemZip,
                                                               std::uint32_t probeSn) {
    // nRF Util lists J-Link serial numbers with 12 digits (to confirm in Phase 0).
    return {"device",         "program",         "--firmware",
            toUtf8(modemZip), "--serial-number", std::format("{:012}", probeSn),
            "--json"};
}

namespace {

// What the NDJSON lines of one run said.
struct JsonState {
    std::optional<bool> success; // false once any task_end failed
    std::string lastMessage;

    template <class ReportFn> void add(const std::string& text, const ReportFn& report) {
        QJsonParseError parseError{};
        const QJsonDocument document =
            QJsonDocument::fromJson(QByteArray::fromStdString(text), &parseError);
        if (!document.isObject()) {
            return; // not JSON (tolerated)
        }
        const JsonEvent event = parseJsonEvent(document.object());
        if (event.percent) {
            report(static_cast<std::uint64_t>(*event.percent));
        }
        if (event.success.has_value()) {
            success = success.value_or(true) && *event.success;
        }
        if (!event.message.empty()) {
            lastMessage = event.message;
        }
    }
};

} // namespace

Status NrfUtilModemUpdater::waitForProcess(QProcess& process, const std::function<void()>& drain,
                                           const std::stop_token& stop) const {
    // Ends the process: terminate, then kill after the grace time.
    const auto stopProcess = [&] {
        process.terminate();
        if (!process.waitForFinished(static_cast<int>(m_timing.terminateGrace.count()))) {
            process.kill();
            process.waitForFinished();
        }
        drain();
    };
    const auto deadline = Clock::now() + m_timing.overallTimeout;
    while (process.state() != QProcess::NotRunning) {
        (void)process.waitForReadyRead(static_cast<int>(m_timing.poll.count()));
        drain();
        if (stop.stop_requested()) {
            qCInfo(log::lcFlash) << "Cancelling nRF Util";
            stopProcess();
            return fail(ErrorCode::Cancelled);
        }
        if (Clock::now() >= deadline) {
            stopProcess();
            return std::unexpected(makeError(
                ErrorCode::Timeout, std::format("nRF Util did not finish within {} s",
                                                std::chrono::duration_cast<std::chrono::seconds>(
                                                    m_timing.overallTimeout)
                                                    .count())));
        }
    }
    return {};
}

Status NrfUtilModemUpdater::updateModem(const fs::path& modemZip, std::uint32_t probeSn,
                                        const firmware::ProgressFn& progress,
                                        std::stop_token stop) {
    QStringList arguments;
    for (const std::string& argument : programArguments(modemZip, probeSn)) {
        arguments.append(toQString(argument));
    }
    qCInfo(log::lcFlash).noquote()
        << "Running" << toQ(m_executable) << arguments.join(QLatin1Char(' '));
    const auto report = [&progress](std::uint64_t done) {
        if (progress) {
            progress(firmware::ProgramProgress{
                .phase = firmware::ProgramPhase::Modem, .done = done, .total = 100});
        }
    };
    report(0);

    QProcess process; // owned by this (worker) thread; blocking waits, no event loop
    process.setProgram(toQ(m_executable));
    process.setArguments(arguments);
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(static_cast<int>(m_timing.terminateGrace.count()))) {
        return std::unexpected(
            notFound(std::format("{} cannot be started: {}", toUtf8(m_executable),
                                 process.errorString().toStdString())));
    }

    StreamCapture out(m_timing.maxOutputBytes);
    StreamCapture err(m_timing.maxOutputBytes);
    JsonState json;
    const auto onLine = [&json, &report](const std::string& text) { json.add(text, report); };
    const auto drain = [&] {
        out.append(process.readAllStandardOutput(), onLine);
        err.append(process.readAllStandardError(), [](const std::string&) {});
    };
    if (Status waited = waitForProcess(process, drain, stop); !waited) {
        if (waited.error().code == ErrorCode::Timeout) {
            waited.error().detail += "; " + err.tail();
        }
        return waited;
    }
    process.waitForFinished(0);
    drain();
    const bool crashed = process.exitStatus() == QProcess::CrashExit;
    const int exitCode = process.exitCode();
    if (crashed || exitCode != 0 || json.success == false) {
        std::string detail =
            std::format("nRF Util exit {}{}", exitCode, crashed ? " (crashed)" : "");
        if (!json.lastMessage.empty()) {
            detail += ": " + json.lastMessage;
        }
        const std::string stderrTail = err.tail();
        if (!stderrTail.empty()) {
            detail += "; stderr: " + stderrTail;
        }
        qCWarning(log::lcFlash).noquote() << "Modem update failed:" << toQString(detail);
        return std::unexpected(makeError(ErrorCode::FirmwareModemUpdateFailed, std::move(detail)));
    }
    report(100);
    qCInfo(log::lcFlash) << "Modem firmware updated";
    return {};
}

} // namespace hio::nrfutil
