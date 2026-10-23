// HttpParser: table-driven malformed input, limits, incremental feeding, keep-alive reuse
// (plan/features/0013 "HTTP transport"; the fuzzer covers the rest, ADR-0152).

#include "hio/mcp/http_parser.h"

#include <QByteArray>
#include <QtTypes>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <utility>

namespace hio::mcp {
namespace {

HttpParser parseAll(const QByteArray& input, HttpLimits limits = {}) {
    HttpParser parser(limits);
    std::size_t offset = 0;
    while (std::cmp_less(offset, input.size())) {
        offset += parser.feed(QByteArrayView(input).sliced(static_cast<qsizetype>(offset)));
        if (parser.state() == HttpParser::State::HeadersComplete) {
            parser.acceptBody();
        } else if (parser.state() == HttpParser::State::Complete ||
                   parser.state() == HttpParser::State::Error) {
            break;
        }
    }
    if (parser.state() == HttpParser::State::HeadersComplete) {
        parser.acceptBody();
    }
    return parser;
}

TEST(HttpParser, ParsesAPostRequest) {
    const QByteArray input = "POST /mcp?x=1 HTTP/1.1\r\nHost: 127.0.0.1:1\r\nContent-Type:  "
                             "application/json \r\nContent-Length: 4\r\nX-Empty:\r\n\r\nbody";
    const HttpParser parser = parseAll(input);
    ASSERT_EQ(parser.state(), HttpParser::State::Complete);
    const HttpRequest& request = parser.request();
    EXPECT_EQ(request.method, "POST");
    EXPECT_EQ(request.target, "/mcp?x=1");
    EXPECT_EQ(request.path(), "/mcp");
    EXPECT_EQ(request.header("content-type"), QByteArray("application/json"));
    EXPECT_EQ(request.header("x-empty"), QByteArray());
    EXPECT_FALSE(request.header("missing").has_value());
    EXPECT_EQ(request.body, "body");
    EXPECT_EQ(request.contentLength, 4U);
}

TEST(HttpParser, ByteByByteFeedingAndPipelinedRequests) {
    const QByteArray one = "GET /mcp HTTP/1.1\r\nHost: a\r\n\r\n";
    const QByteArray two = "POST /mcp HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}";
    const QByteArray input = "\r\n" + one + two;
    HttpParser parser(HttpLimits{});
    int complete = 0;
    for (qsizetype i = 0; i < input.size();) {
        const std::size_t used = parser.feed(QByteArrayView(input).sliced(i, 1));
        i += static_cast<qsizetype>(used);
        if (parser.state() == HttpParser::State::HeadersComplete) {
            parser.acceptBody();
        }
        if (parser.state() == HttpParser::State::Complete) {
            ++complete;
            EXPECT_EQ(parser.request().method, complete == 1 ? "GET" : "POST");
            parser.reset();
        }
        ASSERT_NE(parser.state(), HttpParser::State::Error);
    }
    EXPECT_EQ(complete, 2);
}

TEST(HttpParser, RejectsMalformedInput) {
    struct Case {
        const char* text;
        HttpParseError error;
    };
    const std::array cases{
        Case{.text = "GET /mcp HTTP/1.0\r\n\r\n", .error = HttpParseError::VersionNotSupported},
        Case{.text = "GET /mcp HTTP/2.0\r\n\r\n", .error = HttpParseError::VersionNotSupported},
        Case{.text = "GET /mcp HTTP/1.1x\r\n\r\n", .error = HttpParseError::BadRequest},
        Case{.text = "GET /mcp\r\n\r\n", .error = HttpParseError::BadRequest},
        Case{.text = "GET  /mcp HTTP/1.1\r\n\r\n", .error = HttpParseError::BadRequest},
        Case{.text = "G(T /mcp HTTP/1.1\r\n\r\n", .error = HttpParseError::BadRequest},
        Case{.text = "GET /m\x01p HTTP/1.1\r\n\r\n", .error = HttpParseError::BadRequest},
        Case{.text = "GET /mcp HTTP/1.1\r\nNo-Colon\r\n\r\n", .error = HttpParseError::BadRequest},
        Case{.text = "GET /mcp HTTP/1.1\r\nBad Name: x\r\n\r\n",
             .error = HttpParseError::BadRequest},
        Case{.text = "GET /mcp HTTP/1.1\r\n: x\r\n\r\n", .error = HttpParseError::BadRequest},
        Case{.text = "GET /mcp HTTP/1.1\r\nA: b\r\n folded\r\n\r\n",
             .error = HttpParseError::BadRequest},
        Case{.text = "GET /mcp HTTP/1.1\r\nA: b\x01\r\n\r\n", .error = HttpParseError::BadRequest},
        Case{.text = "GET /mcp HTTP/1.1\r\nA: b\nC: d\r\n\r\n",
             .error = HttpParseError::BadRequest},
        Case{.text = "POST /mcp HTTP/1.1\r\nContent-Length: -1\r\n\r\n",
             .error = HttpParseError::BadRequest},
        Case{.text = "POST /mcp HTTP/1.1\r\nContent-Length: 1 2\r\n\r\n",
             .error = HttpParseError::BadRequest},
        Case{.text = "POST /mcp HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
             .error = HttpParseError::BadRequest},
        Case{.text = "POST /mcp HTTP/1.1\r\nContent-Length: 99999999999999999999\r\n\r\n",
             .error = HttpParseError::BadRequest},
        Case{.text = "POST /mcp HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
             .error = HttpParseError::BadRequest},
        Case{.text = "POST /mcp HTTP/1.1\r\nContent-Length: 5000\r\n\r\n",
             .error = HttpParseError::BodyTooLarge},
    };
    for (const Case& c : cases) {
        const HttpParser parser = parseAll(
            QByteArray(c.text),
            HttpLimits{.maxHeaderBytes = 1024, .maxHeaderCount = 10, .maxBodyBytes = 4096});
        EXPECT_EQ(parser.state(), HttpParser::State::Error) << c.text;
        EXPECT_EQ(parser.error(), c.error) << c.text;
    }
}

TEST(HttpParser, HeaderLimitIsExact) {
    const HttpLimits limits{.maxHeaderBytes = 64, .maxHeaderCount = 3, .maxBodyBytes = 16};
    const QByteArray start = "GET /mcp HTTP/1.1\r\nA: ";
    const QByteArray end = "\r\n\r\n";
    const QByteArray fits = start + QByteArray(64 - start.size() - end.size(), 'x') + end;
    ASSERT_EQ(fits.size(), 64);
    EXPECT_EQ(parseAll(fits, limits).state(), HttpParser::State::Complete);
    const QByteArray tooLarge = start + QByteArray(65 - start.size() - end.size(), 'x') + end;
    EXPECT_EQ(parseAll(tooLarge, limits).error(), HttpParseError::HeaderTooLarge);
    // Without an end: rejected as soon as the limit is reached.
    EXPECT_EQ(parseAll(start + QByteArray(1000, 'x'), limits).error(),
              HttpParseError::HeaderTooLarge);
    EXPECT_EQ(parseAll(QByteArray(1000, '\n'), limits).error(), HttpParseError::HeaderTooLarge);
    EXPECT_EQ(parseAll("GET /mcp HTTP/1.1\r\nA: 1\r\nB: 2\r\nC: 3\r\nD: 4\r\n\r\n",
                       HttpLimits{.maxHeaderBytes = 1024, .maxHeaderCount = 3, .maxBodyBytes = 0})
                  .error(),
              HttpParseError::HeaderTooLarge);
}

TEST(HttpParser, BodyLimitIsExact) {
    const HttpLimits limits{.maxHeaderBytes = 1024, .maxHeaderCount = 10, .maxBodyBytes = 4};
    EXPECT_EQ(parseAll("POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\nabcd", limits).state(),
              HttpParser::State::Complete);
    EXPECT_EQ(parseAll("POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\nabcde", limits).error(),
              HttpParseError::BodyTooLarge);
}

TEST(HttpParser, HeadersCompleteWaitsForTheCaller) {
    HttpParser parser(HttpLimits{});
    const QByteArray input = "POST /mcp HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}";
    const std::size_t used = parser.feed(input);
    EXPECT_EQ(parser.state(), HttpParser::State::HeadersComplete);
    EXPECT_EQ(used, static_cast<std::size_t>(input.size() - 2));
    EXPECT_TRUE(parser.started());
    EXPECT_EQ(parser.feed("{}"), 0U);
    parser.acceptBody();
    EXPECT_EQ(parser.feed("{}x"), 2U);
    EXPECT_EQ(parser.state(), HttpParser::State::Complete);
    parser.reset();
    EXPECT_FALSE(parser.started());
    EXPECT_EQ(parser.state(), HttpParser::State::Headers);
}

} // namespace
} // namespace hio::mcp
