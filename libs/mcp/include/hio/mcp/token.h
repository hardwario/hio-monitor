#pragma once

// TokenStore - the MCP HTTP bearer token in an owner-only file (ADR-0102, plan/features/0013 "HTTP
// transport"). Used by `hio mcp --token-file` and by the GUI (plan 0014, default location).
//
// Token: 32 random bytes from QRandomGenerator::system(), base64url without padding = 43
// characters. File: the token on the first line (+ "\n"). The path is made absolute (relative to
// the current directory) when the store is created, so a bare file name works.
//
// Permissions (review R2): the directory is hardened only when it is the default location
// (defaultPath()) or when the store created it; a directory the user chose (--token-file) is never
// changed.
//   POSIX   - file 0600, checked on every load; a wrong mode is fixed and a hio.mcp warning is
//             logged. A symbolic link or a file of another user is refused. The default (or newly
//             created) directory is set to 0700 the same way; a user directory that is writable by
//             group or others is refused (mcp.token_file).
//   Windows - a protected DACL that grants access to the owner only ("OWNER RIGHTS"), applied to
//             the file whenever the token is written or loaded, and to the directory under the rule
//             above (no check of the previous ACL and none of a user directory; minimal Win32
//             implementation, compiled only on Windows).
// New contents are written to "<file>.tmp" and renamed over the file. The temporary file is
// created owner-only before the token is written (POSIX: mode 0600 at creation; Windows: created
// with the owner-only security descriptor, review R3).
// Errors: mcp.token_file (detail: the path), for unreadable/unwritable files, wrong owner,
// symbolic links and invalid contents.
//
// Thread safety: none (one owner).

#include "hio/core/result.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace hio::mcp {

class TokenStore {
public:
    static constexpr std::size_t tokenBytes = 32;
    static constexpr std::size_t tokenChars = 43;

    explicit TokenStore(const std::filesystem::path& file);

    // <AppConfigLocation>/mcp-token (requires hio::initApplicationIdentity()).
    [[nodiscard]] static std::filesystem::path defaultPath();
    [[nodiscard]] static std::string generateToken();
    // 43 characters of [A-Za-z0-9_-].
    [[nodiscard]] static bool isValidToken(std::string_view token) noexcept;

    // Reads the token of an existing file (io.file_not_found when it does not exist).
    [[nodiscard]] Result<std::string> load() const;
    // load(), or create the file with a new token when it does not exist.
    [[nodiscard]] Result<std::string> loadOrCreate() const;
    // Writes a new token (the old one stops working for new requests at once in the caller).
    [[nodiscard]] Result<std::string> regenerate() const;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_file; }

private:
    [[nodiscard]] Status write(const std::string& token) const;
    // `hardenDirectory`: the directory is the default location or was created by write().
    [[nodiscard]] Status secure(bool hardenDirectory) const;

    std::filesystem::path m_file; // absolute
    bool m_defaultLocation = false;
};

// Compares two secrets in time that depends only on their lengths.
[[nodiscard]] bool constantTimeEquals(std::string_view a, std::string_view b) noexcept;

} // namespace hio::mcp
