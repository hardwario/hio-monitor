#include "hwio/device/text_sanitizer.h"

#include "hwio/device/line_assembler.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QChar>
#include <QString>
#include <QtTypes>

#include <cstddef>
#include <string>
#include <string_view>

namespace hwio::device {

namespace {

constexpr char escape = '\x1b';
constexpr char bell = '\x07';
constexpr char tab = '\t';
constexpr char del = '\x7f';
constexpr char16_t c1First = 0x80;
constexpr char16_t c1Last = 0x9f;

bool inRange(char c, char low, char high) { return c >= low && c <= high; }

// Index after an OSC / DCS / SOS / PM / APC string that starts at `i` (after "ESC x"): after
// ST (ESC '\'), or after BEL when `bellEnds` (OSC); the end of `bytes` when unterminated.
std::size_t skipString(std::string_view bytes, std::size_t i, bool bellEnds) {
    while (i < bytes.size()) {
        if (bellEnds && bytes[i] == bell) {
            return i + 1;
        }
        if (bytes[i] == escape && i + 1 < bytes.size() && bytes[i + 1] == '\\') {
            return i + 2;
        }
        ++i;
    }
    return i;
}

// Step 1: every escape sequence removed (see the header).
std::string stripEscapeSequences(std::string_view bytes) {
    std::string result;
    result.reserve(bytes.size());
    std::size_t i = 0;
    while (i < bytes.size()) {
        if (bytes[i] != escape) {
            result += bytes[i];
            ++i;
            continue;
        }
        ++i; // ESC
        if (i >= bytes.size()) {
            break;
        }
        const char kind = bytes[i];
        ++i;
        switch (kind) {
        case '[': // CSI
            while (i < bytes.size() && inRange(bytes[i], 0x30, 0x3F)) {
                ++i;
            }
            while (i < bytes.size() && inRange(bytes[i], 0x20, 0x2F)) {
                ++i;
            }
            if (i < bytes.size() && inRange(bytes[i], 0x40, 0x7E)) {
                ++i;
            }
            break;
        case ']': // OSC
            i = skipString(bytes, i, true);
            break;
        case 'P': // DCS
        case 'X': // SOS
        case '^': // PM
        case '_': // APC
            i = skipString(bytes, i, false);
            break;
        default:
            // Other ESC sequences: intermediates, then one final byte (ESC c, ESC ( B, ...).
            if (inRange(kind, 0x20, 0x2F)) {
                while (i < bytes.size() && inRange(bytes[i], 0x20, 0x2F)) {
                    ++i;
                }
                if (i < bytes.size() && inRange(bytes[i], 0x30, 0x7E)) {
                    ++i;
                }
            }
            break;
        }
    }
    return result;
}

// Step 3: C0 controls except TAB, and DEL, dropped.
std::string dropControls(std::string_view bytes) {
    std::string result;
    result.reserve(bytes.size());
    for (const char c : bytes) {
        if ((inRange(c, 0x00, 0x1F) && c != tab) || c == del) {
            continue;
        }
        result += c;
    }
    return result;
}

} // namespace

std::string sanitizeDeviceText(std::string_view bytes) {
    const std::string text = dropControls(applyCarriageReturns(stripEscapeSequences(bytes)));
    QString decoded =
        QString::fromUtf8(QByteArrayView(text.data(), static_cast<qsizetype>(text.size())));
    for (QChar& c : decoded) {
        if (c.unicode() >= c1First && c.unicode() <= c1Last) {
            c = QChar::ReplacementCharacter;
        }
    }
    return decoded.toStdString();
}

QString sanitizeDeviceText(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return QString::fromStdString(sanitizeDeviceText(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
}

} // namespace hwio::device
