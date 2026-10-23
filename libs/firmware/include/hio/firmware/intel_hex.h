#pragma once

// Intel HEX parser (plan/features/0003-m2-flashing.md, ADR-0035).
//
// Record types: 00 data, 01 end of file, 02 extended segment address, 03 start segment address,
// 04 extended linear address, 05 start linear address. Every record's checksum is checked; lines
// end with "\r\n" or "\n"; empty lines are ignored. The EOF record is required and nothing but
// empty lines may follow it. Overlapping data is an error unless the bytes are identical.
// Errors: firmware.hex_parse_error, the line number in the message and the reason in `detail`.

#include "hio/core/result.h"
#include "hio/firmware/firmware_image.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stop_token>
#include <string_view>

namespace hio::firmware {

// Bounds (plan/features/0003, "Bounds").
inline constexpr std::uint64_t maxHexFileBytes = 64ULL * 1024 * 1024;   // input file size
inline constexpr std::uint64_t maxImageDataBytes = 16ULL * 1024 * 1024; // decoded data
inline constexpr std::uint64_t maxHexLines = 1024ULL * 1024;            // lines of the file

// `sourceName` (a file name or "catalog <id>") is only used in `detail`. `stop` is checked before
// every line (review F2: an offloaded parse ends soon after its call was cancelled); a requested
// stop gives `cancelled`.
[[nodiscard]] Result<FirmwareImage> parseIntelHex(std::span<const std::byte> text,
                                                  std::string_view sourceName,
                                                  const std::stop_token& stop = {});

// Reads (at most maxHexFileBytes) and parses a HEX file. Errors: io.file_not_found,
// io.read_failed, firmware.invalid_image (larger than maxHexFileBytes), those of parseIntelHex()
// (`cancelled` when `stop` is requested).
[[nodiscard]] Result<FirmwareImage> readIntelHexFile(const std::filesystem::path& path,
                                                     const std::stop_token& stop = {});

} // namespace hio::firmware
