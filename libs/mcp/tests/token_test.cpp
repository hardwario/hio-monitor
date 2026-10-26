// TokenStore: create, reload, regenerate, bad contents, permission fix-up, constant-time compare
// (ADR-0102); directory rules and absolute paths (review R2).

#include "hio/core/error.h"
#include "hio/mcp/token.h"

#include <QByteArray>
#include <QDir>
#include <QString>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

namespace hio::mcp {
namespace {

namespace fs = std::filesystem;

class TokenStoreTest : public ::testing::Test {
public:
    void SetUp() override { ASSERT_TRUE(dir.isValid()); }

    [[nodiscard]] fs::path path(const char* relative) const {
        return fs::path(dir.path().toStdString()) / relative;
    }

    static void writeFile(const fs::path& file, const std::string& content) {
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream << content;
    }

    QTemporaryDir dir;
};

TEST_F(TokenStoreTest, GeneratesValidTokens) {
    const std::string a = TokenStore::generateToken();
    const std::string b = TokenStore::generateToken();
    EXPECT_EQ(a.size(), TokenStore::tokenChars);
    EXPECT_TRUE(TokenStore::isValidToken(a));
    EXPECT_NE(a, b);
    EXPECT_FALSE(TokenStore::isValidToken(a.substr(1)));
    EXPECT_FALSE(TokenStore::isValidToken(a.substr(1) + "="));
    EXPECT_FALSE(TokenStore::isValidToken(a.substr(1) + "+"));
}

TEST_F(TokenStoreTest, CreatesAndReloads) {
    const TokenStore store(path("config/sub/mcp-token"));
    auto created = store.loadOrCreate();
    ASSERT_TRUE(created.has_value()) << created.error().detail;
    auto loaded = store.load();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, *created);
    auto again = store.loadOrCreate();
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, *created);
    EXPECT_FALSE(fs::exists(path("config/sub/mcp-token.tmp")));

    auto regenerated = store.regenerate();
    ASSERT_TRUE(regenerated.has_value());
    EXPECT_NE(*regenerated, *created);
    EXPECT_EQ(store.load().value_or(""), *regenerated);
#ifndef _WIN32
    EXPECT_EQ(fs::status(store.path()).permissions() & fs::perms::mask,
              fs::perms::owner_read | fs::perms::owner_write);
    EXPECT_EQ(fs::status(store.path().parent_path()).permissions() & fs::perms::mask,
              fs::perms::owner_all);
#endif
}

TEST_F(TokenStoreTest, MissingFileAndBadContents) {
    const TokenStore missing(path("nothing"));
    auto result = missing.load();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::IoFileNotFound);

    fs::create_directories(path("bad"));
    fs::permissions(path("bad"), fs::perms::owner_all, fs::perm_options::replace);
    const TokenStore bad(path("bad/token"));
    for (const std::string& content : {std::string("short\n"), std::string(43, '!') + "\n",
                                       std::string(), TokenStore::generateToken() + "x\n"}) {
        writeFile(bad.path(), content);
        fs::permissions(bad.path(), fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace);
        auto loaded = bad.load();
        ASSERT_FALSE(loaded.has_value()) << content;
        EXPECT_EQ(loaded.error().code, ErrorCode::McpTokenFile);
        EXPECT_EQ(loaded.error().detail, bad.path().string());
        // loadOrCreate never overwrites an existing (broken) file.
        EXPECT_FALSE(bad.loadOrCreate().has_value());
    }
    // A token without the trailing newline and with CRLF are fine.
    const std::string token = TokenStore::generateToken();
    writeFile(bad.path(), token);
    EXPECT_EQ(bad.load().value_or(""), token);
    writeFile(bad.path(), token + "\r\n");
    EXPECT_EQ(bad.load().value_or(""), token);
}

#ifndef _WIN32
TEST_F(TokenStoreTest, FixesWrongPermissions) {
    const TokenStore store(path("perm/mcp-token"));
    ASSERT_TRUE(store.loadOrCreate().has_value());
    fs::permissions(store.path(),
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                        fs::perms::others_read,
                    fs::perm_options::replace);
    fs::permissions(store.path().parent_path(), fs::perms::owner_all | fs::perms::group_exec,
                    fs::perm_options::replace);
    ASSERT_TRUE(store.load().has_value());
    EXPECT_EQ(fs::status(store.path()).permissions() & fs::perms::mask,
              fs::perms::owner_read | fs::perms::owner_write);
    // A user-chosen directory is never changed (review R2), only checked.
    EXPECT_EQ(fs::status(store.path().parent_path()).permissions() & fs::perms::mask,
              fs::perms::owner_all | fs::perms::group_exec);
}

TEST_F(TokenStoreTest, UserDirectoryIsCheckedNotChanged) {
    const fs::perms home = fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                           fs::perms::others_read | fs::perms::others_exec; // 0755
    fs::create_directories(path("home"));
    fs::permissions(path("home"), home, fs::perm_options::replace);
    const TokenStore store(path("home/token"));
    ASSERT_TRUE(store.loadOrCreate().has_value());
    EXPECT_EQ(fs::status(path("home")).permissions() & fs::perms::mask, home);
    EXPECT_EQ(fs::status(store.path()).permissions() & fs::perms::mask,
              fs::perms::owner_read | fs::perms::owner_write);
    EXPECT_TRUE(store.load().has_value());

    // A directory writable by group or others is refused, for new and existing files.
    for (const fs::perms writable : {fs::perms::group_write, fs::perms::others_write}) {
        fs::permissions(path("home"), home | writable, fs::perm_options::replace);
        auto loaded = store.load();
        ASSERT_FALSE(loaded.has_value());
        EXPECT_EQ(loaded.error().code, ErrorCode::McpTokenFile);
        EXPECT_EQ(loaded.error().detail, store.path().string());
        const TokenStore fresh(path("home/other-token"));
        auto created = fresh.loadOrCreate();
        ASSERT_FALSE(created.has_value());
        EXPECT_EQ(created.error().code, ErrorCode::McpTokenFile);
        EXPECT_EQ(created.error().detail, fresh.path().string());
        EXPECT_FALSE(fs::exists(fresh.path()));
        EXPECT_EQ(fs::status(path("home")).permissions() & fs::perms::mask, home | writable);
    }
}

TEST_F(TokenStoreTest, CreatedDirectoryIsOwnerOnly) {
    const TokenStore store(path("made/by/store/token"));
    ASSERT_TRUE(store.loadOrCreate().has_value());
    EXPECT_EQ(fs::status(path("made/by/store")).permissions() & fs::perms::mask,
              fs::perms::owner_all);
}

// Linux only: QStandardPaths follows HOME there (test mode: ~/.qttest/config); on macOS it asks
// the system for the home folder, so the default location cannot be moved into a temporary one.
#ifdef __linux__
TEST_F(TokenStoreTest, DefaultDirectoryIsHardened) {
    // The default location under a temporary HOME (QStandardPaths test mode: ~/.qttest/config).
    const QByteArray savedHome = qgetenv("HOME");
    ASSERT_TRUE(qputenv("HOME", dir.path().toLocal8Bit()));
    const fs::path standard = TokenStore::defaultPath();
    ASSERT_FALSE(standard.empty());
    ASSERT_EQ(standard.string().rfind(dir.path().toStdString(), 0), 0U) << standard.string();
    fs::create_directories(standard.parent_path());
    fs::permissions(standard.parent_path(),
                    fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec,
                    fs::perm_options::replace);
    const TokenStore store(standard);
    const bool created = store.loadOrCreate().has_value();
    const fs::perms mode = fs::status(standard.parent_path()).permissions() & fs::perms::mask;
    ASSERT_TRUE(qputenv("HOME", savedHome));
    ASSERT_TRUE(created);
    EXPECT_EQ(mode, fs::perms::owner_all);
}
#endif

TEST_F(TokenStoreTest, RelativePathIsMadeAbsolute) {
    const QString saved = QDir::currentPath();
    ASSERT_TRUE(QDir::setCurrent(dir.path()));
    const TokenStore store(fs::path("mcp-token"));
    const bool created = store.loadOrCreate().has_value();
    const bool loaded = store.load().has_value();
    ASSERT_TRUE(QDir::setCurrent(saved));
    EXPECT_TRUE(store.path().is_absolute());
    EXPECT_EQ(store.path(), path("mcp-token"));
    EXPECT_TRUE(created);
    EXPECT_TRUE(loaded);
    EXPECT_TRUE(fs::exists(path("mcp-token")));
    // The temporary directory (0700, created by QTemporaryDir) is not changed.
}

TEST_F(TokenStoreTest, LeftoverTemporaryFileIsReplaced) {
    fs::create_directories(path("left"));
    fs::permissions(path("left"), fs::perms::owner_all, fs::perm_options::replace);
    writeFile(path("left/token.tmp"), "stale");
    fs::permissions(path("left/token.tmp"), fs::perms::all, fs::perm_options::replace);
    const TokenStore store(path("left/token"));
    ASSERT_TRUE(store.loadOrCreate().has_value());
    EXPECT_FALSE(fs::exists(path("left/token.tmp")));
    EXPECT_EQ(fs::status(store.path()).permissions() & fs::perms::mask,
              fs::perms::owner_read | fs::perms::owner_write);
}

TEST_F(TokenStoreTest, RefusesSymbolicLinks) {
    const TokenStore real(path("links/real"));
    ASSERT_TRUE(real.loadOrCreate().has_value());
    fs::create_symlink(real.path(), path("links/link"));
    const TokenStore link(path("links/link"));
    auto result = link.load();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::McpTokenFile);
}
#endif

TEST(ConstantTimeEquals, ComparesWholeStrings) {
    EXPECT_TRUE(constantTimeEquals("", ""));
    EXPECT_TRUE(constantTimeEquals("abc", "abc"));
    EXPECT_FALSE(constantTimeEquals("abc", "abd"));
    EXPECT_FALSE(constantTimeEquals("abc", "ab"));
    EXPECT_FALSE(constantTimeEquals("ab", "abc"));
    EXPECT_FALSE(constantTimeEquals("", "a"));
    EXPECT_FALSE(constantTimeEquals(std::string_view("a\0", 2), "a"));
}

} // namespace
} // namespace hio::mcp
