#pragma once

// HttpServer - the MCP Streamable HTTP transport on 127.0.0.1 (plan/features/0013 "HTTP transport",
// ADR-0096, ADR-0102). One endpoint: POST /mcp (both dialects), GET /mcp (legacy session stream),
// DELETE /mcp (legacy session end); OPTIONS and other methods -> 405, other paths -> 404.
//
// Check order per request (after the request line and headers were read; malformed -> 400,
// HTTP/1.0 or other versions -> 505, headers over Limits::maxHeaderBytes -> 431 while reading):
//   Host (421) -> Origin, Sec-Fetch-Site cross-site/same-site (403) -> lockout (429) / bearer
//   token (401 + WWW-Authenticate: Bearer)
//   -> path (404) / method (405) -> Transfer-Encoding (411) / missing Content-Length on POST (411)
//   -> body size (413) -> Content-Type application/json (415, POST) -> Accept (406)
//   -> dialect headers and JSON-RPC (McpServer::handle()).
// Every refusal closes the connection after the response (aborted when the client has not read it
// within Limits::closingTimeout) and is logged in hio.mcp (never the token). After
// Limits::authFailureLimit failed tokens within Limits::authFailureWindow every request gets 429
// for Limits::authLockout; only requests that carry an Authorization header count as failures.
//
// Responses: application/json when the call is answered within Limits::jsonResponseWindow and
// had no progress token; otherwise text/event-stream (chunked; "event: message" per message, a
// ": heartbeat" comment every Limits::sseHeartbeat) when the client accepts it. Closing the stream
// cancels a modern call. A connection whose send queue exceeds Limits::maxSendQueueBytes is
// dropped and its calls are cancelled. At most Limits::maxHttpConnections connections; more are
// answered 503 and closed. Timeouts: headers Limits::headerTimeout, body Limits::bodyTimeout,
// idle keep-alive Limits::idleTimeout.
//
// Thread safety: owner thread only.

#include "hio/core/result.h"

#include <QObject>
#include <QtTypes>

#include <cstddef>
#include <memory>
#include <string>

namespace hio::mcp {

class McpServer;

struct HttpServerOptions {
    quint16 port = 47800; // 0: any free port (tests); the CLI and GUI validate 1024-65535
    std::string token;    // 43 base64url characters (TokenStore); never logged
};

class HttpServer : public QObject {
    Q_OBJECT

public:
    // `server` must outlive the HTTP server; its Limits apply.
    HttpServer(McpServer& server, HttpServerOptions options, QObject* parent = nullptr);
    ~HttpServer() override;
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    HttpServer(HttpServer&&) = delete;
    HttpServer& operator=(HttpServer&&) = delete;

    // Listens on 127.0.0.1:port. Errors: mcp.port_in_use (detail: port), mcp.listen_failed
    // (detail: port and the OS error text).
    [[nodiscard]] Status listen();
    // Stops listening and closes every connection (their calls are cancelled).
    void close();

    [[nodiscard]] bool isListening() const;
    // The bound port (the requested one, or the chosen one for port 0).
    [[nodiscard]] quint16 port() const;
    [[nodiscard]] std::size_t connectionCount() const;
    // A new token for the next requests (token regeneration in plan 0014).
    void setToken(std::string token);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hio::mcp
