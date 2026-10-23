#pragma once

// M6 low-level target commands of `hio chester app` and `hio chester lte` (plan/features/0013 B4,
// ADR-0099, ADR-0153):
//   status | halt | go | regs | zones
//   mem read ADDR [-l LEN=64] [-w 8|16|32] [-o FILE]
//   mem write ADDR (HEX | -i FILE) [-w 8|16|32] [--yes]
//   flash-write ADDR (HEX | -i FILE) [--allow-uicr] [--yes]
// Each opens a flash-style session (attachRtt = false) on the group's target through the
// operation runner. Addresses (and LEN) are decimal or 0x hex; results print addresses as
// 0x%08X. `mem write` and `flash-write` ask [y/N] through confirmDestructive (ADR-0153).
// The CLI is English only (ADR-0027).

#include "cli_app.h"
#include "hio/core/result.h"
#include "hio/device/operation.h"
#include "probe_options.h"

#include <QJsonObject>

#include <CLI/App.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hio::cli {

// `mem read` bounds: at most 16 MiB in total, read in operations of maxReadMemoryBytes (64 KiB).
inline constexpr std::size_t maxMemReadBytes = 16ULL * 1024 * 1024;
inline constexpr std::size_t memReadChunkBytes = device::maxReadMemoryBytes;

void registerTargetCommands(CLI::App& targetGroup, const std::shared_ptr<ProbeOptions>& group,
                            CommandSlot& slot, const GlobalOptions& globals,
                            std::string_view targetId);

// ---- exposed for tests ----

// "0x" + 1..8 hex digits, or 1..10 decimal digits <= 0xFFFFFFFF (the MCP address pattern).
// Errors: usage.invalid_argument (detail: the text).
[[nodiscard]] Result<std::uint32_t> parseAddress(std::string_view text);
// An even, non-zero number of hex digits: the bytes in address order. Errors:
// usage.invalid_argument.
[[nodiscard]] Result<std::vector<std::byte>> parseHexBytes(std::string_view text);
// 16 bytes per line: "0x20000000  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  |........|",
// with 16/32-bit widths the units (little-endian) instead of the bytes. No trailing newline.
[[nodiscard]] std::string hexDump(std::uint32_t address, std::span<const std::byte> data,
                                  device::AccessWidth width);
// "0x%08X".
[[nodiscard]] std::string hex32(std::uint64_t value);
// `regs`: one "NAME  0x%08X" line per register in the library's order / {"registers":{...}}.
[[nodiscard]] std::string registersText(std::span<const device::RegisterValue> registers);
[[nodiscard]] QJsonObject registersJson(std::span<const device::RegisterValue> registers);
// `zones`: one "NAME  0x%08X  DESCRIPTION" line per zone ("No memory zones." when empty) /
// {"zones":[{"name","description","virtualAddress"}]}.
[[nodiscard]] std::string zonesText(std::span<const device::MemoryZone> zones);
[[nodiscard]] QJsonObject zonesJson(std::span<const device::MemoryZone> zones);

} // namespace hio::cli
