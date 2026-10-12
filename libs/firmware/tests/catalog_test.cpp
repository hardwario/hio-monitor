// FirmwareCache (LRU, atomic writes) and FirmwareCatalog against a local HTTP fixture
// (plan/features/0003, phase A4).

#include "hex_builder.h"
#include "hio/core/error.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_catalog.h"
#include "http_fixture.h"

#include <QByteArray>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::firmware::CacheLimits;
using hio::firmware::CatalogOptions;
using hio::firmware::DownloadOptions;
using hio::firmware::FirmwareCache;
using hio::firmware::FirmwareCatalog;
using hio::firmware::FirmwareId;
using hio::firmware::waitForDownload;
using hio::firmware::test::bytesOf;
using hio::firmware::test::eofRecord;
using hio::firmware::test::hexRecord;
using hio::firmware::test::HttpFixture;

constexpr const char* idText = "0123456789ABCDEF0123456789abcdef";

FirmwareId id(const char* text = idText) { return *FirmwareId::parse(text); }

std::string hexText(std::uint8_t value = 1) { return hexRecord(0, 0, {value, 2, 3}) + eofRecord(); }

std::string path(const FirmwareId& firmware) { return "/chester/" + firmware.value() + "/hex"; }

class Catalog : public ::testing::Test {
protected:
    [[nodiscard]] fs::path cacheDir() const {
        return fs::path(m_dir.path().toStdU16String()) / "cache";
    }

    [[nodiscard]] std::unique_ptr<FirmwareCatalog>
    catalog(FirmwareCache* cache,
            std::chrono::milliseconds timeout = std::chrono::milliseconds(60000)) {
        return std::make_unique<FirmwareCatalog>(CatalogOptions{.baseUrl = QUrl(m_http.baseUrl()),
                                                                .transferTimeout = timeout,
                                                                .maxBodyBytes = 1024},
                                                 cache);
    }

    void serve(const FirmwareId& firmware, const HttpFixture::Response& response) {
        m_http.set(QString::fromStdString(path(firmware)), response);
    }
    [[nodiscard]] int requests(const FirmwareId& firmware) const {
        return m_http.requests(QString::fromStdString(path(firmware)));
    }

private:
    QTemporaryDir m_dir;
    HttpFixture m_http;
};

TEST(FirmwareIdTest, Parse) {
    EXPECT_EQ(id().value(), "0123456789abcdef0123456789abcdef");
    EXPECT_FALSE(FirmwareId::parse("0123"));
    EXPECT_FALSE(FirmwareId::parse("0123456789abcdef0123456789abcdeg"));
    EXPECT_TRUE(FirmwareId::looksLikeId(idText));
    EXPECT_FALSE(FirmwareId::looksLikeId("firmware.hex"));
}

TEST_F(Catalog, DownloadsParsesAndCaches) {
    FirmwareCache cache(cacheDir());
    auto catalog = this->catalog(&cache);
    serve(id(), HttpFixture::Response::ok(QByteArray::fromStdString(hexText())));
    auto operation = catalog->download(id(), {});
    const auto result = waitForDownload(*operation);
    ASSERT_TRUE(result) << result.error().detail;
    EXPECT_FALSE(result->fromCache);
    EXPECT_EQ(result->image->totalBytes(), 3U);
    EXPECT_TRUE(fs::exists(cache.fileFor(id())));
    // Second time: a cache hit, no request.
    auto again = catalog->download(id(), {});
    const auto cached = waitForDownload(*again);
    ASSERT_TRUE(cached);
    EXPECT_TRUE(cached->fromCache);
    EXPECT_EQ(requests(id()), 1);
    // --no-cache downloads again (and stores).
    auto forced = catalog->download(id(), DownloadOptions{.noCache = true});
    ASSERT_TRUE(waitForDownload(*forced));
    EXPECT_EQ(requests(id()), 2);
}

TEST_F(Catalog, NotFound) {
    auto catalog = this->catalog(nullptr);
    auto operation = catalog->download(id(), {});
    const auto result = waitForDownload(*operation);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::FirmwareNotFoundInCatalog);
    EXPECT_NE(result.error().message.find(id().value()), std::string::npos);
}

TEST_F(Catalog, ServerError) {
    auto catalog = this->catalog(nullptr);
    serve(id(), HttpFixture::Response::withStatus(500, "oops"));
    auto operation = catalog->download(id(), {});
    const auto result = waitForDownload(*operation);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::NetworkDownloadFailed);
    EXPECT_NE(result.error().detail.find("HTTP 500"), std::string::npos) << result.error().detail;
}

TEST_F(Catalog, Timeout) {
    auto catalog = this->catalog(nullptr, std::chrono::milliseconds(300));
    serve(id(), HttpFixture::Response::hanging());
    auto operation = catalog->download(id(), {});
    const auto result = waitForDownload(*operation);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::NetworkDownloadFailed);
}

TEST_F(Catalog, Oversize) {
    auto catalog = this->catalog(nullptr);
    serve(id(), HttpFixture::Response::ok(QByteArray(2048, ':')));
    auto operation = catalog->download(id(), {});
    const auto result = waitForDownload(*operation);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::NetworkDownloadFailed);
    EXPECT_NE(result.error().detail.find("larger than 1024"), std::string::npos);
}

TEST_F(Catalog, InvalidHexIsNotCached) {
    FirmwareCache cache(cacheDir());
    auto catalog = this->catalog(&cache);
    serve(id(), HttpFixture::Response::ok("this is not hex"));
    auto operation = catalog->download(id(), {});
    const auto result = waitForDownload(*operation);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::FirmwareHexParseError);
    EXPECT_FALSE(fs::exists(cache.fileFor(id())));
}

TEST_F(Catalog, Cancel) {
    auto catalog = this->catalog(nullptr);
    serve(id(), HttpFixture::Response::hanging());
    std::stop_source stop;
    auto operation = catalog->download(id(), {}, {}, stop.get_token());
    stop.request_stop();
    const auto result = waitForDownload(*operation);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}

// Review R8: redirects are followed only to https - never to http, also from an http base.
TEST_F(Catalog, RedirectsOnlyToHttps) {
    auto catalog = this->catalog(nullptr);
    const FirmwareId other = id("ffffffffffffffffffffffffffffffff");
    serve(id(), HttpFixture::Response::redirect(QByteArray::fromStdString(path(other))));
    serve(other, HttpFixture::Response::ok(QByteArray::fromStdString(hexText())));
    auto operation = catalog->download(id(), {});
    const auto result = waitForDownload(*operation);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::NetworkDownloadFailed);
    EXPECT_NE(result.error().detail.find("redirect"), std::string::npos) << result.error().detail;
    EXPECT_EQ(requests(other), 0);
}

TEST_F(Catalog, CorruptCacheEntryIsDeletedAndDownloadedAgain) {
    FirmwareCache cache(cacheDir());
    fs::create_directories(cacheDir());
    std::ofstream(cache.fileFor(id())) << "corrupt";
    auto catalog = this->catalog(&cache);
    serve(id(), HttpFixture::Response::ok(QByteArray::fromStdString(hexText())));
    auto operation = catalog->download(id(), {});
    const auto result = waitForDownload(*operation);
    ASSERT_TRUE(result);
    EXPECT_FALSE(result->fromCache);
    EXPECT_EQ(requests(id()), 1);
}

TEST_F(Catalog, UnwritableCacheIsAWarning) {
    const fs::path blocker = cacheDir();
    fs::create_directories(blocker.parent_path());
    std::ofstream(blocker) << "a file where the cache directory should be";
    FirmwareCache cache(blocker);
    auto catalog = this->catalog(&cache);
    serve(id(), HttpFixture::Response::ok(QByteArray::fromStdString(hexText())));
    auto operation = catalog->download(id(), {});
    const auto result = waitForDownload(*operation);
    ASSERT_TRUE(result);
    ASSERT_EQ(result->warnings.size(), 1U);
    EXPECT_EQ(result->warnings[0].code, ErrorCode::IoWriteFailed);
}

TEST_F(Catalog, LruEviction) {
    FirmwareCache cache(cacheDir(), CacheLimits{.maxFiles = 2, .maxBytes = 1 << 20});
    std::vector<FirmwareId> ids;
    for (int i = 0; i < 3; ++i) {
        ids.push_back(id(std::string(31, '0').append(std::to_string(i)).c_str()));
        ASSERT_TRUE(cache.store(ids.back(), bytesOf(hexText(static_cast<std::uint8_t>(i)))));
        // Distinct modification times (file systems with coarse timestamps).
        fs::last_write_time(cache.fileFor(ids.back()),
                            fs::file_time_type::clock::now() + std::chrono::seconds(i));
    }
    EXPECT_FALSE(fs::exists(cache.fileFor(ids[0])));
    EXPECT_TRUE(fs::exists(cache.fileFor(ids[1])));
    EXPECT_TRUE(fs::exists(cache.fileFor(ids[2])));
    // A hit touches the entry: ids[1] becomes the newest, so the next insert evicts ids[2].
    ASSERT_TRUE(cache.lookup(ids[1]));
    fs::last_write_time(cache.fileFor(ids[1]),
                        fs::file_time_type::clock::now() + std::chrono::seconds(10));
    const FirmwareId fourth = id("ffffffffffffffffffffffffffffff03");
    ASSERT_TRUE(cache.store(fourth, bytesOf(hexText())));
    EXPECT_TRUE(fs::exists(cache.fileFor(ids[1])));
    EXPECT_FALSE(fs::exists(cache.fileFor(ids[2])));
    EXPECT_TRUE(fs::exists(cache.fileFor(fourth)));
}

TEST_F(Catalog, ByteLimitEviction) {
    const std::string text = hexText();
    FirmwareCache cache(cacheDir(), CacheLimits{.maxFiles = 100, .maxBytes = text.size() * 2});
    ASSERT_TRUE(cache.store(id("00000000000000000000000000000001"), bytesOf(text)));
    fs::last_write_time(cache.fileFor(id("00000000000000000000000000000001")),
                        fs::file_time_type::clock::now() - std::chrono::seconds(100));
    ASSERT_TRUE(cache.store(id("00000000000000000000000000000002"), bytesOf(text)));
    fs::last_write_time(cache.fileFor(id("00000000000000000000000000000002")),
                        fs::file_time_type::clock::now() - std::chrono::seconds(50));
    ASSERT_TRUE(cache.store(id("00000000000000000000000000000003"), bytesOf(text)));
    EXPECT_FALSE(fs::exists(cache.fileFor(id("00000000000000000000000000000001"))));
    EXPECT_TRUE(fs::exists(cache.fileFor(id("00000000000000000000000000000003"))));
}

} // namespace
