#pragma once

// FirmwareImage - the bytes a firmware writes into the MCU's memory, as sorted, non-overlapping
// segments (plan/features/0003-m2-flashing.md, ADR-0035). Built by parseIntelHex(); immutable
// once shared: images cross threads as std::shared_ptr<const FirmwareImage>.

#include "hio/core/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hio::firmware {

// A half-open address range [start, end). end may be 2^32 (the 64-bit type allows it).
struct AddressRange {
    std::uint64_t start = 0;
    std::uint64_t end = 0;

    [[nodiscard]] constexpr std::uint64_t size() const noexcept {
        return end > start ? end - start : 0;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return end <= start; }
    [[nodiscard]] constexpr bool contains(std::uint64_t address) const noexcept {
        return address >= start && address < end;
    }
    // `other` lies completely inside this range (an empty `other` counts as inside).
    [[nodiscard]] constexpr bool contains(const AddressRange& other) const noexcept {
        return other.empty() || (other.start >= start && other.end <= end);
    }
    [[nodiscard]] constexpr bool overlaps(const AddressRange& other) const noexcept {
        return !empty() && !other.empty() && other.start < end && start < other.end;
    }
    friend constexpr bool operator==(const AddressRange&, const AddressRange&) = default;
};

struct Segment {
    std::uint32_t address = 0;
    std::vector<std::byte> data;

    [[nodiscard]] AddressRange range() const noexcept {
        return AddressRange{.start = address, .end = std::uint64_t{address} + data.size()};
    }
    friend bool operator==(const Segment&, const Segment&) = default;
};

class FirmwareImage {
public:
    FirmwareImage() = default;

    // Sorts the segments, merges adjacent and identical overlapping ones and drops empty ones.
    // Errors: firmware.hex_parse_error when segments overlap with different bytes (the address in
    // `detail`), usage.invalid_argument when a segment crosses the end of the 32-bit space.
    [[nodiscard]] static Result<FirmwareImage>
    fromSegments(std::vector<Segment> segments, std::optional<std::uint32_t> startAddress = {});

    [[nodiscard]] const std::vector<Segment>& segments() const noexcept { return m_segments; }
    // Data bytes (sum of the segment sizes).
    [[nodiscard]] std::uint64_t totalBytes() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return m_segments.empty(); }
    // Lowercase hex SHA-256 over, per segment in address order: the address and the size as 32-bit
    // little-endian values, then the data. Stable for equal images, independent of the HEX text.
    [[nodiscard]] std::string sha256() const;
    // Start address record (type 03 CS:IP as CS * 16 + IP, or type 05), when present.
    [[nodiscard]] std::optional<std::uint32_t> startAddress() const noexcept {
        return m_startAddress;
    }
    // True when any data byte lies inside `range`.
    [[nodiscard]] bool intersects(const AddressRange& range) const noexcept;
    // Moves the bytes inside `range` into a new image and removes them from this one (used for
    // UICR records, ADR-0036). Errors: usage.invalid_argument for an empty range.
    [[nodiscard]] Result<FirmwareImage> splitOut(const AddressRange& range);

    friend bool operator==(const FirmwareImage&, const FirmwareImage&) = default;

private:
    std::vector<Segment> m_segments; // sorted, non-overlapping, adjacent ones merged
    std::optional<std::uint32_t> m_startAddress;
};

} // namespace hio::firmware
