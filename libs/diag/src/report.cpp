// The diagnostic report (ADR-0075, plan/features/0007).

#include "hio/diag/report.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/version.h"
#include "hio/diag/check.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QChar>
#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QString>
#include <QSysInfo>
#include <QtTypes>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <map>
#include <miniz.h>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::diag {

namespace {

namespace fs = std::filesystem;

QString redacted(const QString& text, const RedactionContext& context) {
    return QString::fromStdString(redact(text.toStdString(), context));
}

QJsonValue redactJson(const QJsonValue& value, const RedactionContext& context) {
    if (value.isString()) {
        return redacted(value.toString(), context);
    }
    if (value.isArray()) {
        QJsonArray array;
        for (const auto& item : value.toArray()) {
            array.append(redactJson(item, context));
        }
        return array;
    }
    if (value.isObject()) {
        QJsonObject object;
        const QJsonObject source = value.toObject();
        for (auto it = source.begin(); it != source.end(); ++it) {
            object.insert(it.key(), redactJson(it.value(), context));
        }
        return object;
    }
    return value;
}

std::string str(const QJsonObject& object, const char* key) {
    return object.value(QLatin1StringView(key)).toString().toStdString();
}

QJsonObject checkJson(const CheckResult& result) {
    QJsonObject json;
    json.insert(QStringLiteral("id"), toQString(result.id));
    json.insert(QStringLiteral("status"), toQString(checkStatusId(result.status)));
    json.insert(QStringLiteral("title"), toQString(result.title));
    json.insert(QStringLiteral("result"), toQString(result.result));
    json.insert(QStringLiteral("error"),
                result.error ? QJsonValue(toJson(*result.error)) : QJsonValue(QJsonValue::Null));
    return json;
}

// The last maxLogBytes of a log file, starting at a line start (bounded memory).
std::optional<std::string> readTail(const fs::path& path) {
    QFile file(QString::fromStdU16String(path.u16string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const qint64 size = file.size();
    const auto window = static_cast<qint64>(maxLogBytes);
    if (size <= window) {
        return file.readAll().toStdString();
    }
    // One byte before the window: when it is a newline, the window starts at a line start.
    if (!file.seek(size - window - 1)) {
        return std::nullopt;
    }
    const QByteArray bytes = file.read(window + 1);
    return std::string(tailAtLineStart(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), maxLogBytes));
}

struct ZipEntry {
    std::string name;
    std::string data;
};

Error writeError(const fs::path& target, std::string_view what) {
    return makeError(ErrorCode::DiagReportWriteFailed, std::format("{}: {}", toUtf8(target), what));
}

} // namespace

std::string_view tailAtLineStart(std::string_view data, std::size_t maxBytes) {
    if (data.size() <= maxBytes) {
        return data;
    }
    std::string_view window = data.substr(data.size() - maxBytes);
    if (data[data.size() - maxBytes - 1] == '\n') {
        return window; // the window starts at a line start
    }
    const auto newline = window.find('\n');
    if (newline == std::string_view::npos || newline + 1 >= window.size()) {
        return window;
    }
    return window.substr(newline + 1);
}

QJsonObject appInfoJson(std::string_view uiLanguage) {
    const BuildInfo info = buildInfo();
    QJsonObject app;
    app.insert(QStringLiteral("version"), toQString(info.version));
    app.insert(QStringLiteral("gitHash"), toQString(info.gitHash));
    app.insert(QStringLiteral("qt"), toQString(info.qtRuntime));
    app.insert(QStringLiteral("compiler"), toQString(info.compiler));
    app.insert(QStringLiteral("os"), QSysInfo::prettyProductName());
    app.insert(QStringLiteral("kernel"),
               QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion());
    app.insert(QStringLiteral("arch"), QSysInfo::currentCpuArchitecture());
    app.insert(QStringLiteral("language"), toQString(uiLanguage));
    return app;
}

std::string defaultReportFileName() {
    return std::format(
        "hio-diagnostics-{}.zip",
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")).toStdString());
}

std::vector<fs::path> recentLogFiles(const fs::path& logDir) {
    // Run name -> the newest file of that run (".partN.log" files continue a run).
    std::map<std::string, std::pair<fs::file_time_type, fs::path>> runs;
    std::error_code error;
    fs::directory_iterator it(logDir, error);
    constexpr std::size_t maxEntries = 1024;
    std::size_t entries = 0;
    for (; !error && it != fs::directory_iterator() && entries < maxEntries;
         it.increment(error), ++entries) {
        const std::string name = it->path().filename().string();
        if (!name.ends_with(".log") || !(name.starts_with("gui-") || name.starts_with("cli-"))) {
            continue;
        }
        std::string run = name.substr(0, name.size() - 4);
        if (const auto part = run.find(".part"); part != std::string::npos) {
            run.resize(part);
        }
        std::error_code timeError;
        const auto time = fs::last_write_time(it->path(), timeError);
        auto& slot = runs[run];
        if (slot.second.empty() || (!timeError && time >= slot.first)) {
            slot = {time, it->path()};
        }
    }
    std::vector<fs::path> files;
    for (const std::string_view kind : {std::string_view("gui-"), std::string_view("cli-")}) {
        std::vector<std::string> names;
        for (const auto& [run, newest] : runs) {
            if (run.starts_with(kind)) {
                names.push_back(run);
            }
        }
        // Names hold the UTC start time (gui-YYYYMMDD-HHMMSS-pid): the last two are the newest.
        std::ranges::sort(names);
        const std::size_t first = names.size() > 2 ? names.size() - 2 : 0;
        for (std::size_t i = first; i < names.size() && files.size() < maxLogFiles; ++i) {
            files.push_back(runs.at(names.at(i)).second);
        }
    }
    return files;
}

QJsonObject ReportBuilder::summaryJson(const ReportInput& input) {
    QJsonObject json;
    json.insert(QStringLiteral("created"), toQString(input.createdUtc));
    json.insert(QStringLiteral("app"), input.app);
    json.insert(QStringLiteral("tools"), input.tools);
    json.insert(QStringLiteral("probes"), input.probes);
    json.insert(QStringLiteral("tabs"), input.tabs);
    QJsonArray checks;
    for (const CheckResult& result : input.doctor) {
        checks.append(checkJson(result));
    }
    json.insert(QStringLiteral("checks"), checks);
    return redactJson(json, input.redaction).toObject();
}

std::string ReportBuilder::summaryText(const ReportInput& input) {
    std::string text;
    text += "HARDWARIO Monitor diagnostic report\n";
    text += std::format("Created: {} (UTC)\n", input.createdUtc);
    text +=
        std::format("Application: {} (git {}), Qt {}, {}\n", str(input.app, "version"),
                    str(input.app, "gitHash"), str(input.app, "qt"), str(input.app, "compiler"));
    text += std::format("System: {} ({}), {}\n", str(input.app, "os"), str(input.app, "kernel"),
                        str(input.app, "arch"));
    text += std::format("Language: {}\n\n", str(input.app, "language"));

    const QJsonObject jlink = input.tools.value(QStringLiteral("jlink")).toObject();
    text += jlink.isEmpty() ? std::string("J-Link Software: not found\n")
                            : std::format("J-Link Software: {} · {}\n", str(jlink, "version"),
                                          str(jlink, "path"));
    const QJsonObject nrfUtil = input.tools.value(QStringLiteral("nrfutil")).toObject();
    text += nrfUtil.isEmpty()
                ? std::string("nRF Util: not found\n")
                : std::format("nRF Util: {}, device {} · {}\n", str(nrfUtil, "version"),
                              str(nrfUtil, "deviceVersion"), str(nrfUtil, "path"));

    text += std::format("\nJ-Link probes: {}\n", input.probes.size());
    for (const auto& value : input.probes) {
        const QJsonObject probe = value.toObject();
        const std::string inUse = str(probe, "inUseBy");
        text +=
            std::format("  {} {} · firmware {} ({}){}\n",
                        probe.value(QStringLiteral("serialNumber")).toInteger(),
                        str(probe, "product"), str(probe, "firmware"), str(probe, "firmwareStatus"),
                        inUse.empty() ? std::string() : " · in use by " + inUse);
    }
    if (!input.tabs.isEmpty()) {
        text += std::format("\nOpen tabs: {}\n", input.tabs.size());
        for (const auto& value : input.tabs) {
            const QJsonObject tab = value.toObject();
            text += std::format(
                "  {} {} · J-Link {} · {}\n", str(tab, "profile"), str(tab, "target"),
                tab.value(QStringLiteral("serialNumber")).toInteger(), str(tab, "state"));
        }
    }
    text += "\nChecks:\n";
    text += formatCheckLines(input.doctor);
    for (const CheckResult& result : input.doctor) {
        if (result.error &&
            (result.status == CheckStatus::Failed || result.status == CheckStatus::Warning)) {
            text += std::format("\n{} ({}): {}\n", result.title, idOf(result.error->code),
                                result.error->message);
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
    }
    text = redact(text, input.redaction);
    if (text.size() > maxSummaryBytes) {
        constexpr std::string_view marker = "[truncated]\n";
        text.resize(maxSummaryBytes - marker.size());
        if (const auto newline = text.rfind('\n'); newline != std::string::npos) {
            text.resize(newline + 1);
        }
        text += marker;
    }
    return text;
}

Result<std::size_t> ReportBuilder::writeZip(const ReportInput& input, const fs::path& target,
                                            const std::stop_token& stop) {
    std::vector<ZipEntry> entries;
    entries.push_back({"summary.txt", summaryText(input)});
    entries.push_back(
        {"summary.json",
         QJsonDocument(summaryJson(input)).toJson(QJsonDocument::Indented).toStdString()});
    std::size_t total = entries[0].data.size() + entries[1].data.size();
    for (const fs::path& log : input.logFiles) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled);
        }
        auto tail = readTail(log);
        if (!tail) {
            qCWarning(log::lcDiag).noquote()
                << "Report: log not readable:" << QString::fromStdU16String(log.u16string());
            continue;
        }
        std::string data = redact(*tail, input.redaction);
        if (total + data.size() > maxReportBytes) {
            break;
        }
        total += data.size();
        entries.push_back({"logs/" + log.filename().string(), std::move(data)});
    }
    if (!input.settingsFile.empty()) {
        QFile settings(QString::fromStdU16String(input.settingsFile.u16string()));
        if (settings.open(QIODevice::ReadOnly)) {
            std::string data = redact(
                settings.read(static_cast<qint64>(maxSummaryBytes)).toStdString(), input.redaction);
            if (total + data.size() <= maxReportBytes) {
                entries.push_back({"settings.ini", std::move(data)});
            }
        }
    }

    mz_zip_archive archive{};
    mz_zip_zero_struct(&archive);
    if (mz_zip_writer_init_heap(&archive, 0, 0) == MZ_FALSE) {
        return std::unexpected(writeError(target, "zip init failed"));
    }
    for (const ZipEntry& entry : entries) {
        if (stop.stop_requested()) {
            mz_zip_writer_end(&archive);
            return fail(ErrorCode::Cancelled);
        }
        if (mz_zip_writer_add_mem(&archive, entry.name.c_str(), entry.data.data(),
                                  entry.data.size(), MZ_DEFAULT_COMPRESSION) == MZ_FALSE) {
            mz_zip_writer_end(&archive);
            return std::unexpected(writeError(target, "zip add failed: " + entry.name));
        }
    }
    void* buffer = nullptr;
    std::size_t size = 0;
    if (mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size) == MZ_FALSE) {
        mz_zip_writer_end(&archive);
        return std::unexpected(writeError(target, "zip finalize failed"));
    }
    const QByteArray bytes(static_cast<const char*>(buffer), static_cast<qsizetype>(size));
    mz_free(buffer);
    mz_zip_writer_end(&archive);

    // Temporary file + rename: a failed write leaves no partial zip (ADR-0075).
    QSaveFile file(QString::fromStdU16String(target.u16string()));
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected(writeError(target, file.errorString().toStdString()));
    }
    if (stop.stop_requested()) {
        file.cancelWriting();
        return fail(ErrorCode::Cancelled);
    }
    if (file.write(bytes) != bytes.size()) {
        const std::string why = file.errorString().toStdString();
        file.cancelWriting();
        return std::unexpected(writeError(target, why));
    }
    if (!file.commit()) {
        return std::unexpected(writeError(target, file.errorString().toStdString()));
    }
    qCInfo(log::lcDiag).noquote() << "Diagnostic report written:"
                                  << QString::fromStdU16String(target.u16string()) << size
                                  << "bytes";
    return size;
}

} // namespace hio::diag
