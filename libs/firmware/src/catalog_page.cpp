// parseCatalogPage() - the public catalog page https://firmware.hardwario.com/chester/<id>
// (ADR-0166, plan/features/0021). The page is HTML for people; the parser reduces it to its text
// segments (tags, comments, <style> and <script> removed; a few entities decoded; whitespace
// collapsed) and reads:
//   - "<name>:<version>" - the first segment that is exactly one such token (the <h2>), or the
//     last token of the "<title>" ("CHESTER Firmware: <name>:<version>");
//   - the HEX SHA-256 - the first "SHA256 checksum: <64 hex>" after a segment mentioning "HEX";
//   - the upload time - the segment after "Upload date/time".
// Bounded: at most maxSegments segments of at most maxSegmentBytes bytes are kept; the input
// itself is capped by the caller (CatalogOptions::maxPageBytes). Fuzzed by catalog_page_fuzz.

#include "hio/core/error.h"
#include "hio/firmware/designated_firmware.h"
#include "hio/firmware/firmware_catalog.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::firmware {

namespace {

constexpr std::size_t maxSegments = 2048;
constexpr std::size_t maxSegmentBytes = 512;

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool startsWithNoCase(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           std::ranges::equal(text.substr(0, prefix.size()), prefix,
                              [](char a, char b) { return lower(a) == lower(b); });
}

bool containsNoCase(std::string_view text, std::string_view needle) {
    if (needle.size() > text.size()) {
        return false;
    }
    for (std::size_t i = 0; i + needle.size() <= text.size(); ++i) {
        if (startsWithNoCase(text.substr(i), needle)) {
            return true;
        }
    }
    return false;
}

// Collapses whitespace runs into one space and decodes the entities the page uses.
class SegmentBuilder {
public:
    explicit SegmentBuilder(std::vector<std::string>& out) : m_out(&out) {}

    void text(std::string_view raw) {
        std::size_t i = 0;
        while (i < raw.size()) {
            const char c = raw[i];
            if (c == '&') {
                i += entity(raw.substr(i));
                continue;
            }
            put(c);
            ++i;
        }
    }

    void flush() {
        while (!m_current.empty() && m_current.back() == ' ') {
            m_current.pop_back();
        }
        if (!m_current.empty() && m_out->size() < maxSegments) {
            m_out->push_back(std::move(m_current));
        }
        m_current.clear();
    }

private:
    void put(char c) {
        if (isSpace(c)) {
            if (m_current.empty() || m_current.back() == ' ') {
                return;
            }
            c = ' ';
        }
        if (m_current.size() >= maxSegmentBytes) {
            return; // the rest of an over-long segment is dropped
        }
        m_current += c;
    }

    // Decodes the entity at the start of `raw`; returns the bytes consumed.
    std::size_t entity(std::string_view raw) {
        static constexpr std::array<std::pair<std::string_view, char>, 6> entities{{
            {"&nbsp;", ' '},
            {"&amp;", '&'},
            {"&lt;", '<'},
            {"&gt;", '>'},
            {"&quot;", '"'},
            {"&#39;", '\''},
        }};
        for (const auto& [name, value] : entities) {
            if (startsWithNoCase(raw, name)) {
                put(value);
                return name.size();
            }
        }
        put('&');
        return 1;
    }

    std::vector<std::string>* m_out;
    std::string m_current;
};

// The end (one past) of the raw element starting `rest` ("<style" / "<script": up to its closing
// tag), npos when it is not closed.
std::size_t rawElementEnd(std::string_view rest, std::string_view close) {
    for (std::size_t k = 1; k + close.size() <= rest.size(); ++k) {
        if (rest[k] == '<' && startsWithNoCase(rest.substr(k), close)) {
            const std::size_t gt = rest.find('>', k);
            return gt == std::string_view::npos ? gt : gt + 1;
        }
    }
    return std::string_view::npos;
}

// The end (one past) of the markup starting `rest` (a tag, a comment, a style or script element),
// npos when it is not terminated.
std::size_t markupEnd(std::string_view rest) {
    const auto after = [](std::size_t at, std::size_t length) {
        return at == std::string_view::npos ? at : at + length;
    };
    if (rest.starts_with("<!--")) {
        return after(rest.find("-->"), 3);
    }
    if (startsWithNoCase(rest, "<style")) {
        return rawElementEnd(rest, "</style");
    }
    if (startsWithNoCase(rest, "<script")) {
        return rawElementEnd(rest, "</script");
    }
    return after(rest.find('>'), 1);
}

// The text segments of `html` (bounded).
std::vector<std::string> textSegments(std::string_view html) {
    std::vector<std::string> segments;
    SegmentBuilder builder(segments);
    std::size_t i = 0;
    while (i < html.size() && segments.size() < maxSegments) {
        const std::size_t open = html.find('<', i);
        builder.text(
            html.substr(i, open == std::string_view::npos ? std::string_view::npos : open - i));
        if (open == std::string_view::npos) {
            break;
        }
        builder.flush();
        const std::size_t end = markupEnd(html.substr(open));
        if (end == std::string_view::npos) {
            break; // an unterminated tag ends the text
        }
        i = open + end;
    }
    builder.flush();
    return segments;
}

bool isNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_' || c == '.';
}

bool isVersionChar(char c) { return isNameChar(c) || c == '+'; }

// "<name>:<version>" with a version starting with 'v' or a digit.
std::optional<std::pair<std::string, std::string>> nameAndVersion(std::string_view token) {
    const std::size_t colon = token.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= token.size()) {
        return std::nullopt;
    }
    const std::string_view name = token.substr(0, colon);
    const std::string_view version = token.substr(colon + 1);
    const char first = version.front();
    const bool versionStart = first == 'v' || first == 'V' || (first >= '0' && first <= '9');
    if (name.size() > maxCatalogFieldBytes || version.size() > maxCatalogFieldBytes ||
        !versionStart || !std::ranges::all_of(name, isNameChar) ||
        !std::ranges::all_of(version, isVersionChar)) {
        return std::nullopt;
    }
    return std::pair{std::string(name), std::string(version)};
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// "2026-09-07 12:13:53": starts with a date, printable ASCII, bounded.
bool looksLikeTime(std::string_view text) {
    return text.size() >= 10 && text.size() <= maxCatalogFieldBytes && isDigit(text[0]) &&
           isDigit(text[1]) && isDigit(text[2]) && isDigit(text[3]) && text[4] == '-' &&
           std::ranges::all_of(text, [](char c) { return c >= 0x20 && c < 0x7F; });
}

// "<name>:<version>": the first segment that is exactly one such token, else the title.
std::optional<std::pair<std::string, std::string>>
findNameAndVersion(const std::vector<std::string>& segments) {
    for (const std::string& segment : segments) {
        if (auto parsed = nameAndVersion(segment)) {
            return parsed;
        }
    }
    constexpr std::string_view titlePrefix = "CHESTER Firmware:";
    for (const std::string& segment : segments) {
        if (!startsWithNoCase(segment, titlePrefix)) {
            continue;
        }
        const std::string_view rest = std::string_view(segment).substr(titlePrefix.size());
        const std::size_t start = rest.find_first_not_of(' ');
        if (auto parsed = nameAndVersion(start == std::string_view::npos ? std::string_view()
                                                                         : rest.substr(start))) {
            return parsed;
        }
    }
    return std::nullopt;
}

// The first "SHA256 checksum: <hex>" after a segment that mentions the HEX file.
std::optional<Sha256> findHexChecksum(const std::vector<std::string>& segments) {
    constexpr std::string_view checksumPrefix = "SHA256 checksum:";
    bool afterHex = false;
    for (const std::string& segment : segments) {
        if (!startsWithNoCase(segment, checksumPrefix)) {
            afterHex = afterHex || containsNoCase(segment, "HEX");
            continue;
        }
        if (afterHex) {
            std::string_view value = std::string_view(segment).substr(checksumPrefix.size());
            const std::size_t start = value.find_first_not_of(' ');
            return parseSha256(start == std::string_view::npos ? std::string_view()
                                                               : value.substr(start));
        }
    }
    return std::nullopt;
}

// The segment after "Upload date/time" ("" when missing).
std::string findUploaded(const std::vector<std::string>& segments) {
    constexpr std::string_view uploadLabel = "Upload date/time";
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        if (segments[i].size() == uploadLabel.size() &&
            startsWithNoCase(segments[i], uploadLabel) && looksLikeTime(segments[i + 1])) {
            return segments[i + 1];
        }
    }
    return {};
}

Error pageError(std::string detail) {
    return makeError(ErrorCode::NetworkDownloadFailed, "catalog page: " + std::move(detail));
}

} // namespace

std::expected<CatalogInfo, Error> parseCatalogPage(std::string_view html) {
    const std::vector<std::string> segments = textSegments(html);
    auto named = findNameAndVersion(segments);
    if (!named) {
        return std::unexpected(pageError("no <name>:<version> found"));
    }
    return CatalogInfo{.name = std::move(named->first),
                       .version = std::move(named->second),
                       .uploaded = findUploaded(segments),
                       .sha256 = findHexChecksum(segments)};
}

} // namespace hio::firmware
