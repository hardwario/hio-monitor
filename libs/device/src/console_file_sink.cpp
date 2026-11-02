#include "hwio/device/console_file_sink.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/device/ble_lock.h"
#include "hwio/device/console_line.h"
#include "hwio/device/firmware_identity.h"
#include "hwio/device/probe_lock.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_key.h"
#include "owner_only.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileDevice>
#include <QIODevice>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QString>
#include <QStringLiteral>
#include <QTimeZone>
#include <QtLogging>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace hwio::device {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t headerWidth = 80;

Error writeFailed(const fs::path& file, const QString& reason) {
    return makeError(ErrorCode::IoWriteFailed, toUtf8(file) + ": " + reason.toStdString());
}

QDateTime toDateTime(std::chrono::system_clock::time_point time) {
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(ms);
}

// The stamp of generated file names (UTC).
std::string utcStamp(std::chrono::system_clock::time_point time) {
    return toDateTime(time)
        .toTimeZone(QTimeZone::UTC)
        .toString(QStringLiteral("yyyyMMdd-HHmmss"))
        .toStdString();
}

std::string timestamp(std::chrono::system_clock::time_point time) {
    return toDateTime(time).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")).toStdString();
}

std::string headerBlock(std::string_view text) {
    const std::string stars(headerWidth, '*');
    const std::size_t inner = headerWidth - 6;
    std::string centered(text.substr(0, inner));
    const std::size_t padding = inner - centered.size();
    centered = std::string(padding / 2, ' ') + centered + std::string(padding - (padding / 2), ' ');
    return stars + "\n***" + centered + "***\n" + stars + "\n";
}

fs::path partFile(const fs::path& base, int part) {
    if (part <= 1) {
        return base;
    }
    fs::path file = base;
    file.replace_extension(std::format(".part{}.log", part));
    return file;
}

struct FileInfo {
    fs::path path;
    fs::file_time_type modified;
    std::uintmax_t size = 0;
};

// The console files this process has open, in every session (review R45): the retention never
// deletes them. Owner: ConsoleFileSink (openCurrent() adds, closeFile() removes); guarded by the
// mutex (sinks run on different session workers). Bounded by the open sinks (8 sessions).
class OpenFiles {
public:
    void add(const fs::path& file) {
        const std::scoped_lock lock(m_mutex);
        m_files.insert(key(file));
    }
    void remove(const fs::path& file) {
        const std::scoped_lock lock(m_mutex);
        if (const auto it = m_files.find(key(file)); it != m_files.end()) {
            m_files.erase(it); // one instance: two sinks may append to the same explicit file
        }
    }
    [[nodiscard]] bool contains(const fs::path& file) const {
        const std::scoped_lock lock(m_mutex);
        return m_files.contains(key(file));
    }

private:
    static fs::path key(const fs::path& file) {
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(file, ec);
        return ec ? fs::absolute(file, ec).lexically_normal() : canonical;
    }

    mutable std::mutex m_mutex;
    std::multiset<fs::path> m_files;
};

OpenFiles& openFiles() {
    static OpenFiles files;
    return files;
}

bool allDigits(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

// Seconds-resolution stamp "YYYYMMDD-HHMMSS" at `at` (15 characters).
bool isStamp(std::string_view name, std::size_t at) {
    constexpr std::size_t stampSize = 15;
    if (at + stampSize > name.size()) {
        return false;
    }
    const std::string_view stamp = name.substr(at, stampSize);
    return stamp[8] == '-' && allDigits(stamp.substr(0, 8)) && allDigits(stamp.substr(9, 6));
}

// "-<one digit 2..9>" at the end: the collision suffix of a generated name (plan 0028).
bool hasCollisionSuffix(std::string_view name) {
    return name.size() >= 2 && name[name.size() - 2] == '-' && name.back() >= '2' &&
           name.back() <= '9';
}

// J-Link: "...-YYYYMMDD-HHMMSS-<1..10 digits>".
std::optional<SessionKey> probeKeyOf(std::string_view name) {
    constexpr std::size_t maxDigits = 10; // std::uint32_t
    constexpr std::size_t stampSize = 15;
    const std::size_t dash = name.rfind('-');
    if (dash == std::string_view::npos || dash < stampSize + 1) {
        return std::nullopt;
    }
    const std::string_view id = name.substr(dash + 1);
    const std::size_t stampAt = dash - stampSize;
    if (id.size() > maxDigits || !allDigits(id) || !isStamp(name, stampAt) ||
        name[stampAt - 1] != '-') {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : id) {
        value = (value * 10) + static_cast<std::uint64_t>(c - '0');
    }
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return SessionKey(ProbeKey{.sn = static_cast<std::uint32_t>(value)});
}

} // namespace

// The session key of a generated console file name (ADR-0128, plan 0028):
// <profile>-<target>[-<slug>-<version>]-YYYYMMDD-HHMMSS-<sn>.log (J-Link) or
// <profile>-ble[-<slug>-<version>]-YYYYMMDD-HHMMSS-<id>.log (BLE, the sanitised id), optionally
// with "-2" ... "-9" and/or .partN before .log; nullopt for other names. Profile ids contain no '-'
// (static_assert in the profile). J-Link names are parsed from the end; for BLE the second token is
// "ble" and the FIRST stamp after it ends the prefix (the id may contain dashes).
std::optional<SessionKey> consoleFileSessionKey(const fs::path& file) {
    if (file.extension() != ".log") {
        return std::nullopt;
    }
    std::string stem = file.stem().string(); // without ".log"
    if (const auto part = stem.rfind(".part"); part != std::string::npos) {
        stem.resize(part);
    }
    const std::string_view name = stem;
    const std::size_t first = name.find('-');
    if (first == std::string_view::npos || first == 0) {
        return std::nullopt;
    }
    const std::size_t second = name.find('-', first + 1);
    const std::string_view target = name.substr(
        first + 1, second == std::string_view::npos ? std::string_view::npos : second - first - 1);
    if (target == "ble") {
        for (std::size_t at = second; at != std::string_view::npos; at = name.find('-', at + 1)) {
            constexpr std::size_t stampSize = 15;
            if (!isStamp(name, at + 1) || at + 1 + stampSize >= name.size() ||
                name[at + 1 + stampSize] != '-') {
                continue;
            }
            std::string_view id = name.substr(at + 1 + stampSize + 1);
            if (hasCollisionSuffix(id) && id.size() > 2) {
                id.remove_suffix(2);
            }
            if (id.empty()) {
                return std::nullopt;
            }
            return SessionKey(BleKey{.id = std::string(id)});
        }
        return std::nullopt;
    }
    if (auto key = probeKeyOf(name)) {
        return key;
    }
    if (hasCollisionSuffix(name)) {
        return probeKeyOf(name.substr(0, name.size() - 2));
    }
    return std::nullopt;
}

namespace {

// A set key per session ("p<sn>", "b<id>").
std::string keyText(const SessionKey& key) {
    if (const auto* probe = std::get_if<ProbeKey>(&key)) {
        return "p" + std::to_string(probe->sn);
    }
    return "b" + std::get<BleKey>(key).id;
}

// Coarse file-system timestamps (FAT: 2 s): a file written within this time before its probe's
// lock was taken still counts as the lock owner's file.
constexpr std::chrono::seconds lockTimeTolerance{2};

} // namespace

std::string_view consoleFilePrefix(const ConsoleLine& line) noexcept {
    if (line.dir == Direction::In) {
        return " < ";
    }
    if (line.dir == Direction::Marker) {
        return " @ ";
    }
    return line.channel == Channel::Log ? " # " : " > ";
}

fs::path ConsoleFileSink::defaultDirectory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return fs::path(base.toStdU16String()) / "console";
}

Result<std::unique_ptr<ConsoleFileSink>>
ConsoleFileSink::open(const ConsoleFileConfig& config, const SessionKey& key,
                      std::string_view headerText, std::chrono::system_clock::time_point now) {
    fs::path base;
    const fs::path& directory = config.consoleFileDir.value_or(config.directory);
    std::string nameTarget = config.targetId;
    std::string keySuffix;
    if (const auto* probe = std::get_if<ProbeKey>(&key)) {
        keySuffix = std::to_string(probe->sn);
    } else {
        nameTarget = "ble";
        keySuffix = sanitisedBleId(std::get<BleKey>(key).id);
    }
    const std::string stamp = utcStamp(now);
    if (config.explicitFile) {
        base = *config.explicitFile;
    } else {
        base = directory /
               std::format("{}-{}-{}-{}.log", config.profileId, nameTarget, stamp, keySuffix);
    }
    auto sink = std::make_unique<ConsoleFileSink>(Passkey{}, config, std::move(base));
    sink->m_headerText = std::string(headerText);
    sink->m_nameTarget = std::move(nameTarget);
    sink->m_stamp = stamp;
    sink->m_keySuffix = std::move(keySuffix);
    if (auto status = sink->openCurrent(); !status) {
        return std::unexpected(std::move(status).error());
    }
    sink->m_buffer += headerBlock(headerText);
    if (auto status = sink->flush(); !status) {
        return std::unexpected(std::move(status).error());
    }
    if (!config.explicitFile) {
        sink->prune();
    }
    return sink;
}

Result<std::unique_ptr<ConsoleFileSink>>
ConsoleFileSink::open(const ConsoleFileConfig& config, std::uint32_t probeSerialNumber,
                      std::string_view headerText, std::chrono::system_clock::time_point now) {
    return open(config, SessionKey(ProbeKey{.sn = probeSerialNumber}), headerText, now);
}

ConsoleFileSink::ConsoleFileSink(Passkey /*key*/, ConsoleFileConfig config, fs::path baseFile)
    : m_config(std::move(config)), m_base(std::move(baseFile)), m_current(m_base),
      m_sensitivePrefixes(sensitivePrefixesOf(m_config.profileId, m_config.targetId)),
      m_sensitiveOutput(sensitiveOutputKeysOf(m_config.profileId, m_config.targetId)) {
    // Review R4: the shell prompts of the profile (RTT target, BLE), stripped from echoed lines.
    if (const DeviceProfile* profile = ProfileRegistry::find(m_config.profileId)) {
        if (const TargetProfile* target = findTarget(*profile, m_config.targetId)) {
            m_prompts.at(0) = target->shellPrompt;
        }
        if (profile->ble) {
            m_prompts.at(1) = profile->ble->shellPrompt;
        }
    }
}

ConsoleFileSink::~ConsoleFileSink() {
    if (m_file && !m_buffer.empty()) {
        if (const Status status = flush(); !status) {
            qCWarning(log::lcDevice).noquote() << toQString(status.error().detail);
        }
    }
    closeFile();
}

Status ConsoleFileSink::openCurrent() {
    std::error_code ec;
    if (m_current.has_parent_path()) {
        fs::create_directories(m_current.parent_path(), ec);
    }
    m_file = std::make_unique<QFile>(toQString(toUtf8(m_current)));
    // Review R9: a new file is created owner-only; an existing one is tightened below.
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Append,
                      QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        const QString reason = m_file->errorString();
        m_file = nullptr;
        return std::unexpected(writeFailed(m_current, reason));
    }
    detail::restrictToOwner(*m_file); // ADR-0186: before the first write; existing files too
    m_written = static_cast<std::uintmax_t>(m_file->size());
    openFiles().add(m_current);
    qCInfo(log::lcDevice).noquote() << "Console file:" << toQString(toUtf8(m_current));
    return {};
}

void ConsoleFileSink::closeFile() {
    if (m_file) {
        m_file = nullptr; // closes
        openFiles().remove(m_current);
    }
}

Status ConsoleFileSink::rotateIfNeeded(std::size_t incoming) {
    const std::uintmax_t limit = m_config.explicitFile ? maxExplicitFileBytes : maxDefaultFileBytes;
    if (m_written == 0 || m_written + incoming <= limit) {
        return {};
    }
    if (m_config.explicitFile && m_rotationFailed) {
        // After a failed rotation the file is not rotated again; it keeps growing up to twice the
        // limit, then it is not written any more (the session drops the sink; review R26).
        if (m_written + incoming > maxExplicitFileBytesAfterFailedRotation) {
            closeFile();
            return std::unexpected(
                writeFailed(m_current, QStringLiteral("the file exceeds %1 bytes after a failed "
                                                      "rotation; it is not written any more")
                                           .arg(maxExplicitFileBytesAfterFailedRotation)));
        }
        return {};
    }
    closeFile();
    if (m_config.explicitFile) {
        fs::path backup = m_base;
        backup += ".1";
        std::error_code ec;
        fs::remove(backup, ec);
        fs::rename(m_base, backup, ec);
        if (ec) {
            // Not retried for the rest of the session; one warning (review R26).
            m_rotationFailed = true;
            qCWarning(log::lcDevice).noquote()
                << "Console file rotation failed:" << toQString(ec.message());
            m_warning = writeFailed(
                backup, QStringLiteral("cannot rotate the console file (%1); it keeps growing")
                            .arg(QString::fromStdString(ec.message())));
        }
    } else {
        ++m_part;
        m_current = partFile(m_base, m_part);
    }
    auto status = openCurrent();
    if (status && !m_config.explicitFile) {
        prune();
    }
    return status;
}

std::optional<Error> ConsoleFileSink::takeWarning() {
    return std::exchange(m_warning, std::nullopt);
}

Status ConsoleFileSink::append(std::string_view prefix, std::string_view text,
                               std::chrono::system_clock::time_point time) {
    m_buffer += timestamp(time);
    m_buffer += prefix;
    m_buffer += text;
    m_buffer += '\n';
    if (m_buffer.size() >= maxBufferedBytes) {
        return flush();
    }
    return {};
}

Status ConsoleFileSink::write(const ConsoleLine& line) {
    // ADR-0186: a command with secret arguments is written as "<prefix> <redacted>": the input
    // line and (review R4) a shell's echo of it in the output, after its prompt.
    if (line.dir == Direction::In ||
        (line.dir == Direction::Out && line.channel == Channel::Shell)) {
        const std::size_t promptLength =
            line.dir == Direction::Out ? echoPromptLength(line.text) : 0;
        const std::string_view command = std::string_view(line.text).substr(promptLength);
        if (const auto prefix = matchSensitiveCommand(command, m_sensitivePrefixes)) {
            return append(consoleFilePrefix(line),
                          line.text.substr(0, promptLength) + std::string(*prefix) + " <redacted>",
                          line.time);
        } // Plan 0028 (U2): the value of a secret printed by the device ("claim token:
          // <redacted>").
        if (line.dir == Direction::Out) {
            if (const auto key = matchSensitiveOutput(command, m_sensitiveOutput)) {
                return append(consoleFilePrefix(line),
                              line.text.substr(0, promptLength) + *key + ": <redacted>", line.time);
            }
        }
    }
    return append(consoleFilePrefix(line), line.text, line.time);
}

std::size_t ConsoleFileSink::echoPromptLength(std::string_view text) const noexcept {
    for (const std::string_view prompt : m_prompts) {
        if (!prompt.empty() && text.starts_with(prompt)) {
            return prompt.size();
        }
    }
    // A Zephyr shell prompt "<name>:~$ " (CHESTER's RTT prompt is not in the profile yet).
    constexpr std::string_view zephyrPromptEnd = ":~$ ";
    const std::size_t end = text.find(zephyrPromptEnd);
    if (end != std::string_view::npos && end > 0 &&
        text.substr(0, end).find_first_of(" \t") == std::string_view::npos) {
        return end + zephyrPromptEnd.size();
    }
    return 0;
}

Status ConsoleFileSink::writeConnection(std::string_view text,
                                        std::chrono::system_clock::time_point time) {
    return append(" @ ", text, time);
}

Status ConsoleFileSink::flush() {
    if (m_buffer.empty()) {
        return {};
    }
    if (auto status = rotateIfNeeded(m_buffer.size()); !status) {
        m_buffer.clear();
        return status;
    }
    if (!m_file) {
        m_buffer.clear();
        return std::unexpected(writeFailed(m_current, QStringLiteral("not open")));
    }
    const auto written = m_file->write(m_buffer.data(), static_cast<qint64>(m_buffer.size()));
    const bool flushed = m_file->flush();
    m_buffer.clear();
    if (written < 0 || !flushed) {
        return std::unexpected(writeFailed(m_current, m_file->errorString()));
    }
    m_written += static_cast<std::uintmax_t>(written);
    return {};
}

// ---- plan 0028: the firmware identity in the file name (ADR-0200) ----------------------------

std::optional<fs::path>
ConsoleFileSink::generatedName(const std::optional<FirmwareIdentity>& identity, int parts) const {
    const fs::path directory = m_base.parent_path();
    const std::string stem =
        std::format("{}-{}-{}", consoleFileBase(m_config.profileId, m_nameTarget, identity),
                    m_stamp, m_keySuffix);
    // R3: free for every part that will be renamed to it.
    const auto isFree = [parts](const fs::path& base) {
        for (int part = 1; part <= parts; ++part) {
            std::error_code ec;
            if (fs::exists(partFile(base, part), ec)) {
                return false;
            }
        }
        return true;
    };
    if (fs::path candidate = directory / (stem + ".log"); isFree(candidate)) {
        return candidate;
    }
    constexpr int maxCollisionSuffix = 9;
    for (int suffix = 2; suffix <= maxCollisionSuffix; ++suffix) {
        if (fs::path candidate = directory / std::format("{}-{}.log", stem, suffix);
            isFree(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

Status ConsoleFileSink::adoptIdentity(const std::optional<FirmwareIdentity>& identity,
                                      std::chrono::system_clock::time_point now) {
    if (m_config.explicitFile || !identity) {
        return {}; // never renamed; an unidentified attach keeps the name
    }
    if (!m_identity) {
        return renameForIdentity(*identity, now); // D4, D5
    }
    const bool firmware = sameFirmware(*m_identity, *identity);
    if (firmware && !otherDevice(*m_identity, *identity)) {
        if (m_identity->deviceSerial.empty()) {
            m_identity->deviceSerial = identity->deviceSerial; // R1: learned now
        }
        return {}; // the same firmware on the same device
    }
    FirmwareIdentity next = *identity;
    if (next.deviceSerial.empty()) {
        next.deviceSerial = m_identity->deviceSerial; // R1: an empty serial keeps the stored one
    }
    return continueInNewFile(next, now, firmware); // D3
}

Status ConsoleFileSink::renameForIdentity(const FirmwareIdentity& identity,
                                          std::chrono::system_clock::time_point now) {
    m_identity = identity;
    const std::string marker = "Firmware: " + identityText(identity);
    const std::string wanted =
        std::format("{}-{}-{}.log", consoleFileBase(m_config.profileId, m_nameTarget, identity),
                    m_stamp, m_keySuffix);
    if (m_base.filename().string() == wanted) {
        return writeConnection(marker, now); // no identity part (empty slugs): the same name
    }
    const std::optional<fs::path> newBase = generatedName(identity, m_part);
    if (!newBase) {
        qCInfo(log::lcDevice) << "Console file not renamed: the firmware names are taken";
        return writeConnection(marker, now);
    }
    if (Status flushed = flush(); !flushed) {
        return flushed;
    }
    // R2: the old and the new path of the live file stay in the open-files list until the file is
    // open again, so the retention of another sink in this process never deletes it meanwhile.
    const fs::path oldCurrent = m_current;
    const fs::path newCurrent = partFile(*newBase, m_part);
    openFiles().add(oldCurrent);
    openFiles().add(newCurrent);
    closeFile();
    fs::path current = m_current;
    for (int part = 1; part <= m_part; ++part) {
        const fs::path from = partFile(m_base, part);
        std::error_code missing;
        if (!fs::exists(from, missing)) {
            continue; // R3: a closed part that was pruned already
        }
        const fs::path to = partFile(*newBase, part);
        std::error_code ec;
        fs::rename(from, to, ec);
        if (ec) {
            // D11: e.g. Windows, another program holds the file without FILE_SHARE_DELETE.
            if (!std::exchange(m_renameFailureLogged, true)) {
                qCInfo(log::lcDevice).noquote()
                    << "Console file not renamed (it keeps its name):" << toQString(toUtf8(from))
                    << toQString(ec.message());
            }
            continue;
        }
        if (part == m_part) {
            current = to;
        }
    }
    m_base = *newBase;
    m_current = current;
    Status opened = openCurrent();
    openFiles().remove(newCurrent);
    openFiles().remove(oldCurrent);
    if (!opened) {
        return opened;
    }
    return writeConnection(marker, now);
}

Status ConsoleFileSink::continueInNewFile(const FirmwareIdentity& identity,
                                          std::chrono::system_clock::time_point now,
                                          bool deviceChanged) {
    // R1: the same firmware on another device (other serial number; the serial is never written).
    // R1: the same firmware on another device (the serial is never written). English like every
    // other file marker (user decision 2026-11-02).
    const auto changeText = [&](const std::optional<std::string>& next) {
        if (deviceChanged) {
            return next ? "Device changed; continued in " + *next : std::string("Device changed");
        }
        return next ? std::format("Firmware changed to {}; continued in {}", identityText(identity),
                                  *next)
                    : "Firmware changed to " + identityText(identity);
    };
    const std::string previousStamp = std::exchange(m_stamp, utcStamp(now));
    const std::optional<fs::path> next = generatedName(identity, 1);
    if (!next) {
        m_stamp = previousStamp;
        m_identity = identity;
        qCInfo(log::lcDevice) << "Console file not continued: the names are taken";
        return writeConnection(changeText(std::nullopt), now);
    }
    const std::string oldName = m_current.filename().string();
    if (Status written = writeConnection(changeText(toUtf8(next->filename())), now); !written) {
        return written;
    }
    if (Status flushed = flush(); !flushed) {
        return flushed;
    }
    closeFile();
    m_identity = identity;
    m_base = *next;
    m_current = m_base;
    m_part = 1;
    if (Status opened = openCurrent(); !opened) {
        return opened;
    }
    m_buffer += headerBlock(m_headerText);
    if (Status written = writeConnection("Continues " + oldName, now); !written) {
        return written;
    }
    if (Status written = writeConnection("Firmware: " + identityText(identity), now); !written) {
        return written;
    }
    if (Status flushed = flush(); !flushed) {
        return flushed;
    }
    prune();
    return {};
}

void ConsoleFileSink::prune() const {
    std::vector<FileInfo> files;
    std::error_code ec;
    // ADR-0156: in a directory given with --console-file-dir only our session files are counted
    // and deleted (it may hold other .log files of the user).
    const bool sessionFilesOnly = m_config.consoleFileDir.has_value();
    for (fs::directory_iterator it(m_config.consoleFileDir.value_or(m_config.directory), ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code entryError;
        if (!it->is_regular_file(entryError) || it->path().extension() != ".log") {
            continue;
        }
        if (sessionFilesOnly && !consoleFileSessionKey(it->path())) {
            continue;
        }
        FileInfo info{.path = it->path(),
                      .modified = it->last_write_time(entryError),
                      .size = it->file_size(entryError)};
        if (!entryError) {
            files.push_back(std::move(info));
        }
    }
    std::ranges::sort(files, [](const FileInfo& a, const FileInfo& b) {
        return a.modified > b.modified; // newest first
    });
    // Only a file a live session is writing now is kept (review R45, R48): in this process the
    // files a sink has open; for a probe whose lock has a live owner in ANOTHER process
    // (ProbeLock / BleLock owner() rules), only the newest file of that session, and only when it
    // was written since the owner took the lock. The closed .partN files of a live session and the
    // files of finished sessions are pruned as usual, so at most one file per live session is
    // kept beyond the bounds.
    const fs::path lockDirectory =
        m_config.lockDirectory.empty() ? ProbeLock::defaultDirectory() : m_config.lockDirectory;
    const qint64 ownPid = QCoreApplication::applicationPid();
    std::set<std::string> newestSeen; // session keys whose newest file was seen; bounded
    const auto isLive = [&](const FileInfo& file) {
        if (openFiles().contains(file.path)) {
            return true;
        }
        const std::optional<SessionKey> key = consoleFileSessionKey(file.path);
        if (!key || !newestSeen.insert(keyText(*key)).second) {
            return false; // not a session file, or not the newest file of its session
        }
        // ADR-0128: the J-Link probe lock or the BLE device lock (the sanitised id from the name
        // is a valid BleLock::owner() id: sanitising is idempotent).
        const std::optional<ProbeLockOwner> owner =
            std::holds_alternative<ProbeKey>(*key)
                ? ProbeLock::owner(std::get<ProbeKey>(*key).sn, lockDirectory)
                : BleLock::owner(std::get<BleKey>(*key).id, lockDirectory);
        // A lock of this process: its sessions' files are in the open-files list above.
        return owner.has_value() && owner->pid != ownPid &&
               (!owner->lockedAt || file.modified + lockTimeTolerance >= *owner->lockedAt);
    };
    std::size_t kept = 0;
    std::uintmax_t total = 0;
    bool deleting = false;
    for (const FileInfo& file : files) {
        std::error_code sameError;
        const bool current = fs::equivalent(file.path, m_current, sameError);
        if (!current && !isLive(file) &&
            (deleting || kept >= maxDefaultFiles || total + file.size > maxDefaultTotalBytes)) {
            deleting = true; // by age: everything older than a deleted file goes too
            std::error_code removeError;
            fs::remove(file.path, removeError);
            continue;
        }
        ++kept;
        total += file.size;
    }
}

} // namespace hwio::device
