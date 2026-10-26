// Intel HEX parser and FirmwareImage (plan/features/0003, phase A1).

#include "hex_builder.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/intel_hex.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QStringLiteral>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>
#include <QtTypes>

#include <gtest/gtest.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::firmware::AddressRange;
using hio::firmware::FirmwareImage;
using hio::firmware::parseIntelHex;
using hio::firmware::Segment;
using hio::firmware::test::bytesOf;
using hio::firmware::test::eofRecord;
using hio::firmware::test::filled;
using hio::firmware::test::hexRecord;
using hio::firmware::test::linearAddress;
using hio::firmware::test::toHex;

hio::Result<FirmwareImage> parse(const std::string& text) {
    return parseIntelHex(bytesOf(text), "test.hex");
}

std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> result;
    for (const std::uint8_t v : values) {
        result.push_back(static_cast<std::byte>(v));
    }
    return result;
}

TEST(IntelHex, DataAndEof) {
    const auto image =
        parse(hexRecord(0, 0x0100, {1, 2, 3}) + hexRecord(0, 0x0103, {4}) + eofRecord());
    ASSERT_TRUE(image) << image.error().detail;
    ASSERT_EQ(image->segments().size(), 1U);
    EXPECT_EQ(image->segments()[0].address, 0x100U);
    EXPECT_EQ(image->segments()[0].data, bytes({1, 2, 3, 4}));
    EXPECT_EQ(image->totalBytes(), 4U);
    EXPECT_FALSE(image->startAddress());
}

TEST(IntelHex, CrLfAndLowercaseAndEmptyLines) {
    std::string text = hexRecord(0, 0x10, {0xAB, 0xCD});
    text.insert(text.size() - 1, "\r");
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const auto image = parse("\n" + text + "\n" + eofRecord() + "\r\n\n");
    ASSERT_TRUE(image) << image.error().detail;
    EXPECT_EQ(image->segments()[0].data, bytes({0xAB, 0xCD}));
}

TEST(IntelHex, ExtendedLinearAddress) {
    const auto image =
        parse(linearAddress(0x1000) + hexRecord(0, 0x1080, {0xBA, 0xBE}) + eofRecord());
    ASSERT_TRUE(image);
    EXPECT_EQ(image->segments()[0].address, 0x10001080U);
}

TEST(IntelHex, ExtendedSegmentAddressWrapsInsideTheSegment) {
    // Segment 0x1000 -> base 0x10000; offset 0xFFFF + 2 bytes wraps to 0x10000.
    const auto image =
        parse(hexRecord(2, 0, {0x10, 0x00}) + hexRecord(0, 0xFFFF, {1, 2}) + eofRecord());
    ASSERT_TRUE(image) << image.error().detail;
    ASSERT_EQ(image->segments().size(), 2U);
    EXPECT_EQ(image->segments()[0].address, 0x10000U);
    EXPECT_EQ(image->segments()[0].data, bytes({2}));
    EXPECT_EQ(image->segments()[1].address, 0x1FFFFU);
}

TEST(IntelHex, StartAddresses) {
    const auto linear = parse(hexRecord(5, 0, {0x00, 0x00, 0x12, 0x34}) + eofRecord());
    ASSERT_TRUE(linear);
    EXPECT_EQ(linear->startAddress(), 0x1234U);
    const auto segment = parse(hexRecord(3, 0, {0x00, 0x10, 0x00, 0x05}) + eofRecord());
    ASSERT_TRUE(segment);
    EXPECT_EQ(segment->startAddress(), 0x105U);
}

TEST(IntelHex, ChecksumErrorNamesTheLine) {
    const auto image = parse(hexRecord(0, 0, {1}) + hexRecord(0, 1, {2}, 1) + eofRecord());
    ASSERT_FALSE(image);
    EXPECT_EQ(image.error().code, ErrorCode::FirmwareHexParseError);
    EXPECT_NE(image.error().message.find("(line 2)"), std::string::npos) << image.error().message;
    EXPECT_NE(image.error().detail.find("checksum"), std::string::npos);
}

TEST(IntelHex, EofIsRequired) {
    const auto image = parse(hexRecord(0, 0, {1}));
    ASSERT_FALSE(image);
    EXPECT_NE(image.error().detail.find("end-of-file record is missing"), std::string::npos);
}

TEST(IntelHex, DataAfterEofIsRejected) {
    const auto image = parse(eofRecord() + hexRecord(0, 0, {1}));
    ASSERT_FALSE(image);
    EXPECT_NE(image.error().message.find("(line 2)"), std::string::npos);
}

TEST(IntelHex, MalformedRecords) {
    for (const std::string& bad :
         {std::string("garbage\n"), std::string(":0\n"), std::string(":00000001F\n"),
          std::string(":0100000001ZZ\n"), hexRecord(0x06, 0, {}), hexRecord(1, 0, {1}),
          hexRecord(4, 0, {1}), hexRecord(2, 0, {1, 2, 3}), hexRecord(5, 0, {1}),
          hexRecord(3, 0, {})}) {
        const auto image = parse(bad + eofRecord());
        EXPECT_FALSE(image) << bad;
        if (!image) {
            EXPECT_EQ(image.error().code, ErrorCode::FirmwareHexParseError) << bad;
        }
    }
    // Byte count does not match.
    std::string wrongCount = hexRecord(0, 0, {1, 2});
    wrongCount[2] = '3';
    EXPECT_FALSE(parse(wrongCount + eofRecord()));
}

TEST(IntelHex, IdenticalOverlapIsAcceptedDifferentIsRejected) {
    const auto same = parse(hexRecord(0, 0, {1, 2, 3}) + hexRecord(0, 1, {2, 3, 4}) + eofRecord());
    ASSERT_TRUE(same) << same.error().detail;
    EXPECT_EQ(same->segments()[0].data, bytes({1, 2, 3, 4}));
    const auto different = parse(hexRecord(0, 0, {1, 2, 3}) + hexRecord(0, 2, {9}) + eofRecord());
    ASSERT_FALSE(different);
    EXPECT_EQ(different.error().code, ErrorCode::FirmwareHexParseError);
    EXPECT_NE(different.error().detail.find("0x00000002"), std::string::npos)
        << different.error().detail;
}

TEST(IntelHex, OutOfOrderRecordsAreSortedAndMerged) {
    const auto image = parse(hexRecord(0, 0x20, {3}) + hexRecord(0, 0x00, {1}) +
                             hexRecord(0, 0x01, {2}) + eofRecord());
    ASSERT_TRUE(image);
    ASSERT_EQ(image->segments().size(), 2U);
    EXPECT_EQ(image->segments()[0].data, bytes({1, 2}));
    EXPECT_EQ(image->segments()[1].address, 0x20U);
}

TEST(IntelHex, DataLimit) {
    // 16 MiB + some bytes of data in 255-byte records (linear addressing does not wrap).
    std::string text;
    const std::vector<std::uint8_t> chunk(255, 0xAA);
    std::uint32_t lastUpper = 0xFFFFFFFFU;
    for (std::uint64_t written = 0; written <= hio::firmware::maxImageDataBytes; written += 255) {
        const auto address = static_cast<std::uint32_t>(written);
        if ((address >> 16U) != lastUpper) {
            lastUpper = address >> 16U;
            text += linearAddress(static_cast<std::uint16_t>(lastUpper));
        }
        text += hexRecord(0, static_cast<std::uint16_t>(address & 0xFFFFU), chunk);
    }
    const auto image = parse(text + eofRecord());
    ASSERT_FALSE(image);
    EXPECT_NE(image.error().detail.find("bytes of data"), std::string::npos)
        << image.error().detail;
}

TEST(IntelHex, LineLimit) {
    std::string text;
    for (std::uint64_t i = 0; i <= hio::firmware::maxHexLines; ++i) {
        text += "\n";
    }
    const auto image = parse(text + eofRecord());
    ASSERT_FALSE(image);
    EXPECT_NE(image.error().detail.find("lines"), std::string::npos);
}

TEST(IntelHex, RoundTripThroughTheBuilder) {
    std::vector<Segment> segments;
    segments.push_back(Segment{.address = 0xFFF0, .data = filled(64, 0x11)});
    segments.push_back(Segment{.address = 0x10001000, .data = filled(8, 0x22)});
    const auto original = FirmwareImage::fromSegments(std::move(segments));
    ASSERT_TRUE(original);
    const auto parsed = parse(toHex(*original));
    ASSERT_TRUE(parsed) << parsed.error().detail;
    EXPECT_EQ(parsed->segments(), original->segments());
    EXPECT_EQ(parsed->sha256(), original->sha256());
    EXPECT_EQ(parsed->sha256().size(), 64U);
}

// Review F2: a requested stop gives `cancelled` (parse and file read).
TEST(IntelHex, StopTokenCancelsTheParse) {
    std::stop_source source;
    source.request_stop();
    const std::string text = hexRecord(0, 0x0100, {1, 2, 3}) + eofRecord();
    const auto stopped = parseIntelHex(bytesOf(text), "test.hex", source.get_token());
    ASSERT_FALSE(stopped);
    EXPECT_EQ(stopped.error().code, ErrorCode::Cancelled);
    EXPECT_TRUE(parseIntelHex(bytesOf(text), "test.hex", std::stop_source().get_token()));

    const QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("app.hex")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(text.data(), static_cast<qint64>(text.size()));
    file.close();
    const std::filesystem::path path(file.fileName().toStdU16String());
    const auto read = hio::firmware::readIntelHexFile(path, source.get_token());
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().code, ErrorCode::Cancelled);
    EXPECT_TRUE(hio::firmware::readIntelHexFile(path));
}

TEST(IntelHex, ReadFileErrors) {
    const auto missing = hio::firmware::readIntelHexFile("/nonexistent/dir/file.hex");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, ErrorCode::IoFileNotFound);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.hex"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("not hex\n");
    file.close();
    const auto bad = hio::firmware::readIntelHexFile(std::filesystem::path(path.toStdU16String()));
    ASSERT_FALSE(bad);
    EXPECT_EQ(bad.error().code, ErrorCode::FirmwareHexParseError);
    EXPECT_NE(bad.error().detail.find("a.hex"), std::string::npos);
}

// Real build outputs (opt-in: HIO_TEST_HEX_CORPUS=<directory with .hex files>; CHESTER builds
// are not committed). Every file must parse, lie in the nRF52840 / nRF91 flash or UICR and
// round-trip through the builder with the same SHA-256.
TEST(IntelHex, Corpus) {
    const QString directory = qEnvironmentVariable("HIO_TEST_HEX_CORPUS");
    if (directory.isEmpty()) {
        GTEST_SKIP() << "HIO_TEST_HEX_CORPUS not set";
    }
    int parsed = 0;
    QDirIterator it(directory, {QStringLiteral("*.hex")}, QDir::Files);
    while (it.hasNext()) {
        const QString path = it.next();
        const auto image =
            hio::firmware::readIntelHexFile(std::filesystem::path(path.toStdU16String()));
        ASSERT_TRUE(image) << path.toStdString() << ": " << image.error().detail;
        for (const Segment& segment : image->segments()) {
            const AddressRange range = segment.range();
            const bool inside =
                AddressRange{.start = 0, .end = 0x100000}.contains(range) ||
                AddressRange{.start = 0x10001000, .end = 0x10002000}.contains(range) ||
                AddressRange{.start = 0x00FF8000, .end = 0x00FF9000}.contains(range);
            EXPECT_TRUE(inside) << path.toStdString() << " segment at " << segment.address;
        }
        const auto again = parse(toHex(*image));
        ASSERT_TRUE(again);
        EXPECT_EQ(again->sha256(), image->sha256());
        ++parsed;
    }
    EXPECT_GT(parsed, 0);
}

TEST(FirmwareImage, SplitOut) {
    std::vector<Segment> segments;
    segments.push_back(Segment{.address = 0x0, .data = filled(16, 1)});
    segments.push_back(Segment{.address = 0x10001000, .data = filled(0x100, 2)});
    auto image = FirmwareImage::fromSegments(std::move(segments));
    ASSERT_TRUE(image);
    EXPECT_TRUE(image->intersects(AddressRange{.start = 0x10001080, .end = 0x10001100}));
    auto uicr = image->splitOut({.start = 0x10001080, .end = 0x10001090});
    ASSERT_TRUE(uicr);
    ASSERT_EQ(uicr->segments().size(), 1U);
    EXPECT_EQ(uicr->segments()[0].address, 0x10001080U);
    EXPECT_EQ(uicr->totalBytes(), 16U);
    ASSERT_EQ(image->segments().size(), 3U);
    EXPECT_EQ(image->segments()[1].range().end, 0x10001080U);
    EXPECT_EQ(image->segments()[2].address, 0x10001090U);
    EXPECT_FALSE(image->intersects(AddressRange{.start = 0x10001080, .end = 0x10001090}));
    EXPECT_FALSE(image->splitOut(AddressRange{.start = 5, .end = 5}));
}

TEST(FirmwareImage, SegmentBeyondTheAddressSpaceIsRejected) {
    std::vector<Segment> segments;
    segments.push_back(Segment{.address = 0xFFFFFFF0U, .data = filled(32, 0)});
    EXPECT_FALSE(FirmwareImage::fromSegments(std::move(segments)));
}

} // namespace
