#pragma once

// Terminal-safe device text (ADR-0183, plan/features/0025 M-1). Everything a device sends that is
// shown as text (console lines, BLE advertised names, PIB strings) passes through
// sanitizeDeviceText() once, where it enters the backend, so the GUI, the CLI, console files and
// MCP all see the same text and no front end can be steered by escape sequences:
//   1. escape sequences are removed: CSI (ESC '[' parameters 0x30-0x3F, intermediates 0x20-0x2F,
//      final 0x40-0x7E), OSC (ESC ']' ... BEL or ST = ESC '\'), DCS / SOS / PM / APC
//      (ESC 'P' / 'X' / '^' / '_' ... ST) and every other ESC sequence (ESC, intermediates
//      0x20-0x2F, one final byte); an unterminated sequence runs to the end of the text, which is
//      bounded by its caller (a console line is at most LineAssembler::maxPartialBytes);
//   2. carriage returns act like a terminal that redraws a line (applyCarriageReturns);
//   3. C0 controls except TAB, and DEL, are dropped;
//   4. the bytes are decoded as UTF-8 (invalid sequences -> U+FFFD), then the C1 controls
//      U+0080-U+009F (U+009B acts as CSI on xterm-family terminals) become U+FFFD.
//
// Pure functions; thread-safe.

#include <QString>

#include <string>
#include <string_view>

namespace hwio::device {

// Raw device bytes -> sanitised UTF-8.
[[nodiscard]] std::string sanitizeDeviceText(std::string_view bytes);
// The same for text that is already a QString (its UTF-8 bytes are sanitised).
[[nodiscard]] QString sanitizeDeviceText(const QString& text);

} // namespace hwio::device
