// PIB decoder test vectors (plan/features/0003, C3). The valid vector was produced by py-hardwario
// 1.7.2 (hardwario.chester.pib.PIB().get_buffer() with the values below).

#include "hio/core/error.h"
#include "hio/device/pib.h"

#include <QByteArray>
#include <QtTypes>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace {

using hio::ErrorCode;
using hio::device::decodePib;

constexpr const char* validHex =
    "babecafe027b48415244574152494f0000000000000000434845535445522d4d00000000000000004344474c53"
    "00000000000052332e3200000032313539303138323437003031323334353637383961626364656630313233343536"
    "373839616263646566003132333435360000000000000000000000a8d83379ffffffffff";

std::array<std::byte, 128> bytes(const char* hex = validHex) {
    const QByteArray raw = QByteArray::fromHex(hex);
    std::array<std::byte, 128> result{};
    result.fill(std::byte{0xFF});
    for (qsizetype i = 0; i < raw.size() && i < 128; ++i) {
        result.at(static_cast<std::size_t>(i)) = static_cast<std::byte>(raw.at(i));
    }
    return result;
}

void restoreCrc(std::array<std::byte, 128>& pib) {
    const std::uint32_t crc = hio::device::pibCrc32(std::span(pib).first(0x77));
    pib[0x77] = static_cast<std::byte>(crc >> 24U);
    pib[0x78] = static_cast<std::byte>(crc >> 16U);
    pib[0x79] = static_cast<std::byte>(crc >> 8U);
    pib[0x7A] = static_cast<std::byte>(crc);
}

TEST(Pib, DecodesThePyHardwarioVector) {
    const auto pib = decodePib(bytes());
    ASSERT_TRUE(pib) << pib.error().message << " " << pib.error().detail;
    EXPECT_EQ(pib->signature, 0xBABECAFEU);
    EXPECT_EQ(pib->version, 2);
    EXPECT_EQ(pib->size, 0x7B);
    EXPECT_EQ(pib->crc, 0xA8D83379U);
    EXPECT_EQ(pib->vendorName, "HARDWARIO");
    EXPECT_EQ(pib->productName, "CHESTER-M");
    EXPECT_EQ(pib->hwVariant, "CDGLS");
    EXPECT_EQ(pib->hwRevision, "R3.2");
    EXPECT_EQ(pib->serialNumber, "2159018247");
    EXPECT_EQ(pib->claimToken, "0123456789abcdef0123456789abcdef");
    EXPECT_EQ(pib->blePasskey, "123456");
}

TEST(Pib, ErasedIsInvalidVersion) {
    std::array<std::byte, 128> erased{};
    erased.fill(std::byte{0xFF});
    const auto pib = decodePib(erased);
    ASSERT_FALSE(pib);
    EXPECT_EQ(pib.error().code, ErrorCode::PibInvalid);
    EXPECT_EQ(pib.error().message,
              "The Product Information Block is missing or damaged (version).");
}

TEST(Pib, BadSignature) {
    auto pib = bytes();
    pib[0] = std::byte{0xBB};
    const auto result = decodePib(pib);
    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("(signature)"), std::string::npos);
}

TEST(Pib, BadSize) {
    auto pib = bytes();
    pib[5] = std::byte{0x7C};
    const auto result = decodePib(pib);
    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("(size)"), std::string::npos);
}

TEST(Pib, BadCrc) {
    auto pib = bytes();
    pib[0x10] = std::byte{'X'};
    const auto result = decodePib(pib);
    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("(crc)"), std::string::npos);
    EXPECT_NE(result.error().detail.find("a8d83379"), std::string::npos);
    restoreCrc(pib); // with a matching CRC the changed field decodes
    EXPECT_TRUE(decodePib(pib));
}

TEST(Pib, VersionOneCannotPassTheSizeCheck) {
    auto pib = bytes();
    // Version 1 layout: little-endian signature, 16-bit size at 0x08 (py-hardwario).
    pib[0] = std::byte{0xFE};
    pib[1] = std::byte{0xCA};
    pib[2] = std::byte{0xBE};
    pib[3] = std::byte{0xBA};
    pib[4] = std::byte{1};
    pib[8] = std::byte{0x5C};
    pib[9] = std::byte{0};
    const auto result = decodePib(pib);
    ASSERT_FALSE(result);
    EXPECT_NE(result.error().message.find("(size)"), std::string::npos);
}

TEST(Pib, Crc32IsTheStandardOne) {
    const std::string text = "123456789";
    EXPECT_EQ(hio::device::pibCrc32(std::as_bytes(std::span(text))), 0xCBF43926U);
}

} // namespace
