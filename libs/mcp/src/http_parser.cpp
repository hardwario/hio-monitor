#include "hio/mcp/http_parser.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QtTypes>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

namespace hio::mcp {

namespace {

constexpr QByteArrayView crlf("\r\n");
constexpr QByteArrayView headEnd("\r\n\r\n");
// Content-Length values beyond this are refused (far above every body limit).
constexpr std::size_t maxContentLengthDigits = 15;

bool isTokenChar(char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return true;
    }
    switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
        return true;
    default:
        return false;
    }
}

bool isToken(QByteArrayView text) {
    return !text.isEmpty() && std::ranges::all_of(text, isTokenChar);
}

// Field values: visible ASCII, space, tab and obs-text (>= 0x80); no other control characters.
bool isFieldValue(QByteArrayView text) {
    return std::ranges::all_of(text, [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte == '\t' || (byte >= 0x20 && byte != 0x7F);
    });
}

bool isTarget(QByteArrayView text) {
    return !text.isEmpty() && std::ranges::all_of(text, [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte > 0x20 && byte < 0x7F;
    });
}

QByteArrayView trimmed(QByteArrayView text) {
    while (!text.isEmpty() && (text.front() == ' ' || text.front() == '\t')) {
        text = text.sliced(1);
    }
    while (!text.isEmpty() && (text.back() == ' ' || text.back() == '\t')) {
        text.chop(1);
    }
    return text;
}

bool isVersionShape(QByteArrayView version) {
    return version.size() == 8 && version.startsWith("HTTP/") && version[5] >= '0' &&
           version[5] <= '9' && version[6] == '.' && version[7] >= '0' && version[7] <= '9';
}

std::optional<std::size_t> parseLength(QByteArrayView text) {
    if (text.isEmpty() || std::cmp_greater(text.size(), maxContentLengthDigits) ||
        !std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    std::size_t value = 0;
    for (const char c : text) {
        value = (value * 10) + static_cast<std::size_t>(c - '0');
    }
    return value;
}

} // namespace

std::optional<QByteArray> HttpRequest::header(QByteArrayView lowerName) const {
    for (const auto& [name, value] : headers) {
        if (name == lowerName) {
            return value;
        }
    }
    return std::nullopt;
}

std::size_t HttpRequest::headerCount(QByteArrayView lowerName) const {
    return static_cast<std::size_t>(std::ranges::count_if(
        headers, [lowerName](const auto& field) { return field.first == lowerName; }));
}

QByteArray HttpRequest::path() const {
    const qsizetype query = target.indexOf('?');
    return query < 0 ? target : target.left(query);
}

void HttpParser::fail(HttpParseError error) {
    m_state = State::Error;
    m_error = error;
}

void HttpParser::reset() {
    m_state = State::Headers;
    m_error = HttpParseError::None;
    m_request = HttpRequest{};
    m_head.clear();
    m_skipped = 0;
    m_started = false;
}

std::size_t HttpParser::feed(QByteArrayView data) {
    if (!data.isEmpty() && (m_state == State::Headers || m_state == State::Body)) {
        m_started = true;
    }
    std::size_t consumed = 0;
    if (m_state == State::Headers) {
        consumed = feedHead(data);
    }
    if (m_state == State::Body && std::cmp_less(consumed, data.size())) {
        consumed += feedBody(data.sliced(static_cast<qsizetype>(consumed)));
    }
    return consumed;
}

std::size_t HttpParser::feedHead(QByteArrayView data) {
    std::size_t consumed = 0;
    const auto size = static_cast<std::size_t>(data.size());
    // Skip empty lines before the request line.
    while (m_head.isEmpty() && consumed < size &&
           (data[static_cast<qsizetype>(consumed)] == '\r' ||
            data[static_cast<qsizetype>(consumed)] == '\n')) {
        ++consumed;
        if (++m_skipped >= m_limits.maxHeaderBytes) {
            fail(HttpParseError::HeaderTooLarge);
            return consumed;
        }
    }
    if (consumed == size) {
        return consumed;
    }
    // m_head.size() + m_skipped < maxHeaderBytes holds here (checked below on every call).
    const std::size_t room =
        m_limits.maxHeaderBytes - m_skipped - static_cast<std::size_t>(m_head.size());
    const std::size_t take = std::min(size - consumed, room);
    const qsizetype searchFrom = std::max<qsizetype>(0, m_head.size() - 3);
    m_head.append(data.sliced(static_cast<qsizetype>(consumed), static_cast<qsizetype>(take)));
    const qsizetype end = m_head.indexOf(headEnd, searchFrom);
    if (end < 0) {
        consumed += take;
        if (static_cast<std::size_t>(m_head.size()) + m_skipped >= m_limits.maxHeaderBytes) {
            fail(HttpParseError::HeaderTooLarge);
        }
        return consumed;
    }
    const auto headSize = static_cast<std::size_t>(end) + headEnd.size();
    consumed += take - (static_cast<std::size_t>(m_head.size()) - headSize);
    m_head.truncate(static_cast<qsizetype>(headSize));
    parseHead();
    return consumed;
}

std::size_t HttpParser::feedBody(QByteArrayView data) {
    const std::size_t expected = m_request.contentLength.value_or(0);
    const std::size_t missing = expected - static_cast<std::size_t>(m_request.body.size());
    const std::size_t take = std::min(static_cast<std::size_t>(data.size()), missing);
    m_request.body.append(data.first(static_cast<qsizetype>(take)));
    if (std::cmp_equal(m_request.body.size(), expected)) {
        m_state = State::Complete;
    }
    return take;
}

void HttpParser::acceptBody() {
    if (m_state != State::HeadersComplete) {
        return;
    }
    if (m_request.hasTransferEncoding) {
        fail(HttpParseError::BadRequest);
        return;
    }
    const std::size_t length = m_request.contentLength.value_or(0);
    if (length > m_limits.maxBodyBytes) {
        fail(HttpParseError::BodyTooLarge);
        return;
    }
    m_request.body.reserve(static_cast<qsizetype>(length));
    m_state = length == 0 ? State::Complete : State::Body;
}

bool HttpParser::parseRequestLine(QByteArrayView line) {
    const qsizetype space1 = line.indexOf(' ');
    const qsizetype space2 = space1 < 0 ? -1 : line.indexOf(' ', space1 + 1);
    if (space1 <= 0 || space2 < 0 || line.indexOf(' ', space2 + 1) >= 0) {
        fail(HttpParseError::BadRequest);
        return false;
    }
    const QByteArrayView method = line.first(space1);
    const QByteArrayView target = line.sliced(space1 + 1, space2 - space1 - 1);
    const QByteArrayView version = line.sliced(space2 + 1);
    if (!isToken(method) || !isTarget(target) || !isVersionShape(version)) {
        fail(HttpParseError::BadRequest);
        return false;
    }
    if (version != QByteArrayView("HTTP/1.1")) {
        fail(HttpParseError::VersionNotSupported);
        return false;
    }
    m_request.method = method.toByteArray();
    m_request.target = target.toByteArray();
    m_request.version = version.toByteArray();
    return true;
}

bool HttpParser::parseHeaderLine(QByteArrayView line) {
    // No obs-fold (a line starting with white space), "name: value" with a token name.
    const qsizetype colon = line.indexOf(':');
    if (line.isEmpty() || line.front() == ' ' || line.front() == '\t' || colon <= 0) {
        fail(HttpParseError::BadRequest);
        return false;
    }
    const QByteArrayView name = line.first(colon);
    const QByteArrayView value = trimmed(line.sliced(colon + 1));
    if (!isToken(name) || !isFieldValue(value)) {
        fail(HttpParseError::BadRequest);
        return false;
    }
    if (m_request.headers.size() >= m_limits.maxHeaderCount) {
        fail(HttpParseError::HeaderTooLarge);
        return false;
    }
    m_request.headers.emplace_back(name.toByteArray().toLower(), value.toByteArray());
    return true;
}

bool HttpParser::applyFraming() {
    m_request.hasTransferEncoding = m_request.headerCount("transfer-encoding") > 0;
    // Every Content-Length must be valid and all of them equal.
    const bool lengthOk = std::ranges::all_of(m_request.headers, [this](const auto& field) {
        if (field.first != QByteArrayView("content-length")) {
            return true;
        }
        const auto length = parseLength(field.second);
        if (!length || (m_request.contentLength && *m_request.contentLength != *length)) {
            return false;
        }
        m_request.contentLength = length;
        return true;
    });
    if (!lengthOk) {
        fail(HttpParseError::BadRequest);
    }
    return lengthOk;
}

void HttpParser::parseHead() {
    // m_head ends with CRLF CRLF; drop the final empty line.
    const QByteArrayView head = QByteArrayView(m_head).chopped(2);
    qsizetype lineStart = 0;
    bool first = true;
    while (lineStart < head.size()) {
        const qsizetype lineEnd = head.indexOf(crlf, lineStart);
        if (lineEnd < 0) {
            fail(HttpParseError::BadRequest);
            return;
        }
        const QByteArrayView line = head.sliced(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + crlf.size();
        if (line.contains('\n') || line.contains('\r')) {
            fail(HttpParseError::BadRequest); // bare CR or LF
            return;
        }
        const bool ok = first ? parseRequestLine(line) : parseHeaderLine(line);
        if (!ok) {
            return;
        }
        first = false;
    }
    if (first) {
        fail(HttpParseError::BadRequest);
        return;
    }
    if (!applyFraming()) {
        return;
    }
    m_head.clear();
    m_state = State::HeadersComplete;
}

} // namespace hio::mcp
