#pragma once

// A tiny local HTTP/1.1 server on 127.0.0.1 for the firmware catalog tests (plan/features/0003,
// A4 and F5). It runs on the thread that creates it, so the test must run the Qt event loop
// while a client talks to it (waitForDownload(), or the CLI tests' event-loop process wait).

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTypes>

#include <memory>
#include <utility>

namespace hio::firmware::test {

class HttpFixture {
public:
    struct Response {
        int status = 200;
        QByteArray body;
        QByteArray location; // Location header (redirects)
        bool hang = false;   // accept the request and never answer
        int trickleMs = 0;   // > 0: the headers at once, then one body byte per trickleMs
        // Plan 0016: extra response headers ("Name: value" lines without CRLF).
        QList<QByteArray> headers;

        static Response ok(QByteArray body) {
            return {.status = 200,
                    .body = std::move(body),
                    .location = {},
                    .hang = false,
                    .trickleMs = 0,
                    .headers = {}};
        }
        static Response withStatus(int code, QByteArray body = {}) {
            return {.status = code,
                    .body = std::move(body),
                    .location = {},
                    .hang = false,
                    .trickleMs = 0,
                    .headers = {}};
        }
        static Response redirect(QByteArray location) {
            return {.status = 302,
                    .body = {},
                    .location = std::move(location),
                    .hang = false,
                    .trickleMs = 0,
                    .headers = {}};
        }
        static Response hanging() {
            return {.status = 0,
                    .body = {},
                    .location = {},
                    .hang = true,
                    .trickleMs = 0,
                    .headers = {}};
        }
        // Plan 0021 (review F9): a slow server that keeps sending.
        static Response trickling(QByteArray body, int intervalMs) {
            return {.status = 200,
                    .body = std::move(body),
                    .location = {},
                    .hang = false,
                    .trickleMs = intervalMs,
                    .headers = {}};
        }
    };

    HttpFixture() {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] { accept(); });
    }

    [[nodiscard]] bool listening() const { return m_server.isListening(); }
    [[nodiscard]] QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1/chester").arg(m_server.serverPort());
    }
    // Plan 0016: "http://127.0.0.1:<port>" (paths of other services, e.g. the release list).
    [[nodiscard]] QString origin() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }
    void set(const QString& path, const Response& response) { m_responses.insert(path, response); }
    [[nodiscard]] int requests(const QString& path) const { return m_requests.value(path); }

private:
    void accept() {
        while (QTcpSocket* socket = m_server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                buffer->append(socket->readAll());
                if (!buffer->contains("\r\n\r\n")) {
                    return;
                }
                const QList<QByteArray> requestLine =
                    buffer->left(buffer->indexOf("\r\n")).split(' ');
                buffer->clear();
                const QString path =
                    requestLine.size() > 1 ? QString::fromLatin1(requestLine.at(1)) : QString();
                m_requests[path] += 1;
                const Response response = m_responses.value(path, Response::withStatus(404));
                if (response.hang) {
                    return;
                }
                QByteArray reply = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n";
                reply += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
                if (!response.location.isEmpty()) {
                    reply += "Location: " + response.location + "\r\n";
                }
                for (const QByteArray& header : response.headers) {
                    reply += header + "\r\n";
                }
                reply += "Connection: close\r\n\r\n";
                if (response.trickleMs > 0) {
                    socket->write(reply);
                    // Qt parent ownership: the socket.
                    auto* timer = new QTimer(socket); // NOLINT(cppcoreguidelines-owning-memory)
                    auto sent = std::make_shared<qsizetype>(0);
                    QObject::connect(timer, &QTimer::timeout, socket,
                                     [socket, timer, sent, body = response.body] {
                                         if (*sent >= body.size()) {
                                             timer->stop();
                                             socket->disconnectFromHost();
                                             return;
                                         }
                                         socket->write(body.mid(*sent, 1));
                                         ++*sent;
                                     });
                    timer->start(response.trickleMs);
                    return;
                }
                socket->write(reply + response.body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    QTcpServer m_server; // owns the sockets (Qt parent ownership)
    QMap<QString, Response> m_responses;
    QMap<QString, int> m_requests;
};

} // namespace hio::firmware::test
