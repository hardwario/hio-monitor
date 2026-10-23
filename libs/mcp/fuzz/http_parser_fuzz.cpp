// libFuzzer target for HttpParser (ADR-0152): arbitrary bytes fed in fuzzer-chosen chunk sizes,
// with acceptBody() after every head and reset() after every request, as HttpConnection does.
// Invariants: feed() never consumes more than it was given, never stalls with input left in the
// Headers/Body states, and every complete request respects the limits.

#include "hio/mcp/http_parser.h"

#include <QByteArrayView>
#include <QtTypes>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <utility>

namespace {

void check(bool condition) {
    if (!condition) {
        std::abort();
    }
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): the libFuzzer entry point
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using hio::mcp::HttpLimits;
    using hio::mcp::HttpParser;
    if (size == 0) {
        return 0;
    }
    // Small limits so the fuzzer reaches them; the first byte picks the chunk size.
    const HttpLimits limits{.maxHeaderBytes = 512, .maxHeaderCount = 8, .maxBodyBytes = 256};
    const std::span<const std::uint8_t> bytes(data, size);
    const auto chunk = static_cast<qsizetype>((bytes.front() % 64) + 1);
    const std::span<const std::uint8_t> rest = bytes.subspan(1);
    const QByteArrayView input(rest.data(), static_cast<qsizetype>(rest.size()));
    HttpParser parser(limits);
    qsizetype offset = 0;
    int stalls = 0;
    while (offset < input.size() && stalls < 4) {
        const QByteArrayView piece = input.sliced(
            offset, std::min<qsizetype>(static_cast<qsizetype>(chunk), input.size() - offset));
        const std::size_t used = parser.feed(piece);
        check(std::cmp_less_equal(used, piece.size()));
        offset += static_cast<qsizetype>(used);
        switch (parser.state()) {
        case HttpParser::State::HeadersComplete:
            check(parser.request().headers.size() <= limits.maxHeaderCount);
            parser.acceptBody();
            break;
        case HttpParser::State::Complete:
            check(std::cmp_less_equal(parser.request().body.size(), limits.maxBodyBytes));
            (void)parser.request().path();
            parser.reset();
            break;
        case HttpParser::State::Error:
            return 0;
        case HttpParser::State::Headers:
        case HttpParser::State::Body:
            check(std::cmp_equal(used, piece.size()));
            break;
        }
        stalls = used == 0 ? stalls + 1 : 0;
    }
    return 0;
}
