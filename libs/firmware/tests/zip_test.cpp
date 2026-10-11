// ZipReader limits and zip slip (phase A2), LTE input classification (phase A3).

#include "hex_builder.h"
#include "hio/core/error.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/lte_input.h"
#include "hio/firmware/zip_reader.h"
#include "zip_builder.h"

#include <QByteArray>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::firmware::AppHex;
using hio::firmware::classifyLteInput;
using hio::firmware::FirmwareImage;
using hio::firmware::isUnsafeEntryName;
using hio::firmware::LteBundle;
using hio::firmware::ModemOnly;
using hio::firmware::Segment;
using hio::firmware::ZipLimits;
using hio::firmware::ZipReader;
using hio::firmware::test::eofRecord;
using hio::firmware::test::hexRecord;
using hio::firmware::test::patched;
using hio::firmware::test::writeFile;
using hio::firmware::test::zipBytes;
using ZipItems = std::vector<hio::firmware::test::ZipItem>;

class Zip : public ::testing::Test {
protected:
    [[nodiscard]] fs::path write(const QString& name, const QByteArray& bytes) const {
        const QString path = m_dir.filePath(name);
        EXPECT_TRUE(writeFile(path, bytes));
        return {path.toStdU16String()};
    }
    [[nodiscard]] fs::path dir() const { return {m_dir.path().toStdU16String()}; }

private:
    QTemporaryDir m_dir;
};

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string appHex() { return hexRecord(0, 0x1000, {1, 2, 3, 4}) + eofRecord(); }

TEST_F(Zip, ListsAndExtracts) {
    const fs::path path =
        write(QStringLiteral("a.zip"),
              zipBytes({{.name = "one.txt", .data = "hello"},
                        {.name = "dir/two.bin", .data = std::string(1000, 'x')}}));
    auto zip = ZipReader::open(path);
    ASSERT_TRUE(zip) << zip.error().detail;
    ASSERT_EQ(zip->entries().size(), 2U);
    EXPECT_EQ(zip->entries()[0].name, "one.txt");
    EXPECT_EQ(zip->entries()[1].size, 1000U);
    const auto bytes = zip->extract("one.txt", 100);
    ASSERT_TRUE(bytes);
    EXPECT_EQ(bytes->size(), 5U);
    const auto file = zip->extractTo("dir/two.bin", dir() / "out", 2000);
    ASSERT_TRUE(file) << file.error().detail;
    EXPECT_EQ(readAll(*file), std::string(1000, 'x'));
    EXPECT_EQ(file->parent_path().filename(), "dir");
    EXPECT_FALSE(zip->extract("missing", 10));
}

TEST_F(Zip, ExtractStopsAtTheLimit) {
    const fs::path path = write(QStringLiteral("a.zip"),
                                zipBytes({{.name = "big", .data = std::string(5000, 'y')}}, 0));
    auto zip = ZipReader::open(path);
    ASSERT_TRUE(zip);
    const auto bytes = zip->extract("big", 4999);
    ASSERT_FALSE(bytes);
    EXPECT_EQ(bytes.error().code, ErrorCode::FirmwareInvalidImage);
}

TEST_F(Zip, TooManyEntries) {
    ZipItems items;
    for (int i = 0; i < 65; ++i) {
        items.push_back({.name = "f" + std::to_string(i), .data = "x"});
    }
    const auto zip = ZipReader::open(write(QStringLiteral("many.zip"), zipBytes(items)));
    ASSERT_FALSE(zip);
    EXPECT_EQ(zip.error().code, ErrorCode::FirmwareInvalidImage);
    EXPECT_NE(zip.error().detail.find("65 entries"), std::string::npos);
}

TEST_F(Zip, EntryTooLarge) {
    const auto zip = ZipReader::open(
        write(QStringLiteral("a.zip"),
              zipBytes({{.name = "a", .data = std::string(2048, 'z')}}, 0)),
        ZipLimits{.maxEntries = 64, .maxEntryBytes = 1024, .maxCompressionRatio = 200});
    ASSERT_FALSE(zip);
    EXPECT_NE(zip.error().detail.find("limit"), std::string::npos);
}

TEST_F(Zip, CompressionRatio) {
    // 4 MiB of zeros deflate to a few KiB: a ratio far above 200 (zip bomb rule).
    const auto zip = ZipReader::open(
        write(QStringLiteral("bomb.zip"),
              zipBytes({{.name = "zeros", .data = std::string(4 << 20, '\0')}}, 9)));
    ASSERT_FALSE(zip);
    EXPECT_NE(zip.error().detail.find("compression ratio"), std::string::npos);
}

TEST_F(Zip, ZipSlipNamesAreRejected) {
    EXPECT_TRUE(isUnsafeEntryName("../evil"));
    EXPECT_TRUE(isUnsafeEntryName("a/../../evil"));
    EXPECT_TRUE(isUnsafeEntryName("a\\..\\evil"));
    EXPECT_TRUE(isUnsafeEntryName("/etc/passwd"));
    EXPECT_TRUE(isUnsafeEntryName("\\windows"));
    EXPECT_TRUE(isUnsafeEntryName("C:evil"));
    EXPECT_TRUE(isUnsafeEntryName(""));
    EXPECT_FALSE(isUnsafeEntryName("a/b..c/d"));
    EXPECT_FALSE(isUnsafeEntryName("modem.zip"));

    const auto dotDot = ZipReader::open(
        write(QStringLiteral("a.zip"), zipBytes({{.name = "x/../../evil", .data = "1"}})));
    ASSERT_FALSE(dotDot);
    EXPECT_NE(dotDot.error().detail.find("unsafe entry name"), std::string::npos);
    // miniz refuses to write these names, so they are patched into an archive.
    const QByteArray absolute =
        patched(zipBytes({{.name = "xetc/passwd", .data = "1"}}), "xetc/passwd", "/etc/passwd");
    EXPECT_FALSE(ZipReader::open(write(QStringLiteral("b.zip"), absolute)));
    const QByteArray drive =
        patched(zipBytes({{.name = "Cxevil", .data = "1"}}), "Cxevil", "C:evil");
    EXPECT_FALSE(ZipReader::open(write(QStringLiteral("c.zip"), drive)));
}

TEST_F(Zip, NotAZip) {
    const auto zip = ZipReader::open(write(QStringLiteral("a.zip"), "PK\x03\x04garbage"));
    ASSERT_FALSE(zip);
    EXPECT_EQ(zip.error().code, ErrorCode::FirmwareInvalidImage);
    const auto missing = ZipReader::open(dir() / "missing.zip");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, ErrorCode::IoFileNotFound);
}

TEST_F(Zip, NestedZipIsReadAsBytes) {
    const QByteArray inner = zipBytes({{.name = "inner.txt", .data = "inside"}});
    const fs::path path = write(QStringLiteral("outer.zip"),
                                zipBytes({{.name = "inner.zip", .data = inner.toStdString()}}));
    auto zip = ZipReader::open(path);
    ASSERT_TRUE(zip);
    const auto bytes = zip->extract("inner.zip", 1 << 20);
    ASSERT_TRUE(bytes);
    EXPECT_EQ(bytes->size(), static_cast<std::size_t>(inner.size()));
}

// ---- classifyLteInput (ADR-0040) -------------------------------------------------------------

QByteArray modemZip() {
    return zipBytes({{.name = "firmware.update.image.segments.0.hex", .data = appHex()},
                     {.name = "firmware.update.image.segments.1.hex", .data = appHex()},
                     {.name = "firmware.update.image.digest.txt", .data = "digest"}});
}

TEST_F(Zip, LteBundle) {
    const fs::path path = write(QStringLiteral("bundle.zip"),
                                zipBytes({{.name = "modem.zip", .data = modemZip().toStdString()},
                                          {.name = "application.hex", .data = appHex()}}));
    auto input = classifyLteInput(path);
    ASSERT_TRUE(input) << input.error().detail;
    auto* bundle = std::get_if<LteBundle>(&*input);
    ASSERT_NE(bundle, nullptr);
    EXPECT_EQ(bundle->application.totalBytes(), 4U);
    ASSERT_TRUE(bundle->extractDir);
    EXPECT_TRUE(fs::exists(bundle->modemZip));
    EXPECT_EQ(readAll(bundle->modemZip), modemZip().toStdString());
    const fs::path extracted = bundle->modemZip;
    input = classifyLteInput(path); // releases the first bundle and its directory
    EXPECT_FALSE(fs::exists(extracted));
}

TEST_F(Zip, LteModemZip) {
    const fs::path path = write(QStringLiteral("mfw_nrf91x1_2.0.2.zip"), modemZip());
    const auto input = classifyLteInput(path);
    ASSERT_TRUE(input) << input.error().detail;
    const auto* modem = std::get_if<ModemOnly>(&*input);
    ASSERT_NE(modem, nullptr);
    EXPECT_EQ(modem->path, path);
}

TEST_F(Zip, LteHex) {
    const fs::path path = write(QStringLiteral("app.bin"), QByteArray::fromStdString(appHex()));
    const auto input = classifyLteInput(path);
    ASSERT_TRUE(input);
    EXPECT_TRUE(std::holds_alternative<AppHex>(*input));
}

TEST_F(Zip, LteRejectsOtherZipsAndFiles) {
    const std::vector<QByteArray> rejected{
        zipBytes({{.name = "modem.zip", .data = "x"}}), // 1 entry
        zipBytes({{.name = "modem.zip", .data = "x"},
                  {.name = "application.hex", .data = appHex()},
                  {.name = "readme.txt", .data = "x"}}), // 3 entries
        zipBytes(
            {{.name = "Modem.zip", .data = "x"}, {.name = "application.hex", .data = appHex()}}),
        zipBytes({{.name = "sub/firmware.update.image.segments.0.hex", .data = "x"}}),
        QByteArray("\x7f"
                   "ELF binary"),
    };
    int i = 0;
    for (const QByteArray& bytes : rejected) {
        const auto input = classifyLteInput(write(QStringLiteral("f%1.zip").arg(i++), bytes));
        ASSERT_FALSE(input) << i;
        EXPECT_EQ(input.error().code, ErrorCode::FirmwareInvalidImage) << i;
        EXPECT_EQ(input.error().message,
                  "The file is not a CHESTER LTE bundle, modem firmware or HEX file.");
    }
}

TEST_F(Zip, LteBundleWithBadHex) {
    const fs::path path = write(QStringLiteral("bundle.zip"),
                                zipBytes({{.name = "modem.zip", .data = "x"},
                                          {.name = "application.hex", .data = ":00000001FE\n"}}));
    const auto input = classifyLteInput(path);
    ASSERT_FALSE(input);
    EXPECT_EQ(input.error().code, ErrorCode::FirmwareHexParseError);
}

TEST(ModemMarker, Names) {
    EXPECT_TRUE(hio::firmware::isModemFirmwareMarker("firmware.update.image.segments.0.hex"));
    EXPECT_TRUE(hio::firmware::isModemFirmwareMarker("firmware.update.image.segments.12.hex"));
    EXPECT_FALSE(hio::firmware::isModemFirmwareMarker("firmware.update.image.segments..hex"));
    EXPECT_FALSE(hio::firmware::isModemFirmwareMarker("firmware.update.image.segments.a.hex"));
    EXPECT_FALSE(hio::firmware::isModemFirmwareMarker("firmware.update.image.digest.txt"));
}

} // namespace
