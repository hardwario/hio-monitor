// Plan 0021 (ADR-0165, ADR-0166): the designated firmware download with SHA-256 verification and
// cache, the catalog page parser on the saved page and on mutated input, the catalog lookup and the
// lookup-verified ID download - against the local HTTP fixture.

#include "hex_builder.h"
#include "hio/core/error.h"
#include "hio/firmware/designated_firmware.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_catalog.h"
#include "http_fixture.h"
#include "zip_builder.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::firmware::CatalogOptions;
using hio::firmware::DesignatedFirmware;
using hio::firmware::DownloadOptions;
using hio::firmware::FirmwareCache;
using hio::firmware::FirmwareCatalog;
using hio::firmware::FirmwareId;
using hio::firmware::FirmwareKind;
using hio::firmware::parseCatalogPage;
using hio::firmware::Sha256;
using hio::firmware::sha256Of;
using hio::firmware::toHex;
using hio::firmware::waitForDownload;
using hio::firmware::waitForLookup;
using hio::firmware::test::eofRecord;
using hio::firmware::test::hexRecord;
using hio::firmware::test::HttpFixture;
using hio::firmware::test::zipBytes;

Sha256 shaOf(const QByteArray& bytes) {
    return sha256Of(
        std::as_bytes(std::span(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
}

QByteArray bundleZip() {
    return zipBytes(
        {{.name = "application.hex", .data = hexRecord(0, 0, {1, 2, 3, 4}) + eofRecord()},
         {.name = "modem.zip", .data = "modem"}});
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray savedPage() {
    return readFile(QStringLiteral(HIO_FIRMWARE_TEST_DATA
                                   "/catalog_page_001543e218a04ffdbf65082532202736.html"));
}

class Designated : public ::testing::Test {
protected:
    [[nodiscard]] fs::path cacheDir() const {
        return fs::path(m_dir.path().toStdU16String()) / "cache";
    }
    [[nodiscard]] std::unique_ptr<FirmwareCatalog> catalog(FirmwareCache* cache) {
        return std::make_unique<FirmwareCatalog>(
            CatalogOptions{.baseUrl = QUrl(m_http.baseUrl()),
                           .transferTimeout = std::chrono::milliseconds(60000),
                           .maxBodyBytes = 64ULL * 1024,
                           .lookupTimeout = std::chrono::milliseconds(15000),
                           .maxPageBytes = 64ULL * 1024},
            cache);
    }
    // The designated entry served by the fixture at /lte.zip, with the hash of `pinned`.
    [[nodiscard]] DesignatedFirmware firmware(const QByteArray& pinned) {
        m_url = (m_http.baseUrl() + QStringLiteral("/lte.zip")).toStdString();
        return DesignatedFirmware{.name = "HARDWARIO LTE firmware",
                                  .version = "1.7.0",
                                  .url = m_url,
                                  .sha256 = shaOf(pinned),
                                  .kind = FirmwareKind::LteBundle};
    }
    void serve(const QString& path, const HttpFixture::Response& response) {
        m_http.set(path, response);
    }
    [[nodiscard]] int requests(const QString& path) const { return m_http.requests(path); }
    [[nodiscard]] QString baseUrl() const { return m_http.baseUrl(); }

private:
    QTemporaryDir m_dir;
    HttpFixture m_http;
    std::string m_url; // the string_view of DesignatedFirmware::url points here
};

// ---- SHA-256 helpers -----------------------------------------------------------------------

TEST(Sha256Test, HexRoundTripAndKnownDigest) {
    const std::string_view hex = "95d301a11738bcd9a9df4726159361d64fd8f681c3546f479d46b49f4bf0fe49";
    constexpr Sha256 compiled = hio::firmware::sha256FromHex(
        "95d301a11738bcd9a9df4726159361d64fd8f681c3546f479d46b49f4bf0fe49");
    EXPECT_EQ(hio::firmware::parseSha256(hex), compiled);
    EXPECT_EQ(toHex(compiled), hex);
    EXPECT_EQ(hio::firmware::parseSha256(
                  "95D301A11738BCD9A9DF4726159361D64FD8F681C3546F479D46B49F4BF0FE49"),
              compiled);
    EXPECT_FALSE(hio::firmware::parseSha256(hex.substr(1)));
    EXPECT_FALSE(hio::firmware::parseSha256(std::string(64, 'g')));
    // SHA-256 of "abc" (FIPS 180-2).
    EXPECT_EQ(toHex(shaOf(QByteArrayLiteral("abc"))),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// ---- designated download -------------------------------------------------------------------

TEST_F(Designated, DownloadVerifiesCachesAndClassifies) {
    FirmwareCache cache(cacheDir());
    auto catalog = this->catalog(&cache);
    const QByteArray zip = bundleZip();
    serve(QStringLiteral("/chester/lte.zip"), HttpFixture::Response::ok(zip));
    const DesignatedFirmware designated = firmware(zip);
    auto operation = catalog->downloadDesignated(designated);
    const auto result = waitForDownload(*operation);
    ASSERT_TRUE(result) << result.error().detail;
    EXPECT_FALSE(result->fromCache);
    EXPECT_EQ(result->fileBytes, static_cast<std::uint64_t>(zip.size()));
    ASSERT_TRUE(result->bundle);
    EXPECT_EQ(result->bundle->application.totalBytes(), 4U);
    EXPECT_TRUE(fs::exists(result->bundle->modemZip));
    const fs::path cached = cache.designatedFileFor(designated.sha256);
    EXPECT_EQ(cached.filename().string(), "lte-" + toHex(designated.sha256) + ".zip");
    EXPECT_TRUE(fs::exists(cached));
    EXPECT_EQ(requests(QStringLiteral("/chester/lte.zip")), 1);
}

TEST_F(Designated, CacheHitIsReverifiedWithoutARequest) {
    FirmwareCache cache(cacheDir());
    auto catalog = this->catalog(&cache);
    const QByteArray zip = bundleZip();
    serve(QStringLiteral("/chester/lte.zip"), HttpFixture::Response::ok(zip));
    const DesignatedFirmware designated = firmware(zip);
    ASSERT_TRUE(waitForDownload(*catalog->downloadDesignated(designated)));
    auto again = catalog->downloadDesignated(designated);
    const auto hit = waitForDownload(*again);
    ASSERT_TRUE(hit) << hit.error().detail;
    EXPECT_TRUE(hit->fromCache);
    EXPECT_EQ(requests(QStringLiteral("/chester/lte.zip")), 1);

    // A damaged cache entry fails the re-verification: deleted, downloaded again.
    const fs::path cached = cache.designatedFileFor(designated.sha256);
    {
        QFile file(QString::fromStdU16String(cached.u16string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("damaged");
    }
    auto third = catalog->downloadDesignated(designated);
    const auto downloaded = waitForDownload(*third);
    ASSERT_TRUE(downloaded) << downloaded.error().detail;
    EXPECT_FALSE(downloaded->fromCache);
    EXPECT_EQ(requests(QStringLiteral("/chester/lte.zip")), 2);
    EXPECT_EQ(readFile(QString::fromStdU16String(cached.u16string())), zip);

    // noCache downloads again.
    auto forced = catalog->downloadDesignated(designated, DownloadOptions{.noCache = true});
    ASSERT_TRUE(waitForDownload(*forced));
    EXPECT_EQ(requests(QStringLiteral("/chester/lte.zip")), 3);
}

// Review F5: a cache hit is classified from the verified bytes, not from the cache file read again
// (removing the file right after the lookup does not matter).
TEST_F(Designated, CacheHitUsesTheVerifiedBytes) {
    FirmwareCache cache(cacheDir());
    auto catalog = this->catalog(&cache);
    const QByteArray zip = bundleZip();
    serve(QStringLiteral("/chester/lte.zip"), HttpFixture::Response::ok(zip));
    const DesignatedFirmware designated = firmware(zip);
    ASSERT_TRUE(waitForDownload(*catalog->downloadDesignated(designated)));
    const auto verified = cache.lookupDesignated(designated.sha256);
    ASSERT_TRUE(verified.has_value());
    EXPECT_EQ(verified.value_or(std::vector<std::byte>{}).size(),
              static_cast<std::size_t>(zip.size()));
    auto hit = catalog->downloadDesignated(designated); // verified now, classified later
    std::error_code ec;
    fs::remove(cache.designatedFileFor(designated.sha256), ec);
    const auto result = waitForDownload(*hit);
    ASSERT_TRUE(result) << result.error().detail;
    EXPECT_TRUE(result->fromCache);
    ASSERT_TRUE(result->bundle);
    EXPECT_EQ(result->bundle->application.totalBytes(), 4U);
}

TEST_F(Designated, HashMismatchIsDiscarded) {
    FirmwareCache cache(cacheDir());
    auto catalog = this->catalog(&cache);
    const QByteArray zip = bundleZip();
    serve(QStringLiteral("/chester/lte.zip"), HttpFixture::Response::ok(zip));
    const DesignatedFirmware designated = firmware(QByteArrayLiteral("another file"));
    auto operation = catalog->downloadDesignated(designated);
    const auto result = waitForDownload(*operation);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::FirmwareChecksumMismatch);
    EXPECT_NE(result.error().detail.find(toHex(shaOf(zip))), std::string::npos);
    EXPECT_FALSE(fs::exists(cache.designatedFileFor(designated.sha256)));
    EXPECT_FALSE(fs::exists(cache.designatedFileFor(shaOf(zip))));
}

TEST_F(Designated, WrongKindIsInvalid) {
    auto catalog = this->catalog(nullptr);
    const QByteArray hex = QByteArray::fromStdString(hexRecord(0, 0, {1}) + eofRecord());
    serve(QStringLiteral("/chester/lte.zip"), HttpFixture::Response::ok(hex));
    auto operation = catalog->downloadDesignated(firmware(hex));
    const auto result = waitForDownload(*operation);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::FirmwareInvalidImage);
}

TEST_F(Designated, WithoutCacheTheBundleIsStillLoaded) {
    auto catalog = this->catalog(nullptr);
    const QByteArray zip = bundleZip();
    serve(QStringLiteral("/chester/lte.zip"), HttpFixture::Response::ok(zip));
    const auto result = waitForDownload(*catalog->downloadDesignated(firmware(zip)));
    ASSERT_TRUE(result) << result.error().detail;
    ASSERT_TRUE(result->bundle);
    EXPECT_TRUE(fs::exists(result->bundle->modemZip));
}

TEST_F(Designated, DownloadFailureAndCancel) {
    auto catalog = this->catalog(nullptr);
    const QByteArray zip = bundleZip();
    // 404 is a failed download (not "not in the catalog").
    auto missing = catalog->downloadDesignated(firmware(zip));
    const auto failed = waitForDownload(*missing);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, ErrorCode::NetworkDownloadFailed);
    EXPECT_NE(failed.error().message.find("HARDWARIO LTE firmware 1.7.0"), std::string::npos);

    serve(QStringLiteral("/chester/lte.zip"), HttpFixture::Response::hanging());
    std::stop_source stop;
    auto hanging = catalog->downloadDesignated(firmware(zip), {}, {}, stop.get_token());
    stop.request_stop();
    const auto cancelled = waitForDownload(*hanging);
    ASSERT_FALSE(cancelled);
    EXPECT_EQ(cancelled.error().code, ErrorCode::Cancelled);
}

TEST_F(Designated, CountsAgainstTheCacheLimits) {
    FirmwareCache cache(cacheDir(), hio::firmware::CacheLimits{.maxFiles = 1, .maxBytes = 1 << 20});
    auto catalog = this->catalog(&cache);
    const QByteArray zip = bundleZip();
    serve(QStringLiteral("/chester/lte.zip"), HttpFixture::Response::ok(zip));
    // An older HEX entry is evicted by the newer designated file.
    const FirmwareId id = *FirmwareId::parse("0123456789abcdef0123456789abcdef");
    const std::string hex = hexRecord(0, 0, {1}) + eofRecord();
    ASSERT_TRUE(cache.store(id, std::as_bytes(std::span(hex.data(), hex.size()))));
    const DesignatedFirmware designated = firmware(zip);
    ASSERT_TRUE(waitForDownload(*catalog->downloadDesignated(designated)));
    EXPECT_FALSE(fs::exists(cache.fileFor(id)));
    EXPECT_TRUE(fs::exists(cache.designatedFileFor(designated.sha256)));
}

// ---- catalog page --------------------------------------------------------------------------

TEST(CatalogPage, SavedPage) {
    const QByteArray page = savedPage();
    ASSERT_FALSE(page.isEmpty());
    const auto info =
        parseCatalogPage(std::string_view(page.constData(), static_cast<std::size_t>(page.size())));
    ASSERT_TRUE(info) << info.error().detail;
    EXPECT_EQ(info->name, "hio-chester-push-fm");
    EXPECT_EQ(info->version, "v4.0.1");
    EXPECT_EQ(info->uploaded, "2026-09-07 12:13:53");
    // The HEX checksum, not the BIN one that follows it.
    EXPECT_EQ(info->sha256,
              hio::firmware::parseSha256(
                  "0c7a2286d945c1e12bf9b4f0620beff519a8ade36f98c1da33527d18fa568f5a"));
}

TEST(CatalogPage, TitleFallbackAndMissingParts) {
    const auto titleOnly = parseCatalogPage(
        "<html><head><title> CHESTER Firmware: hio-chester-clime:1.2.3 </title></head></html>");
    ASSERT_TRUE(titleOnly);
    EXPECT_EQ(titleOnly->name, "hio-chester-clime");
    EXPECT_EQ(titleOnly->version, "1.2.3");
    EXPECT_FALSE(titleOnly->sha256);
    EXPECT_TRUE(titleOnly->uploaded.empty());

    // A checksum without the HEX download before it is not taken.
    const auto noHex =
        parseCatalogPage("<h2>a:v1</h2><div>SHA256 checksum: "
                         "0c7a2286d945c1e12bf9b4f0620beff519a8ade36f98c1da33527d18fa568f5a</div>");
    ASSERT_TRUE(noHex);
    EXPECT_FALSE(noHex->sha256);

    for (const std::string_view html :
         {std::string_view(), std::string_view("<html></html>"),
          std::string_view("404 page not found"), std::string_view("<h2>:v1</h2>"),
          std::string_view("<h2>name:</h2>"), std::string_view("<h2>name:x1</h2>"),
          std::string_view("<h2>na me:v1</h2>")}) {
        const auto info = parseCatalogPage(html);
        ASSERT_FALSE(info) << html;
        EXPECT_EQ(info.error().code, ErrorCode::NetworkDownloadFailed);
    }
}

TEST(CatalogPage, StyleScriptAndCommentsAreIgnored) {
    const auto info = parseCatalogPage(
        "<style>a:v1 { }</style><script>x:v2</script><!-- y:v3 --><h2>&nbsp;real:v4&nbsp;</h2>");
    ASSERT_TRUE(info);
    EXPECT_EQ(info->name, "real");
    EXPECT_EQ(info->version, "v4");
}

// Mutated input (deterministic; the libFuzzer target catalog_page_fuzz runs the same body in the
// linux-fuzz preset): never crashes, fields stay bounded.
TEST(CatalogPage, MutatedInputStaysBounded) {
    const QByteArray page = savedPage();
    ASSERT_FALSE(page.isEmpty());
    std::mt19937 random(20261020); // NOLINT(cert-msc32-c,cert-msc51-cpp) - reproducible
    for (int round = 0; round < 300; ++round) {
        std::string text(page.constData(), static_cast<std::size_t>(page.size()));
        const int edits = 1 + (round % 40);
        for (int i = 0; i < edits; ++i) {
            const std::size_t at = random() % text.size();
            switch (random() % 4) {
            case 0:
                text[at] = static_cast<char>(random() % 256);
                break;
            case 1:
                text.erase(at, random() % 64);
                break;
            case 2:
                text.insert(
                    at, std::string(random() % 8, std::string_view("<>&:;\"-").at(random() % 7)));
                break;
            default:
                text.resize(at + 1);
                break;
            }
            if (text.empty()) {
                text = "<";
            }
        }
        const auto info = parseCatalogPage(text);
        if (info) {
            EXPECT_FALSE(info->name.empty());
            EXPECT_LE(info->name.size(), hio::firmware::maxCatalogFieldBytes);
            EXPECT_LE(info->version.size(), hio::firmware::maxCatalogFieldBytes);
            EXPECT_LE(info->uploaded.size(), hio::firmware::maxCatalogFieldBytes);
        }
    }
    // Pathological input: long runs without tags, an unterminated comment / style.
    EXPECT_FALSE(parseCatalogPage(std::string(1 << 20, 'a')));
    EXPECT_FALSE(parseCatalogPage("<!--" + std::string(4096, '-')));
    EXPECT_FALSE(parseCatalogPage("<style>" + std::string(4096, '<')));
    EXPECT_FALSE(parseCatalogPage(std::string(100000, '<')));
}

// ---- lookup and the verified ID download -----------------------------------------------------

class Lookup : public Designated {};

TEST_F(Lookup, ReadsThePage) {
    auto catalog = this->catalog(nullptr);
    const FirmwareId id = *FirmwareId::parse("001543e218a04ffdbf65082532202736");
    EXPECT_EQ(catalog->pageUrlFor(id).path(),
              QStringLiteral("/chester/001543e218a04ffdbf65082532202736"));
    serve(QStringLiteral("/chester/001543e218a04ffdbf65082532202736"),
          HttpFixture::Response::ok(savedPage()));
    auto operation = catalog->lookup(id);
    const auto info = waitForLookup(*operation);
    ASSERT_TRUE(info) << info.error().detail;
    EXPECT_EQ(info->name, "hio-chester-push-fm");
    EXPECT_EQ(info->version, "v4.0.1");
}

TEST_F(Lookup, NotFoundUnreadableAndCancelled) {
    auto catalog = this->catalog(nullptr);
    const FirmwareId id = *FirmwareId::parse("ffffffffffffffffffffffffffffffff");
    const auto missing = waitForLookup(*catalog->lookup(id));
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, ErrorCode::FirmwareNotFoundInCatalog);

    serve(QStringLiteral("/chester/ffffffffffffffffffffffffffffffff"),
          HttpFixture::Response::ok("<html>maintenance</html>"));
    const auto unreadable = waitForLookup(*catalog->lookup(id));
    ASSERT_FALSE(unreadable);
    EXPECT_EQ(unreadable.error().code, ErrorCode::NetworkDownloadFailed);

    serve(QStringLiteral("/chester/ffffffffffffffffffffffffffffffff"),
          HttpFixture::Response::hanging());
    auto hanging = catalog->lookup(id);
    hanging->cancel();
    const auto cancelled = waitForLookup(*hanging);
    ASSERT_FALSE(cancelled);
    EXPECT_EQ(cancelled.error().code, ErrorCode::Cancelled);
}

// Review F9: a server that keeps sending slowly does not hold the lookup beyond its total time
// limit (the inactivity timeout alone would never fire).
TEST_F(Lookup, TotalTimeLimit) {
    auto catalog = std::make_unique<FirmwareCatalog>(
        CatalogOptions{.baseUrl = QUrl(baseUrl()),
                       .transferTimeout = std::chrono::milliseconds(60000),
                       .maxBodyBytes = 64ULL * 1024,
                       .lookupTimeout = std::chrono::milliseconds(400),
                       .maxPageBytes = 64ULL * 1024},
        nullptr);
    const FirmwareId id = *FirmwareId::parse("ffffffffffffffffffffffffffffffff");
    serve(QStringLiteral("/chester/ffffffffffffffffffffffffffffffff"),
          HttpFixture::Response::trickling(QByteArray(1000, 'x'), 50));
    const auto start = std::chrono::steady_clock::now();
    const auto result = waitForLookup(*catalog->lookup(id));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::NetworkDownloadFailed);
    EXPECT_NE(result.error().detail.find("time limit"), std::string::npos) << result.error().detail;
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
}

TEST_F(Lookup, IdDownloadIsVerifiedByTheLookupHash) {
    FirmwareCache cache(cacheDir());
    auto catalog = this->catalog(&cache);
    const FirmwareId id = *FirmwareId::parse("0123456789abcdef0123456789abcdef");
    const QByteArray hex = QByteArray::fromStdString(hexRecord(0, 0, {1, 2, 3}) + eofRecord());
    const QString path = QStringLiteral("/chester/0123456789abcdef0123456789abcdef/hex");
    serve(path, HttpFixture::Response::ok(hex));

    // The wrong hash: firmware.checksum_mismatch, not cached.
    auto wrong =
        catalog->download(id, DownloadOptions{.noCache = false, .expectedSha256 = shaOf("other")});
    const auto mismatch = waitForDownload(*wrong);
    ASSERT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error().code, ErrorCode::FirmwareChecksumMismatch);
    EXPECT_FALSE(fs::exists(cache.fileFor(id)));

    // The right hash: downloaded and cached.
    auto right =
        catalog->download(id, DownloadOptions{.noCache = false, .expectedSha256 = shaOf(hex)});
    ASSERT_TRUE(waitForDownload(*right));
    EXPECT_TRUE(fs::exists(cache.fileFor(id)));
    EXPECT_EQ(requests(path), 2);

    // A cache hit with the right hash needs no request; one with another hash is downloaded again
    // (and then fails the check, as the server still has the first file).
    auto hit =
        catalog->download(id, DownloadOptions{.noCache = false, .expectedSha256 = shaOf(hex)});
    const auto cached = waitForDownload(*hit);
    ASSERT_TRUE(cached);
    EXPECT_TRUE(cached->fromCache);
    EXPECT_EQ(requests(path), 2);
    auto changed =
        catalog->download(id, DownloadOptions{.noCache = false, .expectedSha256 = shaOf("new")});
    const auto refetched = waitForDownload(*changed);
    ASSERT_FALSE(refetched);
    EXPECT_EQ(refetched.error().code, ErrorCode::FirmwareChecksumMismatch);
    EXPECT_EQ(requests(path), 3);
    EXPECT_FALSE(fs::exists(cache.fileFor(id)));
}

} // namespace
