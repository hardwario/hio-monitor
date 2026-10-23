#include "hio/mcp/http_server.h"

#include "hio/core/error.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/mcp/server.h"
#include "http_connection.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTypes>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hio::mcp {

struct HttpServer::Impl {
    HttpServer& q;
    quint16 requestedPort;
    detail::HttpShared shared;
    QTcpServer tcp;
    std::vector<std::unique_ptr<detail::HttpConnection>> connections;
    bool cleanupScheduled = false;

    Impl(HttpServer& server, McpServer& mcp, HttpServerOptions options)
        : q(server), requestedPort(options.port), shared{.server = &mcp,
                                                         .limits = &mcp.limits(),
                                                         .token = std::move(options.token),
                                                         .port = options.port,
                                                         .auth = detail::AuthGuard(mcp.limits())} {}

    [[nodiscard]] std::size_t openConnections() const {
        return static_cast<std::size_t>(std::ranges::count_if(
            connections, [](const auto& connection) { return !connection->isClosed(); }));
    }

    void onNewConnection() {
        while (tcp.hasPendingConnections()) {
            QTcpSocket* socket = tcp.nextPendingConnection(); // Qt child of `tcp`
            if (socket == nullptr) {
                return;
            }
            if (openConnections() >= shared.limits->maxHttpConnections) {
                qCWarning(hio::log::lcMcp) << "MCP HTTP: connection limit reached ("
                                           << shared.limits->maxHttpConnections << "); refused";
                socket->write("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n"
                              "Content-Length: 0\r\n\r\n");
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                // A client that does not read cannot keep the socket (review R4).
                QTimer::singleShot(shared.limits->closingTimeout, socket, [socket] {
                    socket->abort();
                    socket->deleteLater();
                });
                socket->disconnectFromHost();
                continue;
            }
            auto connection = std::make_unique<detail::HttpConnection>(*socket, shared);
            QObject::connect(connection.get(), &detail::HttpConnection::closed, &q,
                             [this] { scheduleCleanup(); });
            connections.push_back(std::move(connection));
        }
    }

    void scheduleCleanup() {
        if (cleanupScheduled) {
            return;
        }
        cleanupScheduled = true;
        QTimer::singleShot(0, &q, [this] {
            cleanupScheduled = false;
            std::erase_if(connections,
                          [](const auto& connection) { return connection->isClosed(); });
        });
    }
};

HttpServer::HttpServer(McpServer& server, HttpServerOptions options, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this, server, std::move(options))) {
    m_impl->tcp.setMaxPendingConnections(
        static_cast<int>(m_impl->shared.limits->maxHttpConnections));
    connect(&m_impl->tcp, &QTcpServer::newConnection, this, [this] { m_impl->onNewConnection(); });
}

HttpServer::~HttpServer() { close(); }

Status HttpServer::listen() {
    if (m_impl->tcp.isListening()) {
        return {};
    }
    if (!m_impl->tcp.listen(QHostAddress(QHostAddress::LocalHost), m_impl->requestedPort)) {
        const std::string port = std::to_string(m_impl->requestedPort);
        if (m_impl->tcp.serverError() == QAbstractSocket::AddressInUseError) {
            return std::unexpected(makeError(ErrorCode::McpPortInUse, port));
        }
        return std::unexpected(makeError(ErrorCode::McpListenFailed,
                                         port + ": " + m_impl->tcp.errorString().toStdString()));
    }
    m_impl->shared.port = m_impl->tcp.serverPort();
    qCInfo(hio::log::lcMcp).nospace() << "MCP HTTP: listening on 127.0.0.1:" << m_impl->shared.port;
    return {};
}

void HttpServer::close() {
    if (m_impl->tcp.isListening()) {
        m_impl->tcp.close();
    }
    auto connections = std::move(m_impl->connections);
    m_impl->connections.clear();
    for (auto& connection : connections) {
        connection->shutdown();
    }
}

bool HttpServer::isListening() const { return m_impl->tcp.isListening(); }

quint16 HttpServer::port() const {
    return m_impl->tcp.isListening() ? m_impl->tcp.serverPort() : m_impl->requestedPort;
}

std::size_t HttpServer::connectionCount() const { return m_impl->openConnections(); }

void HttpServer::setToken(std::string token) { m_impl->shared.token = std::move(token); }

} // namespace hio::mcp
