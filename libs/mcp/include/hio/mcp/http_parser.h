#pragma once

// HttpParser - incremental, bounded HTTP/1.1 request parser for the MCP HTTP transport
// (plan/features/0013 "HTTP transport", ADR-0096, ADR-0102). Fuzzed by fuzz/http_parser_fuzz.cpp.
//
// Only what MCP needs: request line, header fields, Content-Length bodies. Strict: CRLF line ends,
// no obs-fold, token header names, no control characters in values, one consistent
// Content-Length. Leading empty lines before the request line are skipped (counted toward the
// header limit).
//
// Flow per request: feed() until state() is HeadersComplete; the caller checks the headers and
// calls acceptBody() (or refuses the request); feed() until Complete; reset() for the next request
// on the same connection. feed() returns how many bytes it consumed: bytes after a complete head
// (HeadersComplete) or request (Complete) are left for the caller.
//
// Bounds: request line + headers + the empty line (and skipped leading CR/LF) <= maxHeaderBytes
// (else HeaderTooLarge), <= maxHeaderCount header lines (HeaderTooLarge), body <= maxBodyBytes
// (acceptBody() -> BodyTooLarge).

#include <QByteArray>
#include <QByteArrayView>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace hio::mcp {

struct HttpLimits {
    std::size_t maxHeaderBytes = std::size_t{16} * 1024;
    std::size_t maxHeaderCount = 100;
    std::size_t maxBodyBytes = std::size_t{4} * 1024 * 1024;
};

struct HttpRequest {
    QByteArray method;                                      // e.g. "POST"
    QByteArray target;                                      // e.g. "/mcp?x=1"
    QByteArray version;                                     // "HTTP/1.1"
    std::vector<std::pair<QByteArray, QByteArray>> headers; // names lower-cased, values trimmed
    QByteArray body;
    std::optional<std::size_t> contentLength;
    bool hasTransferEncoding = false;

    // The first value of a header (`lowerName` in lower case), nullopt when absent.
    [[nodiscard]] std::optional<QByteArray> header(QByteArrayView lowerName) const;
    [[nodiscard]] std::size_t headerCount(QByteArrayView lowerName) const;
    // The target without the query string.
    [[nodiscard]] QByteArray path() const;
};

enum class HttpParseError : std::uint8_t {
    None,
    BadRequest,          // 400
    HeaderTooLarge,      // 431
    VersionNotSupported, // 505 (HTTP/1.0 and anything but HTTP/1.1)
    BodyTooLarge,        // 413
};

class HttpParser {
public:
    enum class State : std::uint8_t {
        Headers,         // reading the request line and headers
        HeadersComplete, // waiting for acceptBody()
        Body,            // reading Content-Length bytes
        Complete,        // request() is complete
        Error,           // error() says why; the connection must be closed
    };

    explicit HttpParser(HttpLimits limits) : m_limits(limits) {}

    // Consumes bytes in Headers and Body state; consumes nothing in the other states.
    [[nodiscard]] std::size_t feed(QByteArrayView data);
    // HeadersComplete -> Body (or Complete for an empty body); BodyTooLarge / BadRequest
    // (Transfer-Encoding) -> Error.
    void acceptBody();
    void reset();

    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] HttpParseError error() const noexcept { return m_error; }
    [[nodiscard]] const HttpRequest& request() const noexcept { return m_request; }
    // True once at least one byte of the current request was received.
    [[nodiscard]] bool started() const noexcept { return m_started; }

private:
    void fail(HttpParseError error);
    [[nodiscard]] std::size_t feedHead(QByteArrayView data);
    [[nodiscard]] std::size_t feedBody(QByteArrayView data);
    void parseHead();
    [[nodiscard]] bool parseRequestLine(QByteArrayView line);
    [[nodiscard]] bool parseHeaderLine(QByteArrayView line);
    [[nodiscard]] bool applyFraming();

    HttpLimits m_limits;
    State m_state = State::Headers;
    HttpParseError m_error = HttpParseError::None;
    HttpRequest m_request;
    QByteArray m_head;
    std::size_t m_skipped = 0; // leading CR/LF bytes before the request line
    bool m_started = false;
};

} // namespace hio::mcp
