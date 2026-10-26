// Unit tests of the M6 target commands' parsing and formatting (plan/features/0013 B4).

#include "hio/core/error.h"
#include "hio/device/operation.h"
#include "hio/hw/jlink/programmer.h"
#include "hio/hw/jlink/types.h"
#include "target_commands.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::device::AccessWidth;
using hio::device::MemoryZone;
using hio::device::RegisterValue;

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> result;
    for (const int value : values) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

TEST(TargetCommands, ParseAddress) {
    EXPECT_EQ(hio::cli::parseAddress("0x20000000"), 0x20000000U);
    EXPECT_EQ(hio::cli::parseAddress("0XfFfFfFfF"), 0xFFFFFFFFU);
    EXPECT_EQ(hio::cli::parseAddress("536870912"), 0x20000000U);
    EXPECT_EQ(hio::cli::parseAddress("4294967295"), 0xFFFFFFFFU);
    EXPECT_EQ(hio::cli::parseAddress("0"), 0U);
    for (const char* bad :
         {"", "0x", "0x123456789", "4294967296", "12345678901", "-1", "0x1g", "1k", " 1", "+1"}) {
        const auto parsed = hio::cli::parseAddress(bad);
        ASSERT_FALSE(parsed.has_value()) << bad;
        EXPECT_EQ(parsed.error().code, ErrorCode::UsageInvalidArgument);
        EXPECT_EQ(parsed.error().detail, bad);
    }
}

TEST(TargetCommands, ParseHexBytes) {
    EXPECT_EQ(hio::cli::parseHexBytes("0102aaBB"), bytes({0x01, 0x02, 0xAA, 0xBB}));
    for (const char* bad : {"", "1", "0x01", "zz", "01 02"}) {
        const auto parsed = hio::cli::parseHexBytes(bad);
        ASSERT_FALSE(parsed.has_value()) << bad;
        EXPECT_EQ(parsed.error().code, ErrorCode::UsageInvalidArgument);
    }
}

TEST(TargetCommands, HexDumpBytes) {
    std::vector<std::byte> data;
    data.reserve(20);
    for (int i = 0; i < 20; ++i) {
        data.push_back(static_cast<std::byte>(0x41 + i));
    }
    data[1] = std::byte{0x00};
    EXPECT_EQ(hio::cli::hexDump(0x20000000, data, AccessWidth::Bits8),
              "0x20000000  41 00 43 44 45 46 47 48  49 4a 4b 4c 4d 4e 4f 50  |A.CDEFGHIJKLMNOP|\n"
              "0x20000010  51 52 53 54                                       |QRST|");
    EXPECT_EQ(hio::cli::hexDump(0, {}, AccessWidth::Bits8), "");
}

TEST(TargetCommands, HexDumpUnitsAreLittleEndian) {
    const auto data = bytes({0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                             0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13});
    EXPECT_EQ(hio::cli::hexDump(0x1000, data, AccessWidth::Bits32),
              "0x00001000  03020100 07060504  0b0a0908 0f0e0d0c  |................|\n"
              "0x00001010  13121110                              |....|");
    EXPECT_EQ(hio::cli::hexDump(0x1000, std::span(data).first(4), AccessWidth::Bits16),
              "0x00001000  0100 0302" + std::string(33, ' ') + "|....|");
}

TEST(TargetCommands, Registers) {
    const std::vector<RegisterValue> registers{{.name = "R0", .value = 1},
                                               {.name = "XPSR", .value = 0x01000000}};
    EXPECT_EQ(hio::cli::registersText(registers), "R0    0x00000001\nXPSR  0x01000000");
    const QJsonObject json = hio::cli::registersJson(registers);
    const QJsonObject values = json.value(QStringLiteral("registers")).toObject();
    EXPECT_EQ(values.size(), 2);
    EXPECT_EQ(values.value(QStringLiteral("R0")).toString(), QStringLiteral("0x00000001"));
    EXPECT_EQ(values.value(QStringLiteral("XPSR")).toString(), QStringLiteral("0x01000000"));
    EXPECT_EQ(hio::cli::registersText({}), "");
}

TEST(TargetCommands, Zones) {
    EXPECT_EQ(hio::cli::zonesText({}), "No memory zones.");
    EXPECT_TRUE(hio::cli::zonesJson({}).value(QStringLiteral("zones")).toArray().isEmpty());
    const std::vector<MemoryZone> zones{
        {.name = "AHB-AP", .description = "Memory via AHB-AP", .virtualAddress = 0},
        {.name = "APB", .description = "APB", .virtualAddress = 0x100000000ULL}};
    EXPECT_EQ(hio::cli::zonesText(zones),
              "AHB-AP  0x00000000  Memory via AHB-AP\nAPB     0x100000000  APB");
    const QJsonArray list = hio::cli::zonesJson(zones).value(QStringLiteral("zones")).toArray();
    ASSERT_EQ(list.size(), 2);
    const QJsonObject first = list.at(0).toObject();
    EXPECT_EQ(first.value(QStringLiteral("name")).toString(), QStringLiteral("AHB-AP"));
    EXPECT_EQ(first.value(QStringLiteral("description")).toString(),
              QStringLiteral("Memory via AHB-AP"));
    EXPECT_EQ(first.value(QStringLiteral("virtualAddress")).toString(),
              QStringLiteral("0x00000000"));
}

} // namespace
