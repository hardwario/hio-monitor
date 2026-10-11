#include "hio/firmware/firmware_image.h"

#include "hio/core/error.h"
#include "hio/core/result.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hio::firmware {

namespace {

constexpr std::uint64_t addressSpaceEnd = std::uint64_t{1} << 32U;

std::array<char, 4> littleEndian(std::uint32_t value) {
    return {static_cast<char>(value & 0xFFU), static_cast<char>((value >> 8U) & 0xFFU),
            static_cast<char>((value >> 16U) & 0xFFU), static_cast<char>((value >> 24U) & 0xFFU)};
}

} // namespace

Result<FirmwareImage> FirmwareImage::fromSegments(std::vector<Segment> segments,
                                                  std::optional<std::uint32_t> startAddress) {
    std::erase_if(segments, [](const Segment& segment) { return segment.data.empty(); });
    for (const Segment& segment : segments) {
        if (segment.range().end > addressSpaceEnd) {
            return std::unexpected(makeError(
                ErrorCode::UsageInvalidArgument,
                std::format("segment at 0x{:08X} ({} bytes) crosses the end of the address space",
                            segment.address, segment.data.size())));
        }
    }
    std::ranges::stable_sort(segments, {}, &Segment::address);

    FirmwareImage image;
    image.m_startAddress = startAddress;
    for (Segment& segment : segments) {
        if (image.m_segments.empty()) {
            image.m_segments.push_back(std::move(segment));
            continue;
        }
        Segment& last = image.m_segments.back();
        const std::uint64_t lastEnd = last.range().end;
        if (segment.address > lastEnd) {
            image.m_segments.push_back(std::move(segment));
            continue;
        }
        // Adjacent or overlapping: the overlap must hold identical bytes.
        const std::uint64_t overlap =
            std::min<std::uint64_t>(lastEnd - segment.address, segment.data.size());
        const auto lastOverlap =
            last.data.begin() + static_cast<std::ptrdiff_t>(segment.address - last.address);
        for (std::uint64_t i = 0; i < overlap; ++i) {
            if (lastOverlap[static_cast<std::ptrdiff_t>(i)] != segment.data[i]) {
                Error error = makeError(
                    ErrorCode::FirmwareHexParseError,
                    std::format("overlapping data with different values at address 0x{:08X}",
                                segment.address + i));
                return std::unexpected(std::move(error));
            }
        }
        last.data.insert(last.data.end(),
                         segment.data.begin() + static_cast<std::ptrdiff_t>(overlap),
                         segment.data.end());
    }
    return image;
}

std::uint64_t FirmwareImage::totalBytes() const noexcept {
    std::uint64_t total = 0;
    for (const Segment& segment : m_segments) {
        total += segment.data.size();
    }
    return total;
}

std::string FirmwareImage::sha256() const {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const Segment& segment : m_segments) {
        const auto address = littleEndian(segment.address);
        const auto size = littleEndian(static_cast<std::uint32_t>(segment.data.size()));
        hash.addData(QByteArrayView(address.data(), static_cast<qsizetype>(address.size())));
        hash.addData(QByteArrayView(size.data(), static_cast<qsizetype>(size.size())));
        hash.addData(
            QByteArrayView(segment.data.data(), static_cast<qsizetype>(segment.data.size())));
    }
    return hash.result().toHex().toStdString();
}

bool FirmwareImage::intersects(const AddressRange& range) const noexcept {
    return std::ranges::any_of(m_segments,
                               [&range](const Segment& s) { return s.range().overlaps(range); });
}

Result<FirmwareImage> FirmwareImage::splitOut(const AddressRange& range) {
    if (range.empty()) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "splitOut: empty address range"));
    }
    std::vector<Segment> inside;
    std::vector<Segment> outside;
    for (Segment& segment : m_segments) {
        const AddressRange r = segment.range();
        if (!r.overlaps(range)) {
            outside.push_back(std::move(segment));
            continue;
        }
        const std::uint64_t cutStart = std::max(r.start, range.start);
        const std::uint64_t cutEnd = std::min(r.end, range.end);
        const auto at = [&segment](std::uint64_t address) {
            return segment.data.begin() + static_cast<std::ptrdiff_t>(address - segment.address);
        };
        if (cutStart > r.start) {
            outside.push_back(Segment{.address = segment.address,
                                      .data = std::vector<std::byte>(at(r.start), at(cutStart))});
        }
        inside.push_back(Segment{.address = static_cast<std::uint32_t>(cutStart),
                                 .data = std::vector<std::byte>(at(cutStart), at(cutEnd))});
        if (cutEnd < r.end) {
            outside.push_back(Segment{.address = static_cast<std::uint32_t>(cutEnd),
                                      .data = std::vector<std::byte>(at(cutEnd), at(r.end))});
        }
    }
    m_segments = std::move(outside); // still sorted and non-overlapping
    FirmwareImage result;
    result.m_segments = std::move(inside);
    return result;
}

} // namespace hio::firmware
