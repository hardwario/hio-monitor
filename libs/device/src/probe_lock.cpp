#include "hio/device/probe_lock.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLockFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QString>
#include <QSysInfo>
#include <QtLogging>
#include <QtTypes>

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
// NOMINMAX and WIN32_LEAN_AND_MEAN are set by libs/device/CMakeLists.txt.
#include <windows.h>
// <psapi.h> needs <windows.h> first.
#include <QDir>
#include <QtEnvironmentVariables>

#include <array>
#include <psapi.h>
#else
#include <cerrno>
#include <csignal> // NOLINT(misc-include-cleaner): kill() is declared by <signal.h>
#include <sys/file.h>
#include <sys/types.h>
#ifdef __APPLE__
#include <array>
#include <libproc.h>
#endif
#endif

namespace hio::device {

namespace {

namespace fs = std::filesystem;

fs::path lockPath(std::uint32_t serialNumber, const fs::path& directory) {
    return directory / std::format("probe-{}.lock", serialNumber);
}

// ---- QLockFile's stale rules (review R37, R41) ------------------------------------------------
//
// QLockFile has no public non-acquiring stale check: tryLock() takes a stale lock over, and its
// isApparentlyStale() / isProcessRunning() / processNameByPid() are private. owner() must not
// take the lock (a concurrent acquire() of another process would fail), so the rules are mirrored
// here from the Qt sources of our pinned version, Qt 6.8.3 (src/corelib/io/qlockfile.cpp,
// qlockfile_unix.cpp, qlockfile_win.cpp; line numbers below refer to tag v6.8.3). acquire() itself
// uses QLockFile unchanged; both use staleLockTime 0 (no age rule).

// A lock file as QLockFilePrivate::lockFileContents() writes it (qlockfile.cpp:377-385): PID,
// application name (processNameByPid() of the owner), host name (machineName()), machine id
// (QSysInfo::machineUniqueId()) and boot id (QSysInfo::bootUniqueId()), one per line.
struct LockFileInfo {
    qint64 pid = 0;
    QString app;
    QString hostname;
    QByteArray hostId;
    QByteArray bootId;
};

// getLockInfo_helper() (qlockfile.cpp:387-415): nullopt when the file cannot be opened or its
// PID line is not a positive number.
std::optional<LockFileInfo> readLockFile(const QString& path) {
    QFile reader(path);
    if (!reader.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }
    const auto line = [&reader] {
        QByteArray text = reader.readLine();
        text.chop(1);
        return text;
    };
    const QByteArray pidLine = line();
    if (pidLine.isEmpty()) {
        return std::nullopt;
    }
    LockFileInfo info;
    info.app = QString::fromUtf8(line());
    info.hostname = QString::fromUtf8(line());
    info.hostId = line(); // prior to Qt 5.10 only the lines above were written
    info.bootId = line();
    bool ok = false;
    info.pid = pidLine.toLongLong(&ok);
    if (!ok || info.pid <= 0) {
        return std::nullopt;
    }
    return info;
}

// machineName() (qlockfile.cpp:32-40): what QLockFile writes as the host name and compares with.
QString machineName() {
#ifdef _WIN32
    return qEnvironmentVariable("COMPUTERNAME"); // Qt: QSysInfo would do name resolution
#else
    return QSysInfo::machineHostName();
#endif
}

// QLockFilePrivate::processNameByPid(): the name QLockFile records for a process and compares.
// Empty = unknown (no comparison).
QString processNameByPid(qint64 pid) {
#ifdef _WIN32
    // qlockfile_win.cpp:97-116: the module file name without directory and extension ("hio").
    HANDLE process =
        OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return {};
    }
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length =
        GetModuleFileNameExW(process, nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    CloseHandle(process);
    if (length == 0) {
        return {};
    }
    QString name = QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(length));
    if (const qsizetype slash = name.lastIndexOf(u'\\'); slash >= 0) {
        name.remove(0, slash + 1);
    }
    if (const qsizetype dot = name.lastIndexOf(u'.'); dot >= 0) {
        name.truncate(dot);
    }
    return name;
#elifdef __linux__
    // qlockfile_unix.cpp:208-226: the file name of /proc/<pid>/exe without " (deleted)";
    // "/ERROR/" (never equal to a recorded name) when the link cannot be read. Without procfs
    // (qt_haveLinuxProcfs(): /proc/version, qcore_unix_p.h:368-376) the name is unknown.
    std::error_code ec;
    if (!fs::exists("/proc/version", ec)) {
        return {};
    }
    std::string target = fs::read_symlink(std::format("/proc/{}/exe", pid), ec).string();
    if (ec || target.empty()) {
        return QStringLiteral("/ERROR/");
    }
    constexpr std::string_view deleted = " (deleted)";
    if (target.ends_with(deleted)) {
        target.resize(target.size() - deleted.size());
    }
    return QFileInfo(QFile::decodeName(QByteArray::fromStdString(target))).fileName();
#elifdef __APPLE__
    // qlockfile_unix.cpp:204-207: proc_name() into a 1024-byte buffer. Qt ignores its result
    // (the buffer stays uninitialised on failure); ours is zeroed, so a failure is "unknown".
    std::array<char, 1024> name{};
    static_cast<void>(
        proc_name(static_cast<int>(pid), name.data(), static_cast<std::uint32_t>(name.size())));
    return QFile::decodeName(name.data());
#else
    (void)pid;
    return {};
#endif
}

// QLockFilePrivate::isProcessRunning(): the owner runs when its PID is alive and, when the
// running program's name is known, it is the recorded application (otherwise the PID was reused).
bool isProcessRunning(qint64 pid, const QString& app) {
#ifdef _WIN32
    // qlockfile_win.cpp:76-95: a process that cannot be opened (also: access denied) is not
    // running; the names are compared as they are (no extension, case-sensitive).
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return false;
    }
    DWORD exitCode = 0;
    if (GetExitCodeProcess(process, &exitCode) == 0) {
        exitCode = 0;
    }
    CloseHandle(process);
    if (exitCode != STILL_ACTIVE) {
        return false;
    }
    const QString running = processNameByPid(pid);
    return running.isEmpty() || running == app;
#else
    // qlockfile_unix.cpp:185-200: only ESRCH means gone (EPERM = alive, another user's); the
    // recorded name is resolved when it is a symbolic link, then its file name is compared.
    if (::kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH) {
        return false;
    }
    const QString running = processNameByPid(pid);
    if (!running.isEmpty()) {
        QFileInfo recorded(app);
        if (recorded.isSymLink()) {
            recorded.setFile(recorded.symLinkTarget());
        }
        if (running != recorded.fileName()) {
            return false;
        }
    }
    return true;
#endif
}

// QLockFilePrivate::isApparentlyStale() (qlockfile.cpp:417-444) with staleLockTime 0: on the
// same host (the machine id decides when both sides know it, else the host name), a lock of
// another boot or of an owner that does not run is stale; a lock of another host never is.
bool isApparentlyStale(const LockFileInfo& info) {
    bool sameHost = info.hostname.isEmpty() || info.hostname == machineName();
    if (!info.hostId.isEmpty()) {
        const QByteArray ourHostId = QSysInfo::machineUniqueId();
        if (!ourHostId.isEmpty()) {
            sameHost = ourHostId == info.hostId;
        }
    }
    if (sameHost) {
        if (!info.bootId.isEmpty() && info.bootId != QSysInfo::bootUniqueId()) {
            return true;
        }
        if (!isProcessRunning(info.pid, info.app)) {
            return true;
        }
    }
    return false;
}

enum class Removal : std::uint8_t {
    Possible, // QLockFile would remove the file (the lock is stale)
    Refused,  // the file is held by its owner (or cannot be removed): not stale
    Gone,     // the file does not exist any more
};

// Whether QLockFilePrivate::removeStaleLock() would succeed, without removing anything: tryLock()
// takes a lock over only when it is apparently stale AND that removal succeeds
// (qlockfile.cpp:299-307).
Removal staleLockRemoval(const QString& path) {
#ifdef _WIN32
    // qlockfile_win.cpp:70-74: QFile::remove() fails while the owner keeps the file open (it is
    // opened without FILE_SHARE_DELETE, qlockfile_win.cpp:32). Opening it for DELETE access with
    // every sharing mode fails exactly then (ERROR_SHARING_VIOLATION).
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    HANDLE file =
        CreateFileW(native.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Removal::Gone
                                                                              : Removal::Refused;
    }
    CloseHandle(file);
    return Removal::Possible;
#else
    // qlockfile_unix.cpp:174-183: open for writing (an existing file only, nothing truncated),
    // then flock(LOCK_EX | LOCK_NB), which fails while the owner holds its flock
    // (qlockfile_unix.cpp:113-130, 148). Closing the file releases the flock at once; the unlink
    // is not tried.
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::ExistingOnly)) {
        return QFile::exists(path) ? Removal::Refused : Removal::Gone;
    }
    const bool locked = ::flock(file.handle(), LOCK_EX | LOCK_NB) == 0;
    file.close();
    return locked ? Removal::Possible : Removal::Refused;
#endif
}

std::optional<ProbeLockOwner> readOwner(QLockFile& lock) {
    qint64 pid = 0;
    QString hostname;
    QString app;
    if (!lock.getLockInfo(&pid, &hostname, &app)) {
        return std::nullopt;
    }
    return ProbeLockOwner{.app = app.isEmpty() ? std::string("?") : app.toStdString(),
                          .pid = pid,
                          .lockedAt = std::nullopt};
}

// The probes this process holds a ProbeLock for (review R15): tells a lock file with our own PID
// that we hold from a stale one of a dead process whose PID we got. Owner: ProbeLock (acquire /
// release). The mutex is held across the whole file-lock step (tryLock + add, unlock + remove,
// review R20), so a concurrent acquire in this process never sees "our PID, not held" for a lock
// that is being taken or released.
class HeldSerials {
public:
    [[nodiscard]] std::unique_lock<std::mutex> guard() { return std::unique_lock(m_mutex); }
    // The callers below hold guard().
    void add(std::uint32_t serialNumber) { m_serials.insert(serialNumber); }
    void remove(std::uint32_t serialNumber) { m_serials.erase(serialNumber); }
    [[nodiscard]] bool contains(std::uint32_t serialNumber) const {
        return m_serials.contains(serialNumber);
    }

private:
    std::mutex m_mutex;
    std::set<std::uint32_t> m_serials; // bounded by the number of probes held (sessions)
};

HeldSerials& heldSerials() {
    static HeldSerials serials;
    return serials;
}

Error inUseError(std::uint32_t serialNumber, const ProbeLockOwner& owner) {
    Error error = makeError(ErrorCode::ProbeInUseByOtherProcess,
                            std::format("lock held by {} (PID {})", owner.app, owner.pid));
    error.message = QCoreApplication::translate(
                        "hio::device", "The J-Link %1 is used by another program (%2, PID %3).")
                        .arg(serialNumber)
                        .arg(toQString(owner.app))
                        .arg(owner.pid)
                        .toStdString();
    return error;
}

} // namespace

fs::path ProbeLock::defaultDirectory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return fs::path(base.toStdU16String()) / "locks";
}

Result<ProbeLock> ProbeLock::acquire(std::uint32_t serialNumber) {
    return acquire(serialNumber, defaultDirectory());
}

Result<ProbeLock> ProbeLock::acquire(std::uint32_t serialNumber, const fs::path& directory) {
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (!fs::is_directory(directory, ec)) {
        // Locking cannot be guaranteed: the session is not opened (ADR-0058).
        return std::unexpected(makeError(ErrorCode::IoWriteFailed,
                                         "cannot create the lock directory " + toUtf8(directory)));
    }
    const fs::path path = lockPath(serialNumber, directory);
    auto lock = std::make_unique<QLockFile>(toQString(toUtf8(path)));
    lock->setStaleLockTime(std::chrono::milliseconds(0)); // stale only when the owner is gone
    HeldSerials& held = heldSerials();
    const auto guard = held.guard(); // across tryLock + add (review R20)
    // Two attempts: the owner may release the lock between tryLock() and getLockInfo().
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (lock->tryLock(std::chrono::milliseconds(0))) {
            qCDebug(log::lcDevice) << "Probe lock taken:" << toQString(toUtf8(path));
            held.add(serialNumber);
            return ProbeLock(serialNumber, std::move(lock));
        }
        if (lock->error() != QLockFile::LockFailedError) {
            return std::unexpected(
                makeError(ErrorCode::IoWriteFailed,
                          std::format("cannot write the lock file {} (QLockFile error {})",
                                      toUtf8(path), static_cast<int>(lock->error()))));
        }
        const auto owner = readOwner(*lock);
        if (!owner) {
            continue;
        }
        if (owner->pid == QCoreApplication::applicationPid() && !held.contains(serialNumber)) {
            // Our PID, but this process does not hold the probe: a stale lock of a dead process
            // whose PID was reused by us (review R15). Remove it and try once more.
            qCInfo(log::lcDevice) << "Removing a stale probe lock with this process's PID:"
                                  << toQString(toUtf8(path));
            lock->removeStaleLockFile();
            continue;
        }
        if (owner->pid == QCoreApplication::applicationPid()) {
            Error error =
                makeError(ErrorCode::ProbeOpenFailed, "the probe lock is held by this process");
            error.message = QCoreApplication::translate(
                                "hio::device", "The J-Link %1 is already used by this application.")
                                .arg(serialNumber)
                                .toStdString();
            return std::unexpected(std::move(error));
        }
        return std::unexpected(inUseError(serialNumber, *owner));
    }
    return std::unexpected(
        inUseError(serialNumber, ProbeLockOwner{.app = "?", .pid = 0, .lockedAt = std::nullopt}));
}

std::optional<ProbeLockOwner> ProbeLock::owner(std::uint32_t serialNumber) {
    return owner(serialNumber, defaultDirectory());
}

std::optional<ProbeLockOwner> ProbeLock::owner(std::uint32_t serialNumber,
                                               const fs::path& directory) {
    const fs::path path = lockPath(serialNumber, directory);
    const QString file = toQString(toUtf8(path));
    std::error_code ec;
    const auto lockedAt = [&path, &ec]() -> std::optional<fs::file_time_type> {
        const auto time = fs::last_write_time(path, ec);
        return ec ? std::nullopt : std::optional(time);
    };
    if (!fs::exists(path, ec)) {
        return std::nullopt;
    }
    const std::optional<LockFileInfo> info = readLockFile(file);
    if (!info) {
        // QLockFile treats a lock file it cannot read as not stale (no age rule): acquire()
        // fails with an unknown owner, so this is an unknown owner too - unless it just went away.
        if (!fs::exists(path, ec)) {
            return std::nullopt;
        }
        return ProbeLockOwner{.app = "?", .pid = 0, .lockedAt = lockedAt()};
    }
    // The same stale rules as acquire(): QLockFile's (review R37, R41) ...
    if (isApparentlyStale(*info)) {
        const Removal removal = staleLockRemoval(file);
        if (removal != Removal::Refused) {
            return std::nullopt; // tryLock() would take it over (or it is gone)
        }
    }
    // ... and our own PID (R15): a lock file with this process's PID is ours only while this
    // process holds the probe.
    if (info->pid == QCoreApplication::applicationPid()) {
        HeldSerials& held = heldSerials();
        const auto guard = held.guard();
        if (!held.contains(serialNumber)) {
            return std::nullopt;
        }
    }
    return ProbeLockOwner{.app = info->app.isEmpty() ? std::string("?") : info->app.toStdString(),
                          .pid = info->pid,
                          .lockedAt = lockedAt()};
}

ProbeLock::ProbeLock(std::uint32_t serialNumber, std::unique_ptr<QLockFile> lock)
    : m_serialNumber(serialNumber), m_lock(std::move(lock)) {}

ProbeLock::ProbeLock(ProbeLock&& other) noexcept = default;

ProbeLock& ProbeLock::operator=(ProbeLock&& other) noexcept {
    if (this != &other) {
        release();
        m_serialNumber = other.m_serialNumber;
        m_lock = std::move(other.m_lock);
    }
    return *this;
}

ProbeLock::~ProbeLock() { release(); }

void ProbeLock::release() {
    if (m_lock) {
        HeldSerials& held = heldSerials();
        const auto guard = held.guard(); // across unlock + remove (review R20)
        m_lock->unlock();
        m_lock.reset();
        held.remove(m_serialNumber);
        qCDebug(log::lcDevice) << "Probe lock released:" << m_serialNumber;
    }
}

} // namespace hio::device
