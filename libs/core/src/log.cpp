#include "hio/core/log.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QString>
#include <QtLogging>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace hio::log {

Q_LOGGING_CATEGORY(lcCore, "hio.core")
Q_LOGGING_CATEGORY(lcJLink, "hio.jlink")
Q_LOGGING_CATEGORY(lcDevice, "hio.device")

namespace {

namespace fs = std::filesystem;

constexpr std::string_view truncatedMarker = " [truncated]";

std::string_view levelTag(Level level) {
    switch (level) {
    case Level::Debug:
        return "dbg";
    case Level::Info:
        return "inf";
    case Level::Warning:
        return "wrn";
    case Level::Error:
        return "err";
    case Level::Critical:
        return "crt";
    }
    return "crt";
}

Level levelOf(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return Level::Debug;
    case QtInfoMsg:
        return Level::Info;
    case QtWarningMsg:
        return Level::Warning;
    case QtCriticalMsg:
        return Level::Error;
    case QtFatalMsg:
        return Level::Critical;
    }
    return Level::Critical;
}

// Cuts `text` to at most maxMessageBytes without splitting a UTF-8 sequence.
void truncateMessage(std::string& text) {
    if (text.size() <= maxMessageBytes) {
        return;
    }
    std::size_t cut = maxMessageBytes - truncatedMarker.size();
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    text.resize(cut);
    text.append(truncatedMarker);
}

std::string_view kindPrefix(AppKind kind) { return kind == AppKind::Gui ? "gui" : "cli"; }

// A log file written by a process of the same kind ("cli-YYYYMMDD-HHMMSS-<pid>[.partN].log").
bool isOwnLogFile(const fs::path& path, AppKind kind) {
    static const std::regex guiPattern(R"(^gui-\d{8}-\d{6}-\d+(\.part\d+)?\.log$)");
    static const std::regex cliPattern(R"(^cli-\d{8}-\d{6}-\d+(\.part\d+)?\.log$)");
    return std::regex_match(toUtf8(path.filename()),
                            kind == AppKind::Gui ? guiPattern : cliPattern);
}

} // namespace

// The file and stderr sink. All members are guarded by m_mutex.
class Sink {
public:
    // An empty `dir` means stderr only.
    Sink(Level stderrLevel, fs::path dir, AppKind kind, std::string baseName)
        : m_stderrLevel(stderrLevel), m_dir(std::move(dir)), m_kind(kind),
          m_baseName(std::move(baseName)) {}

    // Opens the first file; returns a description of the problem when the directory is not
    // writable (logging then continues on stderr only).
    std::optional<std::string> open() {
        const std::scoped_lock lock(m_mutex);
        if (m_dir.empty()) {
            return std::string("no log directory");
        }
        std::error_code ec;
        fs::create_directories(m_dir, ec);
        if (ec) {
            std::string problem =
                std::format("cannot create log directory '{}': {}", toUtf8(m_dir), ec.message());
            m_dir.clear();
            return problem;
        }
        if (!openPartLocked()) {
            std::string problem = std::format("cannot create log file '{}'", toUtf8(m_currentFile));
            m_dir.clear();
            m_currentFile.clear();
            return problem;
        }
        pruneLocked();
        return std::nullopt;
    }

    void write(Level level, std::string_view category, std::string message) {
        truncateMessage(message);
        const std::string time = QDateTime::currentDateTimeUtc()
                                     .toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"))
                                     .toStdString();
        const std::size_t threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
        const std::string line = std::format("{} [{}] {} ({:#x}) {}\n", time, levelTag(level),
                                             category, threadId, message);

        const std::scoped_lock lock(m_mutex);
        if (level >= m_stderrLevel) {
            std::cerr << line << std::flush;
        }
        if (!m_file.is_open()) {
            return;
        }
        if (m_fileBytes > 0 && m_fileBytes + line.size() > maxFileBytes) {
            m_file.close();
            ++m_part;
            if (!openPartLocked()) {
                m_currentFile.clear();
                return;
            }
            pruneLocked();
        }
        m_file << line;
        m_fileBytes += line.size();
        if (level >= Level::Warning) {
            m_file.flush();
        }
    }

    void flush() {
        const std::scoped_lock lock(m_mutex);
        if (m_file.is_open()) {
            m_file.flush();
        }
        std::cerr.flush();
    }

    [[nodiscard]] fs::path currentFile() const {
        const std::scoped_lock lock(m_mutex);
        return m_currentFile;
    }

private:
    bool openPartLocked() {
        const std::string name = m_part == 1 ? std::format("{}.log", m_baseName)
                                             : std::format("{}.part{}.log", m_baseName, m_part);
        m_currentFile = m_dir / name;
        m_file.open(m_currentFile, std::ios::out | std::ios::trunc | std::ios::binary);
        m_fileBytes = 0;
        return m_file.is_open();
    }

    // Keeps the newest files of the own kind within retentionFor(m_kind); the current file always
    // stays. Once a file is deleted, every older file is deleted as well (pruning by age).
    void pruneLocked() const {
        struct Entry {
            fs::path path;
            fs::file_time_type time;
            std::uintmax_t size;
        };
        std::vector<Entry> entries;
        std::error_code ec;
        for (fs::directory_iterator it(m_dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code entryEc;
            if (!it->is_regular_file(entryEc) || !isOwnLogFile(it->path(), m_kind) ||
                it->path() == m_currentFile) {
                continue;
            }
            const auto time = it->last_write_time(entryEc);
            const auto size = it->file_size(entryEc);
            if (!entryEc) {
                entries.push_back({.path = it->path(), .time = time, .size = size});
            }
        }
        std::ranges::sort(entries, [](const Entry& a, const Entry& b) {
            return a.time != b.time ? a.time > b.time : a.path.filename() > b.path.filename();
        });

        std::error_code currentEc;
        std::uintmax_t total = fs::file_size(m_currentFile, currentEc);
        if (currentEc) {
            total = 0;
        }
        const RetentionLimits limits = retentionFor(m_kind);
        std::size_t kept = 1; // the current file
        bool pruning = false; // set at the first deleted file; everything older goes too
        for (const Entry& entry : entries) {
            if (!pruning && kept < limits.maxFiles && total + entry.size <= limits.maxTotalBytes) {
                ++kept;
                total += entry.size;
                continue;
            }
            pruning = true;
            std::error_code removeEc;
            fs::remove(entry.path, removeEc); // failures ignored (file may be open elsewhere)
        }
    }

    mutable std::mutex m_mutex;
    const Level m_stderrLevel;
    fs::path m_dir; // empty: stderr only
    const AppKind m_kind;
    const std::string m_baseName;
    fs::path m_currentFile;
    std::ofstream m_file;
    std::uintmax_t m_fileBytes = 0;
    int m_part = 1;
};

namespace {

// Process-wide state required by the C-style Qt message handler and the std::terminate handler.
// Owner: the single live Logging instance; every member is guarded by `mutex`.
struct ActiveState {
    std::mutex mutex;
    Sink* sink = nullptr; // non-owning; owned by the live Logging instance
    QtMessageHandler previousMessageHandler = nullptr;
    std::terminate_handler previousTerminateHandler = nullptr;
};

ActiveState& activeState() {
    static ActiveState state;
    return state;
}

// Called by Qt (C-style callback): must never throw (CLAUDE.md). A failure while logging is
// reported once per message on stderr and otherwise ignored.
void messageHandler(QtMsgType type, const QMessageLogContext& context,
                    const QString& text) noexcept {
    try {
        const Level level = levelOf(type);
        const std::string_view category =
            context.category != nullptr ? std::string_view(context.category) : "default";
        ActiveState& state = activeState();
        const std::scoped_lock lock(state.mutex);
        if (state.sink == nullptr) {
            return;
        }
        state.sink->write(level, category, text.toStdString());
        if (type == QtFatalMsg) {
            state.sink->flush(); // Qt aborts after the handler returns
        }
    } catch (...) {
        try {
            std::cerr << "hio: a log message could not be written\n";
        } catch (...) { // NOLINT(bugprone-empty-catch): nothing else can be done here
        }
    }
}

void terminateHandler() noexcept {
    std::terminate_handler previous = nullptr;
    try {
        ActiveState& state = activeState();
        // try_lock: terminate may be reached while this thread already holds the mutex.
        std::unique_lock lock(state.mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            previous = state.previousTerminateHandler;
            if (state.sink != nullptr) {
                state.sink->write(
                    Level::Critical, "hio.core",
                    "std::terminate called (unhandled exception or contract violation)");
                state.sink->flush();
            }
        }
    } catch (...) { // NOLINT(bugprone-empty-catch): the process is terminating anyway
    }
    if (previous != nullptr) {
        previous();
    }
    std::abort();
}

std::string makeBaseName(AppKind kind) {
    const std::string stamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")).toStdString();
    return std::format("{}-{}-{}", kindPrefix(kind), stamp, QCoreApplication::applicationPid());
}

fs::path defaultLogDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        return {};
    }
    return fs::path(base.toStdU16String()) / "logs";
}

} // namespace

Result<std::unique_ptr<Logging>> Logging::init(AppKind kind, const Options& options) {
    std::unique_ptr<Logging> logging;
    std::optional<std::string> problem;
    {
        ActiveState& state = activeState();
        const std::scoped_lock lock(state.mutex);
        if (state.sink != nullptr) {
            return std::unexpected(
                makeError(ErrorCode::InternalUnexpected, "logging is already initialized"));
        }
        fs::path dir = options.logDir.value_or(defaultLogDir());
        auto sink =
            std::make_unique<Sink>(options.stderrLevel, std::move(dir), kind, makeBaseName(kind));
        problem = sink->open();
        logging = std::make_unique<Logging>(Passkey{}, std::move(sink));
    }
    if (problem) {
        qCWarning(lcCore) << "Logging to stderr only:" << toQString(*problem);
    } else {
        qCInfo(lcCore) << "Log file:"
                       << QString::fromStdU16String(logging->currentFile().u16string());
    }
    return logging;
}

// Called by init() with activeState().mutex held.
Logging::Logging(Passkey /*key*/, std::unique_ptr<Sink> sink) : m_sink(std::move(sink)) {
    ActiveState& state = activeState();
    state.sink = m_sink.get();
    state.previousMessageHandler = qInstallMessageHandler(messageHandler);
    state.previousTerminateHandler = std::set_terminate(terminateHandler);
}

Logging::~Logging() {
    ActiveState& state = activeState();
    const std::scoped_lock lock(state.mutex);
    std::set_terminate(state.previousTerminateHandler);
    qInstallMessageHandler(state.previousMessageHandler);
    m_sink->flush();
    state.sink = nullptr;
    state.previousMessageHandler = nullptr;
    state.previousTerminateHandler = nullptr;
}

std::filesystem::path Logging::currentFile() const { return m_sink->currentFile(); }

} // namespace hio::log
