#include "hio/mcp/token.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"

#include <QByteArray>
#include <QFile>
#include <QFileDevice>
#include <QIODevice>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QString>
#include <QtTypes>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <memory>
// NOMINMAX and WIN32_LEAN_AND_MEAN are set by libs/mcp/CMakeLists.txt.
#include <windows.h>
// <sddl.h> needs <windows.h> first (review R3 read: the sorted order did not compile).
#include <sddl.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace hio::mcp {

namespace fs = std::filesystem;

namespace {

std::unexpected<Error> tokenFileError(const fs::path& path) {
    return std::unexpected(makeError(ErrorCode::McpTokenFile, toUtf8(path)));
}

#ifdef _WIN32
struct LocalFreeDeleter {
    void operator()(void* memory) const noexcept { LocalFree(memory); }
};
struct HandleCloser {
    void operator()(void* handle) const noexcept { CloseHandle(handle); }
};
using OwnedDescriptor = std::unique_ptr<void, LocalFreeDeleter>;

// Protected DACL: full access for the owner only ("OW" = OWNER RIGHTS).
OwnedDescriptor ownerOnlyDescriptor() {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;OW)", SDDL_REVISION_1,
                                                             &descriptor, nullptr) == 0) {
        return {};
    }
    return OwnedDescriptor(descriptor);
}

bool applyOwnerOnlyAcl(const fs::path& path) {
    const OwnedDescriptor descriptor = ownerOnlyDescriptor();
    return descriptor &&
           SetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, descriptor.get()) != 0;
}

// Creates `path` (it must not exist) with the owner-only descriptor already applied and writes
// `content` (review R3: no window in which the token is readable by others).
bool writeNewOwnerOnlyFile(const fs::path& path, const QByteArray& content) {
    const OwnedDescriptor descriptor = ownerOnlyDescriptor();
    if (!descriptor) {
        return false;
    }
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor.get();
    attributes.bInheritHandle = FALSE;
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    const std::unique_ptr<void, HandleCloser> owned(handle);
    DWORD written = 0;
    const auto size = static_cast<DWORD>(content.size());
    return WriteFile(handle, content.constData(), size, &written, nullptr) != 0 &&
           written == size && FlushFileBuffers(handle) != 0;
}
#else
// Fixes `mode` of an owned file or directory; refuses symbolic links and other owners.
bool checkPosixMode(const fs::path& path, fs::perms wanted, bool directory) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error || fs::is_symlink(status) ||
        (directory ? !fs::is_directory(status) : !fs::is_regular_file(status))) {
        return false;
    }
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0 || info.st_uid != ::geteuid()) {
        return false;
    }
    const fs::perms current = status.permissions() & fs::perms::mask;
    if (current != wanted) {
        qCWarning(hio::log::lcMcp).noquote()
            << "MCP token: fixed the permissions of" << QString::fromStdString(toUtf8(path));
        fs::permissions(path, wanted, fs::perm_options::replace, error);
        if (error) {
            return false;
        }
    }
    return true;
}

// A directory the user chose is never changed; it must not be writable by group or others (they
// could replace the token file).
bool checkUserDirectory(const fs::path& dir) {
    std::error_code error;
    const fs::file_status status = fs::status(dir, error);
    if (error || !fs::is_directory(status)) {
        return false;
    }
    if ((status.permissions() & (fs::perms::group_write | fs::perms::others_write)) !=
        fs::perms::none) {
        qCWarning(hio::log::lcMcp).noquote()
            << "MCP token: the directory" << QString::fromStdString(toUtf8(dir))
            << "is writable by other users; refused";
        return false;
    }
    return true;
}

// Creates `path` (it must not exist) with mode 0600 and writes `content`.
bool writeNewOwnerOnlyFile(const fs::path& path, const QByteArray& content) {
    QFile file(QString::fromStdU16String(path.u16string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly,
                   QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        return false;
    }
    if (file.write(content) != content.size() || !file.flush()) {
        file.remove();
        return false;
    }
    return true;
}
#endif

fs::path absolutePath(const fs::path& file) {
    if (file.empty()) {
        return file;
    }
    std::error_code error;
    const fs::path absolute = fs::absolute(file, error);
    return error ? file : absolute.lexically_normal();
}

} // namespace

TokenStore::TokenStore(const fs::path& file) : m_file(absolutePath(file)) {
    const fs::path standard = absolutePath(defaultPath());
    m_defaultLocation = !standard.empty() && m_file == standard;
}

fs::path TokenStore::defaultPath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) {
        return {};
    }
    return fs::path(dir.toStdU16String()) / "mcp-token";
}

std::string TokenStore::generateToken() {
    QByteArray bytes(static_cast<qsizetype>(tokenBytes), '\0');
    for (char& byte : bytes) {
        byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
        .toStdString();
}

bool TokenStore::isValidToken(std::string_view token) noexcept {
    return token.size() == tokenChars && std::ranges::all_of(token, [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '-' || c == '_';
           });
}

Status TokenStore::secure(bool hardenDirectory) const {
    const fs::path dir = m_file.parent_path();
#ifdef _WIN32
    if ((hardenDirectory && !applyOwnerOnlyAcl(dir)) || !applyOwnerOnlyAcl(m_file)) {
        return tokenFileError(m_file);
    }
#else
    const bool directoryOk =
        hardenDirectory ? checkPosixMode(dir, fs::perms::owner_all, true) : checkUserDirectory(dir);
    if (!directoryOk ||
        !checkPosixMode(m_file, fs::perms::owner_read | fs::perms::owner_write, false)) {
        return tokenFileError(m_file);
    }
#endif
    return {};
}

Result<std::string> TokenStore::load() const {
    std::error_code error;
    if (!fs::exists(fs::symlink_status(m_file, error))) {
        return std::unexpected(makeError(ErrorCode::IoFileNotFound, toUtf8(m_file)));
    }
    if (auto secured = secure(m_defaultLocation); !secured) {
        return std::unexpected(secured.error());
    }
    QFile file(QString::fromStdU16String(m_file.u16string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return tokenFileError(m_file);
    }
    // The first line; a valid file is 44 bytes, read a little more to detect garbage.
    const QByteArray head = file.read(static_cast<qint64>(tokenChars) + 3);
    const qsizetype newline = head.indexOf('\n');
    std::string token = (newline < 0 ? head : head.left(newline)).trimmed().toStdString();
    if (!isValidToken(token)) {
        return tokenFileError(m_file);
    }
    return token;
}

Result<std::string> TokenStore::loadOrCreate() const {
    std::error_code error;
    if (fs::exists(fs::symlink_status(m_file, error))) {
        return load();
    }
    return regenerate();
}

Result<std::string> TokenStore::regenerate() const {
    std::string token = generateToken();
    if (auto written = write(token); !written) {
        return std::unexpected(written.error());
    }
    return token;
}

Status TokenStore::write(const std::string& token) const {
    std::error_code error;
    const fs::path dir = m_file.parent_path();
    bool created = false;
    if (!dir.empty() && !fs::exists(dir, error)) {
        fs::create_directories(dir, error);
        if (error) {
            return tokenFileError(m_file);
        }
        created = true;
#ifndef _WIN32
        fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, error);
        if (error) {
            return tokenFileError(m_file);
        }
#endif
    }
    const bool hardenDirectory = m_defaultLocation || created;
#ifndef _WIN32
    // A user directory is checked before anything is written into it.
    if (!hardenDirectory && !checkUserDirectory(dir)) {
        return tokenFileError(m_file);
    }
#endif
    fs::path temporary = m_file;
    temporary += ".tmp";
    fs::remove(temporary, error); // a leftover of an interrupted write (never followed if a link)
    // Created owner-only before any secret is written (POSIX 0600 / Windows owner-only DACL).
    if (!writeNewOwnerOnlyFile(temporary, QByteArray::fromStdString(token) + '\n')) {
        fs::remove(temporary, error);
        return tokenFileError(m_file);
    }
    fs::rename(temporary, m_file, error);
    if (error) {
        fs::remove(temporary, error);
        return tokenFileError(m_file);
    }
    return secure(hardenDirectory);
}

bool constantTimeEquals(std::string_view a, std::string_view b) noexcept {
    // Every byte of the longer input is visited; the length difference is folded in.
    const std::size_t length = std::max(a.size(), b.size());
    std::uint8_t difference = a.size() == b.size() ? 0 : 1;
    for (std::size_t i = 0; i < length; ++i) {
        const auto x = static_cast<std::uint8_t>(i < a.size() ? a[i] : 0);
        const auto y = static_cast<std::uint8_t>(i < b.size() ? b[i] : 0);
        difference = static_cast<std::uint8_t>(difference | (x ^ y));
    }
    return difference == 0;
}

} // namespace hio::mcp
