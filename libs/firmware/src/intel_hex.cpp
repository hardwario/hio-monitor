#include "hio/firmware/intel_hex.h"

#include "file_io.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/firmware/firmware_image.h"

#include <QCoreApplication>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::firmware {

namespace {

// Record types.
constexpr std::uint8_t recordData = 0x00;
constexpr std::uint8_t recordEof = 0x01;
constexpr std::uint8_t recordSegmentAddress = 0x02;
constexpr std::uint8_t recordStartSegment = 0x03;
constexpr std::uint8_t recordLinearAddress = 0x04;
constexpr std::uint8_t recordStartLinear = 0x05;

constexpr std::size_t minRecordBytes = 5; // length, address (2), type, checksum

Error parseError(std::uint64_t line, std::string_view sourceName, std::string_view reason) {
    Error error = makeError(ErrorCode::FirmwareHexParseError,
                            std::format("{}: line {}: {}", sourceName, line, reason));
    error.message = QCoreApplication::translate(
                        "hio::firmware", "The firmware file is not a valid HEX file (line %1).")
                        .arg(line)
                        .toStdString();
    return error;
}

std::optional<std::uint8_t> hexDigit(std::byte value) {
    const auto c = static_cast<char>(value);
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<std::uint8_t>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<std::uint8_t>(c - 'A' + 10);
    }
    return std::nullopt;
}

constexpr std::size_t maxRecordBytes = 255 + minRecordBytes;

// The record bytes of one line (without ':') into `out`; the count, or nullopt for non-hex
// characters, an odd length or too many bytes.
std::optional<std::size_t> decodeRecord(std::span<const std::byte> hex,
                                        std::array<std::uint8_t, maxRecordBytes>& out) {
    if (hex.size() % 2 != 0 || hex.size() / 2 > out.size()) {
        return std::nullopt;
    }
    const std::size_t count = hex.size() / 2;
    for (std::size_t i = 0; i < count; ++i) {
        const auto high = hexDigit(hex[2 * i]);
        const auto low = hexDigit(hex[(2 * i) + 1]);
        if (!high || !low) {
            return std::nullopt;
        }
        out.at(i) = static_cast<std::uint8_t>((*high << 4U) | *low);
    }
    return count;
}

// Collects data records into segments; consecutive records extend the current segment.
class SegmentBuilder {
public:
    void add(std::uint64_t address, std::span<const std::uint8_t> data) {
        if (data.empty()) {
            return;
        }
        if (m_segments.empty() || m_segments.back().range().end != address) {
            m_segments.push_back(
                Segment{.address = static_cast<std::uint32_t>(address), .data = {}});
        }
        std::vector<std::byte>& target = m_segments.back().data;
        const auto bytes = std::as_bytes(data);
        target.insert(target.end(), bytes.begin(), bytes.end());
        m_total += data.size();
    }
    [[nodiscard]] std::uint64_t total() const noexcept { return m_total; }
    [[nodiscard]] std::vector<Segment> take() { return std::move(m_segments); }

private:
    std::vector<Segment> m_segments;
    std::uint64_t m_total = 0;
};

// One pass over the HEX text; state of the address records between lines.
class HexParser {
public:
    explicit HexParser(std::string_view sourceName) : m_sourceName(sourceName) {}

    [[nodiscard]] Result<FirmwareImage> parse(std::span<const std::byte> text) {
        std::size_t position = 0;
        while (position < text.size()) {
            std::size_t end = position;
            while (end < text.size() && text[end] != std::byte{'\n'}) {
                ++end;
            }
            std::span<const std::byte> line = text.subspan(position, end - position);
            position = end + 1;
            if (Status parsed = parseLine(line); !parsed) {
                return std::unexpected(std::move(parsed).error());
            }
        }
        if (!m_eof) {
            return std::unexpected(fail("the end-of-file record is missing"));
        }
        auto image = FirmwareImage::fromSegments(m_builder.take(), m_startAddress);
        if (!image) {
            Error error = std::move(image).error();
            error.detail = std::string(m_sourceName) + ": " + error.detail;
            return std::unexpected(std::move(error));
        }
        return image;
    }

private:
    [[nodiscard]] Error fail(std::string_view reason) const {
        return parseError(m_lineNumber, m_sourceName, reason);
    }

    [[nodiscard]] Status parseLine(std::span<const std::byte> line) {
        ++m_lineNumber;
        if (m_lineNumber > maxHexLines) {
            return std::unexpected(fail(std::format("more than {} lines", maxHexLines)));
        }
        if (!line.empty() && line.back() == std::byte{'\r'}) {
            line = line.first(line.size() - 1);
        }
        if (line.empty()) {
            return {};
        }
        if (m_eof) {
            return std::unexpected(fail("data after the end-of-file record"));
        }
        if (line.front() != std::byte{':'}) {
            return std::unexpected(fail("a record must start with ':'"));
        }
        const auto recordSize = decodeRecord(line.subspan(1), m_record);
        if (!recordSize || *recordSize < minRecordBytes) {
            return std::unexpected(fail("malformed record"));
        }
        const std::span<const std::uint8_t> bytes = std::span(m_record).first(*recordSize);
        const std::size_t length = bytes[0];
        if (bytes.size() != length + minRecordBytes) {
            return std::unexpected(fail("the byte count does not match the record"));
        }
        std::uint8_t sum = 0;
        for (const std::uint8_t byte : bytes) {
            sum = static_cast<std::uint8_t>(sum + byte);
        }
        if (sum != 0) {
            return std::unexpected(fail("checksum mismatch"));
        }
        const std::uint32_t offset = (std::uint32_t{bytes[1]} << 8U) | bytes[2];
        return handleRecord(bytes[3], offset, bytes.subspan(4, length));
    }

    [[nodiscard]] Status handleRecord(std::uint8_t type, std::uint32_t offset,
                                      std::span<const std::uint8_t> data) {
        switch (type) {
        case recordData:
            return addData(offset, data);
        case recordEof:
            if (!data.empty()) {
                return std::unexpected(fail("the end-of-file record has data"));
            }
            m_eof = true;
            return {};
        case recordSegmentAddress:
            if (data.size() != 2) {
                return std::unexpected(fail("bad extended segment address record"));
            }
            m_segmentBase = std::uint64_t{word16(data)} << 4U;
            m_segmentMode = true;
            return {};
        case recordStartSegment:
            if (data.size() != 4) {
                return std::unexpected(fail("bad start segment address record"));
            }
            // CS:IP as a linear address.
            m_startAddress = static_cast<std::uint32_t>(((word32(data) >> 16U) << 4U) +
                                                        (word32(data) & 0xFFFFU));
            return {};
        case recordLinearAddress:
            if (data.size() != 2) {
                return std::unexpected(fail("bad extended linear address record"));
            }
            m_linearBase = std::uint64_t{word16(data)} << 16U;
            m_segmentMode = false;
            return {};
        case recordStartLinear:
            if (data.size() != 4) {
                return std::unexpected(fail("bad start linear address record"));
            }
            m_startAddress = word32(data);
            return {};
        default:
            return std::unexpected(fail(std::format("unknown record type {:02X}", type)));
        }
    }

    [[nodiscard]] Status addData(std::uint32_t offset, std::span<const std::uint8_t> data) {
        if (m_builder.total() + data.size() > maxImageDataBytes) {
            return std::unexpected(
                fail(std::format("more than {} bytes of data", maxImageDataBytes)));
        }
        if (m_segmentMode) {
            // Segment addressing wraps inside the 64 KiB segment (Intel HEX specification).
            for (std::size_t i = 0; i < data.size(); ++i) {
                m_builder.add(m_segmentBase + ((offset + i) & 0xFFFFU), data.subspan(i, 1));
            }
            return {};
        }
        const std::uint64_t address = m_linearBase + offset;
        if (address + data.size() > (std::uint64_t{1} << 32U)) {
            return std::unexpected(fail("data beyond the 32-bit address space"));
        }
        m_builder.add(address, data);
        return {};
    }

    static std::uint32_t word16(std::span<const std::uint8_t> data) {
        return (std::uint32_t{data[0]} << 8U) | std::uint32_t{data[1]};
    }
    static std::uint32_t word32(std::span<const std::uint8_t> data) {
        return (std::uint32_t{data[0]} << 24U) | (std::uint32_t{data[1]} << 16U) |
               (std::uint32_t{data[2]} << 8U) | std::uint32_t{data[3]};
    }

    std::string_view m_sourceName;
    SegmentBuilder m_builder;
    std::uint64_t m_segmentBase = 0; // type 02: segment * 16 (addresses wrap within 64 KiB)
    std::uint64_t m_linearBase = 0;  // type 04: upper 16 bits
    bool m_segmentMode = false;      // the last address record was type 02
    std::optional<std::uint32_t> m_startAddress;
    bool m_eof = false;
    std::uint64_t m_lineNumber = 0;
    std::array<std::uint8_t, maxRecordBytes> m_record{};
};

} // namespace

Result<FirmwareImage> parseIntelHex(std::span<const std::byte> text, std::string_view sourceName) {
    if (text.size() > maxHexFileBytes) {
        return std::unexpected(makeError(ErrorCode::FirmwareInvalidImage,
                                         std::format("{}: {} bytes, the limit is {} bytes",
                                                     sourceName, text.size(), maxHexFileBytes)));
    }
    HexParser parser(sourceName);
    return parser.parse(text);
}

Result<FirmwareImage> readIntelHexFile(const std::filesystem::path& path) {
    auto bytes = detail::readFileBounded(path, maxHexFileBytes);
    if (!bytes) {
        return std::unexpected(std::move(bytes).error());
    }
    return parseIntelHex(*bytes, toUtf8(path.filename()));
}

} // namespace hio::firmware
