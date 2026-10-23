#include "hio/mcp/stdio_transport.h"

#include "hio/core/log.h"
#include "hio/mcp/json_rpc.h"
#include "hio/mcp/protocol.h"
#include "hio/mcp/reply_channel.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/server.h"

#include <QByteArray>
#include <QFileDevice>
#include <QIODevice>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QStringLiteral>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>
#include <QtTypes>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace hio::mcp {

// The one, persistent reply channel of a stdio transport.
class StdioChannel final : public IReplyChannel {
public:
    explicit StdioChannel(StdioTransport& transport) : m_transport(&transport) {}

    [[nodiscard]] bool canStream() const override { return true; }
    void startStream() override {}
    void notify(const QJsonObject& notification) override {
        if (isOpen()) {
            m_transport->write(serialize(notification));
        }
    }
    void complete(const QJsonObject& response, const ReplyMeta& /*meta*/) override {
        if (isOpen() && !response.isEmpty()) {
            m_transport->write(serialize(response));
        }
    }
    [[nodiscard]] bool isOpen() const override { return !m_transport.isNull() && !m_closed; }
    void setClosedHandler(std::function<void(CloseReason)> handler) override {
        m_closedHandler = std::move(handler);
    }

    void close(CloseReason reason) {
        if (m_closed) {
            return;
        }
        m_closed = true;
        if (auto handler = std::exchange(m_closedHandler, {})) {
            handler(reason);
        }
    }

private:
    QPointer<StdioTransport> m_transport;
    std::function<void(CloseReason)> m_closedHandler;
    bool m_closed = false;
};

StdioTransport::StdioTransport(McpServer& server, ILineSource& input, QIODevice& output,
                               QObject* parent)
    : QObject(parent), m_server(server), m_input(input), m_output(output),
      m_channel(std::make_shared<StdioChannel>(*this)), m_connectionId(nextConnectionId()) {}

StdioTransport::~StdioTransport() {
    m_input.setWakeup({});
    m_channel->close(CloseReason::Shutdown);
}

void StdioTransport::start() {
    // The reader thread only posts a queued call; m_wakePending collapses bursts into one post.
    m_input.setWakeup([this] {
        if (!m_wakePending.exchange(true)) {
            QMetaObject::invokeMethod(this, &StdioTransport::drain, Qt::QueuedConnection);
        }
    });
    drain();
}

void StdioTransport::drain() {
    m_wakePending.store(false);
    const std::size_t budget = m_server.limits().stdioLinesPerWakeup;
    for (std::size_t handled = 0; handled < budget; ++handled) {
        std::optional<ILineSource::Line> line = m_input.takeLine();
        if (!line) {
            if (m_input.atEnd() && !m_inputClosedEmitted) {
                m_inputClosedEmitted = true;
                qCInfo(hio::log::lcMcp) << "MCP stdio: end of input";
                emit inputClosed();
            }
            return;
        }
        if (line->overlong) {
            qCWarning(hio::log::lcMcp)
                << "MCP stdio: a line longer than" << m_server.limits().maxStdioLineBytes
                << "bytes was dropped";
            m_channel->complete(
                errorResponse(QJsonValue::Undefined,
                              RpcError{.code = protocol::errc::parseError,
                                       .message = QStringLiteral("Parse error: line too long")}),
                ReplyMeta{});
            continue;
        }
        if (line->text.trimmed().isEmpty()) {
            continue;
        }
        m_server.handle(line->text, m_channel,
                        TransportInfo{.kind = TransportKind::Stdio,
                                      .connectionId = m_connectionId,
                                      .protocolVersionHeader = std::nullopt,
                                      .methodHeader = std::nullopt,
                                      .nameHeader = std::nullopt,
                                      .sessionId = std::nullopt});
    }
    // Budget used up: continue in the next event-loop turn.
    if (!m_wakePending.exchange(true)) {
        QMetaObject::invokeMethod(this, &StdioTransport::drain, Qt::QueuedConnection);
    }
}

void StdioTransport::write(const QByteArray& message) {
    if (m_outputFailed) {
        return;
    }
    QByteArray line = message;
    line.append('\n');
    const qint64 written = m_output.write(line);
    bool ok = written == line.size();
    if (ok) {
        if (auto* file = qobject_cast<QFileDevice*>(&m_output)) {
            ok = file->flush();
        }
    }
    if (!ok) {
        m_outputFailed = true;
        qCWarning(hio::log::lcMcp) << "MCP stdio: writing to the output failed";
        m_channel->close(CloseReason::PeerClosed);
        emit outputFailed();
    }
}

} // namespace hio::mcp
